#include <windows.h>
#include <unknwn.h>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cwctype>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
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
    // the same file? Same size, and the same first and last 256 KB (each skin's Crash.pak differs from the game's early
    // on; the client's Crash colors differ from each other only at the end, where their textures are)
    bool same_file(const std::filesystem::path &a, const std::filesystem::path &b)
    {
        FileStamp sa, sb;
        if (!stamp(a, sa) || !stamp(b, sb) || sa.size != sb.size)
            return false;
        std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
        const std::uint64_t span = 256 * 1024;
        for (const std::uint64_t at : {std::uint64_t{0}, sa.size > span ? sa.size - span : 0})
        {
            std::vector<char> ba(span), bb(span);
            fa.clear(), fb.clear();
            fa.seekg(static_cast<std::streamoff>(at)), fb.seekg(static_cast<std::streamoff>(at));
            fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
            fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
            if (fa.gcount() != fb.gcount() || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin()))
                return false;
        }
        return true;
    }
    // the name of the skin in the skins folder whose Crash.pak is this file, or empty
    std::wstring skin_of(const std::filesystem::path &skins, const std::filesystem::path &file)
    {
        std::error_code error;
        for (std::filesystem::directory_iterator it(skins, error), end; !error && it != end; it.increment(error))
        {
            const bool folder = it->is_directory(error);
            const auto key = lower(folder ? it->path().filename().wstring() : it->path().stem().wstring());
            if (key == L"default" || (!folder && lower(it->path().extension().wstring()) != L".pak"))
                continue;
            const auto pak = find_skin(skins, key);
            if (!pak.empty() && same_file(pak, file))
                return key;
        }
        return {};
    }
    // returns the skin the archives folder holds afterwards ("default", a skin's name, or empty when unknown)
    std::wstring apply_skin(const std::filesystem::path &proxy_folder, const std::filesystem::path &game_folder)
    {
        std::wstring report, worn_now;
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
                // no record of what's there (lost, or the file changed): it is the game's own Crash.pak unless it is one
                // of the skins, so a skin is never kept as the default
                bool recognized = installed != L"default";
                if (!recognized && !same_file(game_pak, backup))
                {
                    const auto known = skin_of(skins, game_pak);
                    if (!known.empty())
                        installed = known, recognized = true;
                }
                const auto record = [&](const std::wstring &name) {
                    FileStamp now;
                    std::error_code error;
                    std::filesystem::create_directories(skins, error);
                    std::ofstream out(record_file, std::ios::trunc);
                    if (stamp(game_pak, now))
                        out << narrow(name) << '\n' << now.size << '\n' << now.written << '\n';
                };
                worn_now = installed;
                if (want == installed)
                {
                    outcome = narrow(want) + "|wearing|" + (want == L"default" ? std::string() : narrow(game_pak.wstring()));
                    report = L"already wearing " + want;
                    return;
                }
                if (recognized)
                    record(installed);
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
                worn_now = want;
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
        return worn_now;
    }
    // Crash colors in the archives with their own copies of Crash's textures (V1.2.52): the client lists them in
    // %LOCALAPPDATA%\CrashNSTAP\state\color_patches.txt ("plan <colors skin>", then "archive|original size|record
    // offset|original record|colored file|length"), and they apply while that colors skin is worn. Each colored file
    // goes at the end of its archive (on a 2 KiB sector, like the archive's own files) and its record points there;
    // the original records and the archive's original size are kept in skins\patched.txt ("archive|original size|
    // patched size|plan|record offset|original record"), so the archive is put back exactly when the colors change
    // or come off. An archive that isn't as expected (a Steam update or verify) is left alone. The result is left in
    // CRASHNSTAP_COLORS for the client.
    struct PatchLine
    {
        std::wstring archive;
        std::string plan, original;
        unsigned long long original_size = 0, patched_size = 0, at = 0, length = 0;
        std::wstring file;
    };
    // (no string streams here: statically linked, they pull in number parsing this DLL's toolchain can't link)
    std::vector<std::string> split_line(const std::string &line)
    {
        std::vector<std::string> parts;
        std::size_t from = 0;
        for (auto bar = line.find('|'); bar != std::string::npos; bar = line.find('|', from))
            parts.push_back(line.substr(from, bar - from)), from = bar + 1;
        parts.push_back(line.substr(from));
        return parts;
    }
    bool from_hex(const std::string &text, unsigned char (&out)[16])
    {
        if (text.size() != 32)
            return false;
        for (int i = 0; i < 16; ++i)
        {
            const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            const int hi = digit(text[i * 2]), lo = digit(text[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out[i] = static_cast<unsigned char>(hi * 16 + lo);
        }
        return true;
    }
    bool file_at(HANDLE file, unsigned long long at, void *data, DWORD size, bool write)
    {
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(at);
        DWORD done = 0;
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN))
            return false;
        return (write ? WriteFile(file, data, size, &done, nullptr) : ReadFile(file, data, size, &done, nullptr)) && done == size;
    }
    unsigned long long file_size(HANDLE file)
    {
        LARGE_INTEGER size{};
        return GetFileSizeEx(file, &size) ? static_cast<unsigned long long>(size.QuadPart) : 0;
    }
    // whether the archive at `path` is one of the client's patched copies of Ripper Roo's: its names hold N. Sanity Beach's
    // crate layer, which the game's own never does (so a copy is never taken for the game's own, whatever the records say)
    bool patched_copy(const std::filesystem::path &path)
    {
        const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        bool found = false;
        unsigned char head[56]{};
        if (file_at(file, 0, head, 56, false))
        {
            std::uint32_t magic = 0, size = 0;
            std::uint64_t at = 0;
            std::memcpy(&magic, head, 4), std::memcpy(&at, head + 40, 8), std::memcpy(&size, head + 48, 4);
            if (magic == 0x1a414749 && size > 0 && size <= (64u << 20) && at <= file_size(file) && size <= file_size(file) - at)
            {
                std::string names(size, '\0');
                if (file_at(file, at, names.data(), size, false))
                {
                    std::transform(names.begin(), names.end(), names.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                    found = names.find("l101_nsanitybeach_crates.igz") != std::string::npos;
                }
            }
        }
        CloseHandle(file);
        return found;
    }
    void write_patched(const std::filesystem::path &path, const std::vector<PatchLine> &done)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream out(temporary, std::ios::trunc | std::ios::binary);
            for (const auto &d : done)
                out << narrow(d.archive) << '|' << d.original_size << '|' << d.patched_size << '|' << d.plan << '|' << d.at << '|' << d.original << '\n';
        }
        MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    void apply_color_patches(const std::filesystem::path &proxy_folder, const std::filesystem::path &game_folder, const std::wstring &worn)
    {
        unsigned patched = 0, restored = 0, kept = 0, skipped = 0;
        std::string why;
        try
        {
            const auto skins = proxy_folder / L"CrashNSTAP" / L"skins";
            const auto state = skins / L"patched.txt";
            const auto archives = game_folder / L"archives";
            // the plan, only while its colors skin is the one worn
            std::string plan_id;
            std::map<std::wstring, std::vector<PatchLine>> plan;
            {
                static wchar_t local[32768]{};
                const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, std::size(local));
                if (count && count < std::size(local))
                {
                    std::ifstream in(std::filesystem::path(local) / L"CrashNSTAP" / L"state" / L"color_patches.txt");
                    std::string line;
                    if (in && std::getline(in, line) && line.rfind("plan ", 0) == 0)
                    {
                        plan_id = narrow(trim(widen(line.substr(5))));
                        while (std::getline(in, line))
                        {
                            const auto f = split_line(narrow(trim(widen(line))));
                            if (f.size() != 6)
                                continue;
                            PatchLine p;
                            p.archive = widen(f[0]), p.plan = plan_id, p.original_size = std::strtoull(f[1].c_str(), nullptr, 10);
                            p.at = std::strtoull(f[2].c_str(), nullptr, 10), p.original = f[3], p.file = widen(f[4]);
                            p.length = std::strtoull(f[5].c_str(), nullptr, 10);
                            plan[lower(p.archive)].push_back(p);
                        }
                    }
                }
            }
            if (plan_id.empty() || lower(widen(plan_id)) != worn)
                plan.clear(), plan_id.clear();
            // what is patched now
            std::map<std::wstring, std::vector<PatchLine>> current;
            {
                std::ifstream in(state);
                std::string line;
                while (std::getline(in, line))
                {
                    const auto f = split_line(narrow(trim(widen(line))));
                    if (f.size() != 6)
                        continue;
                    PatchLine p;
                    p.archive = widen(f[0]), p.original_size = std::strtoull(f[1].c_str(), nullptr, 10);
                    p.patched_size = std::strtoull(f[2].c_str(), nullptr, 10), p.plan = f[3];
                    p.at = std::strtoull(f[4].c_str(), nullptr, 10), p.original = f[5];
                    current[lower(p.archive)].push_back(p);
                }
            }
            std::vector<PatchLine> done;
            // 1) archives patched now: kept when the plan wants exactly that, otherwise put back
            for (auto &[key, lines] : current)
            {
                const auto path = archives / lines.front().archive;
                const auto wanted = plan.find(key);
                bool same = wanted != plan.end() && wanted->second.size() == lines.size() && lines.front().plan == plan_id;
                if (same)
                    for (const auto &l : lines)
                        same = same && std::any_of(wanted->second.begin(), wanted->second.end(), [&](const PatchLine &w) { return w.at == l.at && w.original == l.original; });
                const auto file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                {
                    // can't open it now: keep the record, try again next time (a missing archive is forgotten)
                    if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
                        done.insert(done.end(), lines.begin(), lines.end()), ++skipped, why = "an archive was in use";
                    if (same)
                        plan.erase(wanted);
                    continue;
                }
                const auto size = file_size(file);
                if (size != lines.front().patched_size)
                {
                    // replaced since (a Steam update or verify): nothing of ours in it any more
                    CloseHandle(file);
                    continue;
                }
                if (same)
                {
                    CloseHandle(file);
                    done.insert(done.end(), lines.begin(), lines.end());
                    plan.erase(wanted);
                    ++kept;
                    continue;
                }
                bool ok = true;
                for (const auto &l : lines)
                {
                    unsigned char record[16];
                    ok = ok && from_hex(l.original, record) && file_at(file, l.at, record, 16, true);
                }
                LARGE_INTEGER end{};
                end.QuadPart = static_cast<LONGLONG>(lines.front().original_size);
                ok = ok && SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
                FlushFileBuffers(file);
                CloseHandle(file);
                if (ok)
                    ++restored;
                else
                    done.insert(done.end(), lines.begin(), lines.end()), ++skipped, why = "an archive couldn't be put back";
            }
            write_patched(state, done);
            // 2) archives the plan wants patched that aren't yet
            for (auto &[key, lines] : plan)
            {
                const auto path = archives / lines.front().archive;
                // (not Ripper Roo's patched copy while it's in: see level patches)
                if (patched_copy(path))
                {
                    ++skipped, why = "Ripper Roo's patched archive is in (/roo_nitro off)";
                    continue;
                }
                const auto file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                {
                    ++skipped, why = "an archive couldn't be opened";
                    continue;
                }
                // as the client saw it: its size, and every record still the original
                bool ok = file_size(file) == lines.front().original_size;
                std::vector<std::vector<char>> data;
                for (const auto &l : lines)
                {
                    unsigned char want[16], now[16];
                    ok = ok && from_hex(l.original, want) && file_at(file, l.at, now, 16, false) && !std::memcmp(want, now, 16);
                    std::ifstream in(std::filesystem::path(l.file), std::ios::binary);
                    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    ok = ok && bytes.size() == l.length && l.length > 0;
                    data.push_back(std::move(bytes));
                }
                if (!ok)
                {
                    CloseHandle(file);
                    ++skipped, why = "an archive changed since the client looked (connect again to update)";
                    continue;
                }
                // the colored copies at the end, each on a 2 KiB sector
                unsigned long long end = lines.front().original_size;
                std::vector<unsigned long long> where;
                const std::vector<char> zeros(0x800);
                for (std::size_t i = 0; ok && i < lines.size(); ++i)
                {
                    const auto at = (end + 0x7ff) & ~0x7ffull;
                    ok = (at == end || file_at(file, end, const_cast<char *>(zeros.data()), static_cast<DWORD>(at - end), true)) &&
                         file_at(file, at, data[i].data(), static_cast<DWORD>(data[i].size()), true);
                    where.push_back(at);
                    end = at + data[i].size();
                }
                const auto padded = (end + 0x7ff) & ~0x7ffull;
                ok = ok && (padded == end || file_at(file, end, const_cast<char *>(zeros.data()), static_cast<DWORD>(padded - end), true));
                FlushFileBuffers(file);
                if (!ok)
                {
                    // nothing points at the new end yet: cut it off again
                    LARGE_INTEGER cut{};
                    cut.QuadPart = static_cast<LONGLONG>(lines.front().original_size);
                    SetFilePointerEx(file, cut, nullptr, FILE_BEGIN), SetEndOfFile(file);
                    CloseHandle(file);
                    ++skipped, why = "an archive couldn't be written";
                    continue;
                }
                // recorded before the records change, so a crash in between is put back next time
                for (auto &l : lines)
                    l.patched_size = padded;
                done.insert(done.end(), lines.begin(), lines.end());
                write_patched(state, done);
                // each record: where it starts (low 32 bits, bit 32 in the second word's lowest bit), stored whole
                for (std::size_t i = 0; ok && i < lines.size(); ++i)
                {
                    unsigned char record[16];
                    from_hex(lines[i].original, record);
                    const auto low = static_cast<std::uint32_t>(where[i]);
                    std::uint32_t second = 0, length = static_cast<std::uint32_t>(lines[i].length), whole = 0xffffffff;
                    std::memcpy(&second, record + 4, 4);
                    second = (second & ~1u) | static_cast<std::uint32_t>((where[i] >> 32) & 1);
                    std::memcpy(record, &low, 4), std::memcpy(record + 4, &second, 4), std::memcpy(record + 8, &length, 4), std::memcpy(record + 12, &whole, 4);
                    ok = file_at(file, lines[i].at, record, 16, true);
                }
                FlushFileBuffers(file);
                CloseHandle(file);
                if (ok)
                    ++patched;
                else
                    ++skipped, why = "an archive's records couldn't be written (it is put back next time)";
            }
        }
        catch (...)
        {
            why = "unexpected error";
        }
        const auto outcome = std::to_string(patched + kept) + "|" + std::to_string(restored) + "|" + std::to_string(skipped) + "|" + why;
        SetEnvironmentVariableW(L"CRASHNSTAP_COLORS", widen(outcome).c_str());
    }
    // Level patches (Testing, the real Nitro in Ripper Roo's arena): a level's archive swapped for a patched copy the
    // client made (CrashNSTAP\patches\<archive>) before the game opens it, and the game's own put back exactly once it's
    // no longer wanted, by moving files (the game's own to patches\original and back, the copy into its place). The client's plan is %LOCALAPPDATA%\CrashNSTAP\state\level_patches.txt, a line each: "archive
    // (from the archives folder)|its size|its write time (the game's own, as the client saw it)|the copy's size|its write
    // time". patches\installed.txt remembers what's in place ("archive|size|
    // write time in place|the game's own size|write time"), so a Steam update or verify since is noticed and left alone.
    // Copies keep their write times, so the stamps say which file is which. An archive with Crash colors in it is left
    // alone. The result is left in CRASHNSTAP_LEVEL_PATCHES ("in place|put back|skipped|why") for the client.
    struct LevelPatch
    {
        std::wstring archive;
        FileStamp source, patch;
    };
    struct LevelInstalled
    {
        std::wstring archive;
        FileStamp in_place, original;
    };
    bool relative_archive(const std::wstring &path)
    {
        return !path.empty() && path.find(L"..") == std::wstring::npos && path.find(L':') == std::wstring::npos && path[0] != L'\\' && path[0] != L'/' &&
               lower(std::filesystem::path(path).extension().wstring()) == L".pak";
    }
    // `from` moved to `to` (over what's there); a rename, as both are in the game's folder
    bool move_over(const std::filesystem::path &from, const std::filesystem::path &to)
    {
        std::error_code error;
        std::filesystem::create_directories(to.parent_path(), error);
        return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
    }
    void apply_level_patches(const std::filesystem::path &proxy_folder, const std::filesystem::path &game_folder)
    {
        unsigned placed = 0, restored = 0, skipped = 0;
        std::string why;
        try
        {
            const auto patches = proxy_folder / L"CrashNSTAP" / L"patches";
            const auto kept = patches / L"original";
            const auto record_file = patches / L"installed.txt";
            const auto archives = game_folder / L"archives";
            const auto stamp_of = [](const std::string &size, const std::string &written) {
                return FileStamp{std::strtoull(size.c_str(), nullptr, 10), std::strtoull(written.c_str(), nullptr, 10)};
            };
            std::map<std::wstring, LevelPatch> plan;
            {
                static wchar_t local[32768]{};
                const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, std::size(local));
                if (count && count < std::size(local))
                {
                    std::ifstream in(std::filesystem::path(local) / L"CrashNSTAP" / L"state" / L"level_patches.txt");
                    std::string line;
                    while (std::getline(in, line))
                    {
                        const auto f = split_line(narrow(trim(widen(line))));
                        if (f.size() != 5 || !relative_archive(widen(f[0])))
                            continue;
                        plan[lower(widen(f[0]))] = {widen(f[0]), stamp_of(f[1], f[2]), stamp_of(f[3], f[4])};
                    }
                }
            }
            std::map<std::wstring, LevelInstalled> installed;
            {
                std::ifstream in(record_file);
                std::string line;
                while (std::getline(in, line))
                {
                    const auto f = split_line(narrow(trim(widen(line))));
                    if (f.size() == 5 && relative_archive(widen(f[0])))
                        installed[lower(widen(f[0]))] = {widen(f[0]), stamp_of(f[1], f[2]), stamp_of(f[3], f[4])};
                }
            }
            // the record, on disk before anything is moved (false: it couldn't be written)
            const auto write_record = [&]() -> bool {
                std::error_code error;
                std::filesystem::create_directories(patches, error);
                std::string text;
                for (const auto &[key, i] : installed)
                    text += narrow(i.archive) + "|" + std::to_string(i.in_place.size) + "|" + std::to_string(i.in_place.written) + "|" + std::to_string(i.original.size) + "|" +
                            std::to_string(i.original.written) + "\n";
                auto temporary = record_file;
                temporary += L".tmp";
                const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    return false;
                DWORD done = 0;
                const bool wrote = (text.empty() || (WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &done, nullptr) && done == text.size())) && FlushFileBuffers(file);
                CloseHandle(file);
                return wrote && MoveFileExW(temporary.c_str(), record_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            };
            // 0) the game's own kept aside without a record of it (the record lost): back where it was if the game's file is
            // gone; the record made again if a copy of ours is in its place
            {
                std::vector<std::filesystem::path> aside;
                std::error_code error;
                if (std::filesystem::is_directory(kept, error))
                    for (std::filesystem::recursive_directory_iterator it(kept, error), end; !error && it != end; it.increment(error))
                        if (it->is_regular_file(error))
                            aside.push_back(it->path());
                bool adopted = false;
                for (const auto &backup : aside)
                {
                    std::error_code bad;
                    const auto relative = std::filesystem::relative(backup, kept, bad).wstring();
                    if (bad || !relative_archive(relative) || installed.count(lower(relative)) || patched_copy(backup))
                        continue;
                    const auto game = archives / relative;
                    FileStamp now, saved;
                    if (!stamp(backup, saved))
                        continue;
                    if (!stamp(game, now))
                    {
                        if (move_over(backup, game))
                            ++restored;
                    }
                    else if (patched_copy(game))
                        installed[lower(relative)] = {relative, now, saved}, adopted = true;
                }
                if (adopted)
                    write_record();
            }
            if (plan.empty() && installed.empty())
                return;
            // (archives with Crash colors in them: left alone)
            std::set<std::wstring> colored;
            {
                std::ifstream in(proxy_folder / L"CrashNSTAP" / L"skins" / L"patched.txt");
                std::string line;
                while (std::getline(in, line))
                    if (const auto f = split_line(narrow(trim(widen(line)))); f.size() == 6)
                        colored.insert(lower(widen(f[0])));
            }
            // 1) ours in place now: kept when the plan wants that very copy, otherwise the game's own put back. Ours: the
            // one recorded, or any patched copy (changed since, by something else)
            for (auto it = installed.begin(); it != installed.end();)
            {
                const auto game = archives / it->second.archive, backup = kept / it->second.archive;
                FileStamp now, saved;
                const bool there = stamp(game, now), kept_own = stamp(backup, saved) && saved == it->second.original && !patched_copy(backup);
                const bool ours = there && (now == it->second.in_place || patched_copy(game));
                if (there && !ours)
                {
                    // the game's own there again (a Steam update or verify): nothing of ours in place any more
                    it = installed.erase(it);
                    continue;
                }
                if (ours && !(now == it->second.in_place))
                    it->second.in_place = now;
                if (ours)
                    if (const auto want = plan.find(it->first); want != plan.end() && want->second.patch == it->second.in_place)
                    {
                        ++placed;
                        plan.erase(want);
                        ++it;
                        continue;
                    }
                // (also when the game stopped between the two moves: the game's own back where it was)
                if (!kept_own)
                {
                    if (there)
                        ++skipped, why = "the game's own " + narrow(it->second.archive) + " is missing (Steam's verify of the game files puts it back)";
                    it = there ? std::next(it) : installed.erase(it);
                    continue;
                }
                if (!move_over(backup, game))
                {
                    ++skipped, why = narrow(it->second.archive) + " couldn't be put back (Windows error " + std::to_string(GetLastError()) + ")";
                    ++it;
                    continue;
                }
                ++restored;
                it = installed.erase(it);
            }
            write_record();
            // 2) wanted and not in place yet: the game's own kept aside, then the copy put in
            for (const auto &[key, p] : plan)
            {
                const auto game = archives / p.archive, patch = patches / p.archive, backup = kept / p.archive;
                FileStamp now, copy;
                if (colored.count(key))
                {
                    ++skipped, why = narrow(p.archive) + " has Crash colors in it";
                    continue;
                }
                if (!stamp(game, now) || !(now == p.source))
                {
                    ++skipped, why = narrow(p.archive) + " changed since the client made its copy (make it again)";
                    continue;
                }
                // (never a copy taken for the game's own, nor put aside over it)
                if (patched_copy(game))
                {
                    ++skipped, why = "the " + narrow(p.archive) + " in place is a patched copy, not the game's own (Steam's verify of the game files puts it back)";
                    continue;
                }
                if (!stamp(patch, copy) || !(copy == p.patch))
                {
                    ++skipped, why = "the patched " + narrow(p.archive) + " is missing or changed (make it again)";
                    continue;
                }
                // recorded first: if the game stops in between, the next start sees what's where
                installed[key] = {p.archive, p.patch, p.source};
                if (!write_record())
                {
                    ++skipped, why = "couldn't write " + narrow(record_file.wstring()) + " (Windows error " + std::to_string(GetLastError()) + ")";
                    installed.erase(key);
                    continue;
                }
                if (!move_over(game, backup))
                {
                    ++skipped, why = "the game's own " + narrow(p.archive) + " couldn't be kept (Windows error " + std::to_string(GetLastError()) + ")";
                    installed.erase(key), write_record();
                    continue;
                }
                FileStamp after;
                if (!move_over(patch, game) || !stamp(game, after) || !(after == p.patch))
                {
                    ++skipped, why = "the patched " + narrow(p.archive) + " couldn't be put in (Windows error " + std::to_string(GetLastError()) + ")";
                    if (!stamp(game, after) && move_over(backup, game))
                        installed.erase(key), write_record();
                    continue;
                }
                ++placed;
            }
        }
        catch (...)
        {
            why = "unexpected error";
        }
        if (!placed && !restored && !skipped && why.empty())
            return;
        const auto outcome = std::to_string(placed) + "|" + std::to_string(restored) + "|" + std::to_string(skipped) + "|" + why;
        SetEnvironmentVariableW(L"CRASHNSTAP_LEVEL_PATCHES", widen(outcome).c_str());
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
            {
                const auto worn = apply_skin(std::filesystem::path(proxy).parent_path(), std::filesystem::path(host).parent_path());
                apply_color_patches(std::filesystem::path(proxy).parent_path(), std::filesystem::path(host).parent_path(), worn);
                apply_level_patches(std::filesystem::path(proxy).parent_path(), std::filesystem::path(host).parent_path());
            }
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
