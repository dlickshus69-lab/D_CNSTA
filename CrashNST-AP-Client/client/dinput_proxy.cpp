#include <windows.h>
#include <unknwn.h>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <cstdint>
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
