#include "standalone.h"
#include "session.h"
#include "console.h"
#include "game_runtime.h"
#include "save_format.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>
extern "C" bool NST_PrepareRenderer();

namespace
{
    std::atomic_bool enabled{false}, bound{false};
    std::filesystem::path state;
    using Bind = BOOL(WINAPI *)(const wchar_t *);
    Bind set_appdata = nullptr;
    
    class DebugLog : public std::streambuf
    {
        std::ofstream file;
        HANDLE console;
        std::mutex mutex;
        // The Debug window gets its copy on a thread of its own. While text is selected in a console window (Ctrl+A,
        // or dragging over it), Windows makes every write to it wait until the selection ends, and a log line written
        // by the game's own thread then froze the game (V1.1.72: frozen for a minute and a half while the log was
        // being copied). Now only this thread waits; the log file is written at once as before.
        static constexpr std::size_t console_backlog = 4 << 20;
        std::mutex console_mutex;
        std::condition_variable console_ready;
        std::string console_pending;
        bool console_dropped = false;
        void console_writer()
        {
            std::string chunk;
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(console_mutex);
                    console_ready.wait(lock, [this] { return !console_pending.empty(); });
                    chunk.swap(console_pending);
                    console_dropped = false;
                }
                DWORD written = 0;
                WriteFile(console, chunk.data(), static_cast<DWORD>(chunk.size()), &written, nullptr);
                chunk.clear();
            }
        }
        void to_console(const char *text, std::streamsize count)
        {
            if (!console || console == INVALID_HANDLE_VALUE)
                return;
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                if (console_pending.size() + static_cast<std::size_t>(count) <= console_backlog)
                    console_pending.append(text, static_cast<std::size_t>(count));
                else if (!console_dropped)
                {
                    // the window has been held for a long time: the file still has everything
                    console_dropped = true;
                    console_pending += "\n[Debug window was paused; some lines are only in the log file]\n";
                }
            }
            console_ready.notify_one();
        }
        std::streamsize xsputn(const char *text, std::streamsize count) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            file.write(text, count);
            file.flush();
            to_console(text, count);
            return file ? count : 0;
        }
        int_type overflow(int_type value) override
        {
            if (traits_type::eq_int_type(value, traits_type::eof()))
                return traits_type::not_eof(value);
            const auto character = traits_type::to_char_type(value);
            return xsputn(&character, 1) == 1 ? value : traits_type::eof();
        }
        int sync() override
        {
            std::lock_guard<std::mutex> lock(mutex);
            file.flush();
            return file ? 0 : -1;
        }

    public:
        DebugLog(const std::filesystem::path &path, HANDLE output) : file(path, std::ios::app), console(output)
        {
            if (!file)
                throw std::runtime_error("Cannot open AP runtime log");
            // the log lives as long as NST does
            if (console && console != INVALID_HANDLE_VALUE)
                std::thread(&DebugLog::console_writer, this).detach();
        }
    };
    DebugLog *log_file = nullptr;
    // V1.3.75 (Eric's V1.3.74 run: "game crashed", and the log just stopped): a crash goes into the log. A handler last
    // in Windows' list (the game's own and ours go first) writes each of the first 12 serious exceptions (an access
    // violation, an illegal instruction, a stack overflow, heap corruption ...): which thread, where it happened (the
    // module and how far into it, the address an access violation touched), the registers and the debug registers (the
    // client's watches), and 16 frames of the stack, straight into the log file with Windows' own file calls (the C++
    // stream might be what broke), from a buffer of its own (the stack might be what broke). It changes nothing else: a
    // game that handles one itself goes on as before
    wchar_t crash_log_path[1024]{};
    std::atomic_uint crash_count{0};
    std::atomic_flag crash_busy = ATOMIC_FLAG_INIT;
    char crash_text[8192];
    int crash_used = 0;
    void crash_add(const char *format, ...)
    {
        if (crash_used >= static_cast<int>(sizeof crash_text) - 1)
            return;
        va_list args;
        va_start(args, format);
        const int wrote = std::vsnprintf(crash_text + crash_used, sizeof crash_text - static_cast<std::size_t>(crash_used), format, args);
        va_end(args);
        if (wrote > 0)
            crash_used = std::min(crash_used + wrote, static_cast<int>(sizeof crash_text) - 1);
    }
    // (an address as its module and how far into it, the way the CI build's symbols can be looked up)
    void crash_where(DWORD64 at)
    {
        HMODULE module = nullptr;
        if (at && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(at), &module) &&
            module)
        {
            wchar_t path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
            const wchar_t *name = path;
            for (DWORD i = 0; i < length; ++i)
                if (path[i] == L'\\' || path[i] == L'/')
                    name = path + i + 1;
            char narrow[MAX_PATH]{};
            WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof narrow, nullptr, nullptr);
            crash_add("%s+0x%llx", narrow, static_cast<unsigned long long>(at - reinterpret_cast<DWORD64>(module)));
        }
        else
            crash_add("0x%llx", static_cast<unsigned long long>(at));
    }
    // (what's written so far, into the log file: before the stack is walked too, in case that's what fails)
    void crash_flush()
    {
        const HANDLE file = CreateFileW(crash_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD wrote = 0;
            WriteFile(file, crash_text, static_cast<DWORD>(crash_used), &wrote, nullptr);
            FlushFileBuffers(file);
            CloseHandle(file);
        }
        crash_used = 0;
    }
    LONG CALLBACK crash_handler(EXCEPTION_POINTERS *e)
    {
        const auto *record = e->ExceptionRecord;
        switch (record->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        case EXCEPTION_INVALID_DISPOSITION:
        case 0xC0000374: // (heap corruption)
            break;
        default:
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (!crash_log_path[0] || crash_count.load() >= 12 || crash_busy.test_and_set())
            return EXCEPTION_CONTINUE_SEARCH;
        const unsigned which = crash_count.fetch_add(1) + 1;
        crash_used = 0;
        const CONTEXT &c = *e->ContextRecord;
        crash_add("Crash report %u: exception 0x%08lx in thread %lu at ", which, static_cast<unsigned long>(record->ExceptionCode), GetCurrentThreadId());
        crash_where(reinterpret_cast<DWORD64>(record->ExceptionAddress));
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        {
            const auto kind = record->ExceptionInformation[0];
            crash_add(" (%s 0x%llx)", kind == 0 ? "reading" : kind == 1 ? "writing" : kind == 8 ? "running" : "touching",
                      static_cast<unsigned long long>(record->ExceptionInformation[1]));
        }
        crash_add("\n  rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx rbp %llx rsp %llx\n  r8 %llx r9 %llx r10 %llx r11 %llx r12 %llx r13 %llx r14 %llx r15 %llx",
                  c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.Rbp, c.Rsp, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13, c.R14, c.R15);
        crash_add("\n  debug registers: dr0 %llx dr1 %llx dr2 %llx dr3 %llx dr6 %llx dr7 %llx\n", c.Dr0, c.Dr1, c.Dr2, c.Dr3, c.Dr6, c.Dr7);
        crash_flush();
        crash_add("  stack:");
        // (the stack, frame by frame, as Windows unwinds it; a frame with no unwind data goes on from its return address)
        if (record->ExceptionCode != EXCEPTION_STACK_OVERFLOW)
        {
            CONTEXT walk = c;
            for (int frame = 0; frame < 16 && walk.Rip; ++frame)
            {
                crash_add(frame % 4 ? " " : "\n    ");
                crash_where(walk.Rip);
                DWORD64 image = 0;
                if (const auto entry = RtlLookupFunctionEntry(walk.Rip, &image, nullptr))
                {
                    void *data = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, walk.Rip, entry, &walk, &data, &establisher, nullptr);
                }
                else
                {
                    DWORD64 back = 0;
                    SIZE_T got = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(walk.Rsp), &back, sizeof back, &got) || got != sizeof back)
                        break;
                    walk.Rip = back, walk.Rsp += 8;
                }
            }
        }
        crash_add("\n");
        crash_flush();
        crash_busy.clear();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    HANDLE debug_console()
    {
        if (!AllocConsole() && GetLastError() != ERROR_ACCESS_DENIED)
            return nullptr;
        SetConsoleTitleW(L"Crash NST Archipelago — Debug");
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCtrlHandler(nullptr, TRUE); // Ctrl+C
        const auto menu = GetSystemMenu(GetConsoleWindow(), FALSE);
        if (menu)
            EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        const auto input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(input, &mode))
            SetConsoleMode(input, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
        return GetStdHandle(STD_OUTPUT_HANDLE);
    }
    std::string address = "127.0.0.1:38281", slot = "CrashPlayer";
    void read_connection(const std::filesystem::path &path, std::string &server, std::string &name)
    {
        if (!std::filesystem::exists(path))
        {
            Json::Value defaults;
            defaults["server"] = server;
            defaults["slot"] = name;
            write_state_json(path, defaults);
        }
        const auto config = read_state_json(path);
        if (!config["server"].isString() || !ingame::valid_server(config["server"].asString()) ||
            !config["slot"].isString() || config["slot"].asString().empty() || config["slot"].asString().size() > 255 ||
            config["slot"].asString().find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Invalid connection.json: provide a valid server and slot");
        server = config["server"].asString();
        name = config["slot"].asString();
    }
    DWORD WINAPI runtime(void *)
    {
        try
        {
            nst_client_main(address, slot);
            std::cout << (bound ? "AP stopped. Close NST before reconnecting or changing slots." : "AP startup cancelled.") << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Standalone AP: " << e.what() << std::endl;
        }
        // there is no game window/event loop yet while the save gate is waiting
        // cancelling the startup screen must close NST, not leave it stuck
        if (!bound)
        {
            std::cout << "Closing NST before game save loading." << std::endl;
            ExitProcess(0);
        }
        return 0;
    }
}
namespace standalone
{
    bool save_ready() { return bound; }
    std::filesystem::path state_directory() { return state; }
    std::filesystem::path connection_file() { return state / L"connection.json"; }
    void bind_saves(const std::filesystem::path &appdata)
    {
        if (enabled && !bound)
            try
            {
                constexpr wchar_t folder[] = L"Crash Bandicoot(TM) N. Sane Trilogy";
                const auto accepted = [](const std::filesystem::path &path)
                {
                    try
                    {
                        return std::filesystem::is_regular_file(path) && save_probe::license_accepted(save_probe::read_file(path));
                    }
                    catch (const std::exception &)
                    {
                        return false;
                    }
                };
                wchar_t normal[32768]{};
                const auto length = GetEnvironmentVariableW(L"APPDATA", normal, std::size(normal));
                bool inherit = length && length < std::size(normal) && accepted(std::filesystem::path(normal) / folder / L"SAVE_OPTIONS.sav");
                // reuse license acceptance from a previous AP profile when no normal save has it
                const auto profiles = state / L"sessions" / L"profiles";
                if (!inherit && std::filesystem::is_directory(profiles))
                    for (const auto &profile : std::filesystem::directory_iterator(profiles))
                    {
                        if (accepted(profile.path() / L"AppData" / folder / L"SAVE_OPTIONS.sav"))
                        {
                            inherit = true;
                            break;
                        }
                    }
                if (inherit)
                {
                    ingame::inherit_license();
                    std::cout << "License acceptance inherited from an existing NST save.\n";
                }
            }
            catch (const std::exception &e)
            {
                std::cout << "License inheritance unavailable: " << e.what() << ". Using the game license prompt.\n";
            }
        if (!enabled || !set_appdata || !set_appdata(appdata.c_str()))
            throw std::runtime_error("Cannot bind isolated game save path. NST remains at startup");
        bound = true;
    }
}
std::string nst_cache_path() { return (standalone::state_directory() / L"APCpp_datapkg.cache").string(); }
int nst_ap_printf(const char *format, ...)
{
    char line[4096]{};
    va_list arguments;
    va_start(arguments, format);
    const auto count = std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    std::cout << line << std::flush;
    return count;
}
extern "C" __declspec(dllexport) DWORD WINAPI NST_Bootstrap(void *callback)
{
    if (enabled)
        return 1;
    try
    {
        wchar_t host[32768]{};
        GetModuleFileNameW(nullptr, host, std::size(host));
        if (!nst_verified_executable(host))
            return 0;
        if (!NST_PrepareRenderer())
            throw std::runtime_error("Cannot capture NST's renderer creation before startup");
        wchar_t local[32768]{};
        const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, std::size(local));
        if (!count || count >= std::size(local))
            throw std::runtime_error("LOCALAPPDATA is unavailable");
        state = std::filesystem::path(local) / L"CrashNSTAP" / L"state";
        std::filesystem::create_directories(state / L"logs");
        set_appdata = reinterpret_cast<Bind>(callback);
        if (!set_appdata)
            throw std::runtime_error("Missing startup save-path gate");
        // each game's log is named after when the game started (client-2026-10-23_16-01-05.log), so the newest is easy
        // to find; old ones are cleared out below, once this one is open
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char stamp[64]{};
        std::snprintf(stamp, sizeof(stamp), "client-%04u-%02u-%02u_%02u-%02u-%02u.log", unsigned(now.wYear), unsigned(now.wMonth), unsigned(now.wDay),
                      unsigned(now.wHour), unsigned(now.wMinute), unsigned(now.wSecond));
        auto output = std::make_unique<DebugLog>(state / L"logs" / stamp, debug_console());
        log_file = output.release();
        std::cout.rdbuf(log_file);
        std::cerr.rdbuf(log_file);
        // (V1.3.75: a crash goes into this log, see crash_handler)
        {
            const auto path = (state / L"logs" / stamp).wstring();
            if (path.size() < std::size(crash_log_path))
            {
                std::copy(path.begin(), path.end(), crash_log_path);
                crash_log_path[path.size()] = 0;
                AddVectoredExceptionHandler(0, crash_handler);
            }
        }
        // only the newest 10 logs are kept (they are only ever written, never read back). Plain Windows calls: V1.2.74
        // went over the folder with std::filesystem before the log was open, and the game no longer started
        {
            struct OldLog
            {
                FILETIME written;
                std::wstring name;
            };
            std::vector<OldLog> logs;
            WIN32_FIND_DATAW found{};
            const auto folder = (state / L"logs").wstring();
            const auto search = FindFirstFileW((folder + L"\\client-*.log").c_str(), &found);
            if (search != INVALID_HANDLE_VALUE)
            {
                do
                    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        logs.push_back({found.ftLastWriteTime, found.cFileName});
                while (FindNextFileW(search, &found));
                FindClose(search);
            }
            std::sort(logs.begin(), logs.end(), [](const OldLog &a, const OldLog &b) { return CompareFileTime(&a.written, &b.written) > 0; });
            for (std::size_t i = 10; i < logs.size(); ++i)
                DeleteFileW((folder + L"\\" + logs[i].name).c_str());
        }
        try
        {
            read_connection(standalone::connection_file(), address, slot);
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << ". Correct settings with F1 and /connect." << std::endl;
        }
        std::cout << "AP startup: autoconnect from " << standalone::connection_file().string()
                  << ". Startup stays quiet while connecting and syncing. The connection console opens if fallback fails." << std::endl;
        enabled = true;
        const auto thread = CreateThread(nullptr, 0, runtime, nullptr, 0, nullptr);
        if (!thread)
        {
            enabled = false;
            throw std::runtime_error("Cannot start AP runtime worker");
        }
        CloseHandle(thread);
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "AP startup: " << e.what() << std::endl;
        OutputDebugStringA(e.what());
        return 0;
    }
}
