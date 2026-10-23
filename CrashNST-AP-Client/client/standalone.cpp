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
        // V1.2.74: each game's log is named after when the game started (client-2026-10-23_16-01-05.log), so the
        // newest is easy to find, and only the newest 10 are kept (they only ever get written, never read back)
        {
            std::error_code error;
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> old;
            for (const auto &entry : std::filesystem::directory_iterator(state / L"logs", error))
            {
                const auto name = entry.path().filename().wstring();
                if (entry.is_regular_file(error) && name.rfind(L"client-", 0) == 0 && entry.path().extension() == L".log")
                    old.emplace_back(entry.last_write_time(error), entry.path());
            }
            std::sort(old.begin(), old.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            for (std::size_t i = 9; i < old.size(); ++i)
                std::filesystem::remove(old[i].second, error);
        }
        SYSTEMTIME now{};
        GetLocalTime(&now);
        char stamp[64]{};
        std::snprintf(stamp, sizeof(stamp), "client-%04u-%02u-%02u_%02u-%02u-%02u.log", unsigned(now.wYear), unsigned(now.wMonth), unsigned(now.wDay),
                      unsigned(now.wHour), unsigned(now.wMinute), unsigned(now.wSecond));
        auto output = std::make_unique<DebugLog>(state / L"logs" / stamp, debug_console());
        log_file = output.release();
        std::cout.rdbuf(log_file);
        std::cerr.rdbuf(log_file);
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
