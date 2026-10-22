#include <windows.h>
#include <unknwn.h>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <string>
#include <vector>
#ifdef _MSC_VER
#include <intrin.h>
#endif

// Status bits: system ready=1, payload loaded=2, worker finished=4,
// system failure=8, payload failure=16, unrelated host=32.
namespace
{
    HMODULE self = nullptr, system_input = nullptr, payload = nullptr;
    INIT_ONCE system_once = INIT_ONCE_STATIC_INIT;
    volatile LONG status = 0;
    FARPROC functions[6]{};
    using Environment = char *(__cdecl *)(const char *);
    Environment original_environment = nullptr;
    void **environment_slot = nullptr;
    HANDLE save_gate = nullptr;
    volatile LONG save_bound = 0;
    char save_root[250]{};
    char *__cdecl isolated_environment(const char *name)
    {
#ifdef _MSC_VER
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#else
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        // only the game's save-path helper gets redirected. other getenv("APPDATA") callers keep their normal path.
        if (name && !_stricmp(name, "APPDATA") && caller >= base + 0xe6d320 && caller < base + 0xe6d9a0)
        {
            if (WaitForSingleObject(save_gate, INFINITE) != WAIT_OBJECT_0 || !InterlockedCompareExchange(&save_bound, 0, 0))
            {
                MessageBoxW(nullptr, L"Archipelago could not prepare its isolated save profile. NST will close. Normal saves were not opened. Check CrashNSTAP/loader.log and the AP runtime log.", L"Crash NST Archipelago", MB_OK | MB_ICONERROR);
                ExitProcess(ERROR_DLL_INIT_FAILED);
            }
            return save_root;
        }
        return original_environment ? original_environment(name) : nullptr;
    }
    BOOL WINAPI bind_save_root(const wchar_t *path)
    {
        if (!path || !save_gate || !environment_slot)
            return FALSE;
        wchar_t absolute[250]{};
        const auto size = GetFullPathNameW(path, std::size(absolute), absolute, nullptr);
        if (!size || size >= std::size(absolute) || CompareStringOrdinal(path, -1, absolute, -1, TRUE) != CSTR_EQUAL)
            return FALSE;
        BOOL substituted = FALSE;
        char converted[250]{};
        const auto count = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, path, -1, converted, sizeof(converted), nullptr, &substituted);
        if (!count || substituted || converted[0] == 0)
            return FALSE;
        if (InterlockedCompareExchange(&save_bound, 0, 0))
            return !std::strcmp(save_root, converted);
        std::memcpy(save_root, converted, count);
        InterlockedExchange(&save_bound, 1);
        return SetEvent(save_gate);
    }
    bool install_save_gate()
    {
        auto base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 1024 * 1024)
            return false;
        const auto pe = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;
        const auto size = pe->OptionalHeader.SizeOfImage;
        if (size < 0xe6d9a0)
            return false; // synthetic startup hosts have no NST save-path helper.
        const auto imports = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        // patch the executable's getenv import slot
        if (!imports.VirtualAddress || imports.VirtualAddress >= size || imports.Size > size - imports.VirtualAddress)
            return false;
        const auto entries = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress);
        for (unsigned i = 0; i < imports.Size / sizeof(*entries) && entries[i].Name; ++i)
        {
            if (!entries[i].OriginalFirstThunk || entries[i].OriginalFirstThunk >= size || !entries[i].FirstThunk || entries[i].FirstThunk >= size)
                continue;
            const auto names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + entries[i].OriginalFirstThunk);
            auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + entries[i].FirstThunk);
            for (unsigned j = 0; j < 4096 && (j + 1) * 8 <= size - entries[i].OriginalFirstThunk && (j + 1) * 8 <= size - entries[i].FirstThunk && names[j].u1.AddressOfData; ++j)
            {
                const auto at = names[j].u1.AddressOfData;
                if (IMAGE_SNAP_BY_ORDINAL64(at) || at >= size || size - at < 9)
                    continue;
                const auto imported = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + at)->Name;
                if (std::memcmp(imported, "getenv\0", 7))
                    continue;
                save_gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!save_gate)
                    return false;
                environment_slot = reinterpret_cast<void **>(&addresses[j].u1.Function);
                original_environment = reinterpret_cast<Environment>(*environment_slot);
                DWORD old = 0;
                if (!VirtualProtect(environment_slot, sizeof(void *), PAGE_READWRITE, &old))
                    return false;
                InterlockedExchangePointer(environment_slot, reinterpret_cast<void *>(isolated_environment));
                DWORD ignored = 0;
                return VirtualProtect(environment_slot, sizeof(void *), old, &ignored);
            }
        }
        return false;
    }
    // Skins: the game's archives\Crash.pak (Crash's model, textures and sounds) swapped for a skin mod's Crash.pak from
    // CrashNSTAP\skins before the game opens it, the same as installing the mod by hand. The skin to wear is the first
    // line of %LOCALAPPDATA%\CrashNSTAP\state\skin.txt (written by the client from the YAML's crash_skin or /skin).
    // The game's own Crash.pak is kept in skins\default; skins\installed.txt remembers which skin is in the archives
    // folder (name, size, write time), so a Steam update or verify is noticed and becomes the new default.
    // The result is left in CRASHNSTAP_SKIN ("name|wearing|path", "name|missing|", "name|failed|reason") for the client.
    std::wstring lower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return text;
    }
    std::wstring trim(std::wstring text)
    {
        const auto first = text.find_first_not_of(L" \t\r\n\ufeff");
        const auto last = text.find_last_not_of(L" \t\r\n");
        return first == std::wstring::npos ? std::wstring() : text.substr(first, last - first + 1);
    }
    std::wstring widen(const std::string &text)
    {
        if (text.empty())
            return {};
        std::wstring result(MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), static_cast<int>(result.size()));
        return result;
    }
    std::string narrow(const std::wstring &text)
    {
        if (text.empty())
            return {};
        std::string result(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), static_cast<int>(result.size()), nullptr, nullptr);
        return result;
    }
    struct FileStamp
    {
        unsigned long long size = 0, written = 0;
        bool operator==(const FileStamp &o) const { return size == o.size && written == o.written; }
    };
    bool stamp(const std::filesystem::path &path, FileStamp &result)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            return false;
        result.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        result.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
        return true;
    }
    // the YAML's names and what a skin's folder or file is likely called
    std::vector<std::wstring> skin_aliases(const std::wstring &name)
    {
        if (name == L"playstation_1" || name == L"playstation 1" || name == L"ps1")
            return {L"playstation_1", L"playstation 1", L"playstation1", L"playstation", L"ps1", L"psx", L"low-poly", L"lowpoly", L"low poly", L"low_poly"};
        if (name == L"biker")
            return {L"biker"};
        if (name == L"jetpack" || name == L"jet pack" || name == L"jet_pack")
            return {L"jetpack", L"jet pack", L"jet_pack", L"jet-pack"};
        return {name};
    }
    // the skin's Crash.pak: a .pak file in skins named for it, or the Crash.pak inside a folder named for it (any depth,
    // as the mod came); an exact name wins over a name that only contains it
    std::filesystem::path find_skin(const std::filesystem::path &skins, const std::wstring &name)
    {
        const auto aliases = skin_aliases(name);
        for (const bool exact : {true, false})
        {
            std::error_code error;
            for (std::filesystem::directory_iterator it(skins, error), end; !error && it != end; it.increment(error))
            {
                const auto entry = it->path();
                const bool folder = it->is_directory(error);
                const auto key = lower(folder ? entry.filename().wstring() : entry.stem().wstring());
                if (key == L"default" || (!folder && lower(entry.extension().wstring()) != L".pak"))
                    continue;
                const bool match = std::any_of(aliases.begin(), aliases.end(), [&](const std::wstring &a) {
                    return exact ? key == a : key.find(a) != std::wstring::npos;
                });
                if (!match)
                    continue;
                if (!folder)
                    return entry;
                std::filesystem::path any;
                std::error_code inner;
                for (std::filesystem::recursive_directory_iterator walk(entry, inner), stop; !inner && walk != stop; walk.increment(inner))
                {
                    if (walk.depth() > 4 || !walk->is_regular_file(inner) || lower(walk->path().extension().wstring()) != L".pak")
                        continue;
                    if (lower(walk->path().filename().wstring()) == L"crash.pak")
                        return walk->path();
                    if (any.empty())
                        any = walk->path();
                }
                if (!any.empty())
                    return any;
            }
        }
        return {};
    }
    void apply_skin(const std::filesystem::path &proxy_folder, const std::filesystem::path &game_folder)
    {
        std::wstring report;
        std::string outcome;
        try
        {
            [&] {
                const auto skins = proxy_folder / L"CrashNSTAP" / L"skins";
                const auto game_pak = game_folder / L"archives" / L"Crash.pak";
                const auto backup = skins / L"default" / L"Crash.pak";
                const auto record_file = skins / L"installed.txt";
                std::wstring want = L"default";
                static wchar_t local[32768]{};
                const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, std::size(local));
                if (count && count < std::size(local))
                {
                    std::ifstream in(std::filesystem::path(local) / L"CrashNSTAP" / L"state" / L"skin.txt");
                    std::string line;
                    if (in && std::getline(in, line) && !trim(widen(line)).empty())
                        want = lower(trim(widen(line)));
                }
                if (want == L"default_crash" || want == L"normal" || want == L"vanilla" || want == L"off")
                    want = L"default";
                FileStamp current;
                if (!stamp(game_pak, current))
                {
                    outcome = narrow(want) + "|failed|the game's archives\\Crash.pak was not found";
                    return;
                }
                // which skin the archives folder holds now: the recorded one if the file is still what was put there
                std::wstring installed = L"default";
                {
                    std::ifstream in(record_file);
                    std::string name, size, written;
                    if (in && std::getline(in, name) && std::getline(in, size) && std::getline(in, written))
                    {
                        FileStamp recorded{std::strtoull(size.c_str(), nullptr, 10), std::strtoull(written.c_str(), nullptr, 10)};
                        if (recorded == current)
                            installed = lower(trim(widen(name)));
                    }
                }
                const auto record = [&](const std::wstring &name) {
                    FileStamp now;
                    std::error_code error;
                    std::filesystem::create_directories(skins, error);
                    std::ofstream out(record_file, std::ios::trunc);
                    if (stamp(game_pak, now))
                        out << narrow(name) << '\n' << now.size << '\n' << now.written << '\n';
                };
                if (want == installed)
                {
                    outcome = narrow(want) + "|wearing|" + (want == L"default" ? std::string() : narrow(game_pak.wstring()));
                    report = L"already wearing " + want;
                    return;
                }
                // the game's own Crash.pak (or a Steam update of it) is kept before a skin first replaces it
                if (installed == L"default")
                {
                    FileStamp saved;
                    if (!stamp(backup, saved) || !(saved == current))
                    {
                        std::error_code error;
                        std::filesystem::create_directories(backup.parent_path(), error);
                        if (!CopyFileW(game_pak.c_str(), backup.c_str(), FALSE))
                        {
                            outcome = narrow(want) + "|failed|could not keep a copy of the game's Crash.pak (Windows error " + std::to_string(GetLastError()) + ")";
                            return;
                        }
                    }
                    record(L"default");
                }
                const auto source = want == L"default" ? backup : find_skin(skins, want);
                FileStamp found;
                if (source.empty() || !stamp(source, found))
                {
                    outcome = narrow(want) + "|missing|" + narrow(installed);
                    report = L"no skin named " + want + L" in " + skins.wstring();
                    return;
                }
                if (!CopyFileW(source.c_str(), game_pak.c_str(), FALSE))
                {
                    outcome = narrow(want) + "|failed|could not copy " + narrow(source.wstring()) + " (Windows error " + std::to_string(GetLastError()) + ")";
                    return;
                }
                record(want);
                outcome = narrow(want) + "|wearing|" + narrow(source.wstring());
                report = L"put on " + want + L" from " + source.wstring();
            }();
        }
        catch (...)
        {
            outcome = "default|failed|unexpected error";
        }
        if (!outcome.empty())
            SetEnvironmentVariableW(L"CRASHNSTAP_SKIN", widen(outcome).c_str());
        if (!report.empty())
            OutputDebugStringW((L"Crash NST AP skins: " + report + L"\n").c_str());
    }
    const char *names[]{"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
                        "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};
    BOOL CALLBACK load_system(PINIT_ONCE, void *, void **)
    {
        wchar_t directory[32768]{};
        const auto count = GetSystemDirectoryW(directory, std::size(directory));
        if (count && count < std::size(directory))
        {
            const auto path = std::filesystem::path(directory) / L"dinput8.dll";
            system_input = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        bool ok = system_input && system_input != self;
        for (unsigned i = 0; ok && i < 6; ++i)
        {
            functions[i] = GetProcAddress(system_input, names[i]);
            // Wine implements the first five exports. GetdfDIJoystick is Windows-only
            ok = (i == 5 || functions[i]) && (!functions[i] || functions[i] != GetProcAddress(self, names[i]));
        }
        InterlockedOr(&status, ok ? 1 : 8);
        return TRUE; // failed system load stays failed
    }
    FARPROC original(unsigned index)
    {
        InitOnceExecuteOnce(&system_once, load_system, nullptr, nullptr);
        return (InterlockedCompareExchange(&status, 0, 0) & 1) ? functions[index] : nullptr;
    }
    DWORD WINAPI preload(void *)
    {
        DWORD error = 0;
        try
        {
            wchar_t host[32768]{}, module[32768]{};
            const auto host_count = GetModuleFileNameW(nullptr, host, std::size(host));
            const auto module_count = GetModuleFileNameW(self, module, std::size(module));
            if (!host_count || host_count >= std::size(host) || !module_count || module_count >= std::size(module))
            {
                InterlockedOr(&status, 16 | 4);
                return 0;
            }
            if (_wcsicmp(std::filesystem::path(host).filename().c_str(), L"CrashBandicootNSaneTrilogy.exe"))
            {
                InterlockedOr(&status, 32 | 4);
                return 0;
            }
            const auto folder = std::filesystem::path(module).parent_path() / L"CrashNSTAP";
            if (original(0))
            {
                const auto dll = folder / L"crash_nst_ingame.dll";
                // explicit payload directory, followed by normal system dependencies
                payload = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (payload && GetProcAddress(payload, "NST_Start") && GetProcAddress(payload, "NST_Tick"))
                {
                    InterlockedOr(&status, 2);
                    const auto bootstrap = reinterpret_cast<DWORD(WINAPI *)(void *)>(GetProcAddress(payload, "NST_Bootstrap"));
                    if (environment_slot && (!bootstrap || !bootstrap(reinterpret_cast<void *>(bind_save_root))))
                    {
                        error = ERROR_DLL_INIT_FAILED;
                        if (save_gate)
                            SetEvent(save_gate);
                    }
                }
                else
                {
                    error = GetLastError();
                    if (!error)
                        error = ERROR_PROC_NOT_FOUND;
                    InterlockedOr(&status, 16);
                }
            }
            else
                error = ERROR_MOD_NOT_FOUND;
            char message[256]{};
            std::snprintf(message, sizeof(message), "Crash NST AP proxy: status=0x%lx, Windows error=%lu\r\n%s\r\n",
                          static_cast<unsigned long>(InterlockedCompareExchange(&status, 0, 0) | 4), static_cast<unsigned long>(error),
                          (InterlockedCompareExchange(&status, 0, 0) & 2) ? "AP DLL loaded. Standalone networking and cloud-save startup initialized." : "AP payload unavailable. DirectInput forwarding remains independent.");
            OutputDebugStringA(message);
            const auto file = CreateFileW((folder / L"loader.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                DWORD done = 0;
                WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &done, nullptr);
                CloseHandle(file);
            }
            InterlockedOr(&status, 4);
            if (error && save_gate)
                SetEvent(save_gate);
        }
        catch (...)
        {
            InterlockedOr(&status, 16 | 4);
            if (save_gate)
                SetEvent(save_gate);
            OutputDebugStringA("Crash NST AP proxy: preload failed. Input forwarding remains available.\n");
        }
        return 0;
    }
}
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, void **result, IUnknown *outer)
{
    using Call = HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, void **, IUnknown *);
    const auto call = reinterpret_cast<Call>(original(0));
    if (!call)
    {
        if (result)
            *result = nullptr;
        return E_FAIL;
    }
    return call(instance, version, iid, result, outer);
}
extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    const auto call = reinterpret_cast<HRESULT(WINAPI *)()>(original(1));
    return call ? call() : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **result)
{
    const auto call = reinterpret_cast<HRESULT(WINAPI *)(REFCLSID, REFIID, void **)>(original(2));
    if (!call)
    {
        if (result)
            *result = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return call(clsid, iid, result);
}
extern "C" HRESULT WINAPI DllRegisterServer()
{
    const auto call = reinterpret_cast<HRESULT(WINAPI *)()>(original(3));
    return call ? call() : E_FAIL;
}
extern "C" HRESULT WINAPI DllUnregisterServer()
{
    const auto call = reinterpret_cast<HRESULT(WINAPI *)()>(original(4));
    return call ? call() : E_FAIL;
}
extern "C" const void *WINAPI GetdfDIJoystick()
{
    const auto call = reinterpret_cast<const void *(WINAPI *)()>(original(5));
    return call ? call() : nullptr;
}
extern "C" DWORD WINAPI NST_ProxyStatus() { return static_cast<DWORD>(InterlockedCompareExchange(&status, 0, 0)); }
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        self = module;
        DisableThreadLibraryCalls(module);
        // static TLS can keep thread notifications enabled. 
        // a stack buffer here reserves 64 KB on every notification, 
        // overflowing NST graphics workers before the DLL_PROCESS_ATTACH branch is reached
        static wchar_t host[32768]{};
        const auto count = GetModuleFileNameW(nullptr, host, std::size(host));
        if (!count || count >= std::size(host))
            return FALSE;
        const auto slash = wcsrchr(host, L'\\');
        const auto filename = slash ? slash + 1 : host;
        if (!_wcsicmp(filename, L"CrashBandicootNSaneTrilogy.exe"))
        {
            const auto base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
            const auto pe = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
            if (pe->OptionalHeader.SizeOfImage >= 0xe6d9a0 && !install_save_gate())
                return FALSE;
            // before the game opens its archives
            static wchar_t proxy[32768]{};
            const auto proxy_count = GetModuleFileNameW(module, proxy, std::size(proxy));
            if (proxy_count && proxy_count < std::size(proxy))
                apply_skin(std::filesystem::path(proxy).parent_path(), std::filesystem::path(host).parent_path());
        }
        // the worker runs after DLL notifications release loader lock
        const auto thread = CreateThread(nullptr, 0, preload, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        else
        {
            InterlockedOr(&status, 16 | 4);
            if (save_gate)
                SetEvent(save_gate);
        }
    }
    return TRUE;
}
