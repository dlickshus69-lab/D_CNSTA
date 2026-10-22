#include "ingame_protocol.h"
#include "game_runtime.h"
#include "console.h"
#include "game_font.h"
#include "game_memory.h"
#include "trap_rules.h"
#include "game_sound.h"
#include <d3d11.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <xinput.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <climits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <vector>

namespace
{
    using namespace ingame;
    HMODULE self;
    HANDLE mapping = nullptr, bridge_mutex = nullptr;
    Shared *shared = nullptr;
    std::unique_ptr<Shared> snapshot;
    std::atomic_bool active{false}, console_open{false};
    std::atomic_bool worker_running{false};
    std::atomic_bool startup_mode{false}, startup_check{false};
    // input trap end times (GetTickCount64), copied from the shared block each tick
    std::atomic<std::uint64_t> reverse_until{0}, freeze_until{0}, slippery_until{0}, slow_until{0};
    std::atomic<float> walk_tilt{default_slow_tilt};
    // the pause menu is on screen: no trap touches the controls (checked by the renderer, which keeps running)
    std::atomic_bool menu_open{false};
    // Darkness does nothing in gimmick stages
    std::atomic_bool darkness_exempt{false};
    bool frozen() { return active && !menu_open && timer_active(freeze_until, GetTickCount64()); }
    bool reversed() { return active && !menu_open && timer_active(reverse_until, GetTickCount64()); }
    // the stick-momentum Slippery: only while the game's own ice can't be used
    bool slippery() { return active && !menu_open && !real_ice_active() && timer_active(slippery_until, GetTickCount64()); }
    bool slowed() { return active && !menu_open && timer_active(slow_until, GetTickCount64()); }
    // Slippery momentum per XInput pad and for keyboard scan codes / virtual keys
    std::mutex momentum_mutex;
    StickMomentum stick_momentum[4];
    KeyMomentum scan_momentum, virtual_momentum;
    void clear_input_traps() { reverse_until = freeze_until = slippery_until = slow_until = 0; }
    HANDLE worker_thread = nullptr;
    std::recursive_mutex render_mutex;
    HWND game_window = nullptr;
    WNDPROC original_window = nullptr;
    using Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
    using CreateDevice = decltype(&D3D11CreateDeviceAndSwapChain);
    CreateDevice original_create = nullptr;
    void **create_slot = nullptr;
    struct PresentHook
    {
        IDXGISwapChain *chain = nullptr;
        void **original_table = nullptr;
        std::array<void *, 41> table{};
        Present original = nullptr;
    };
    // keep private tables/chains for late calls. NST normally creates one.
    // cap at eight chains
    std::vector<std::unique_ptr<PresentHook>> present_hooks;
    ConsoleInput input;
    std::string tab_prefix;
    std::vector<std::string> history;
    unsigned history_index = 0, tab_index = 0, scroll = 0;
    bool tab_active = false;
    PopupQueue popups;
    // the latest popups, shown again while F2 is held
    std::deque<Line> popup_history;
    std::atomic_bool history_held{false};
    std::uint64_t popup_seen = 0;
    bool discard_popup_history = false;
    bool secret_input(const std::string &text) { return text.rfind("/password", 0) == 0 || text.rfind("!admin login", 0) == 0; }
    struct InputHook
    {
        void **slot;
        void *original;
        void *replacement;
    };
    std::vector<InputHook> input_hooks;
    std::mutex input_mutex;
    bool replace_slot(void **at, void *expected, void *replacement);
    using BinkOpen = void *(WINAPI *)(const char *, unsigned);
    using BinkFrame = int(WINAPI *)(void *);
    using BinkAsync = int(WINAPI *)(void *, unsigned, unsigned);
    using BinkClose = void(WINAPI *)(void *);
    using BinkGoto = void(WINAPI *)(void *, unsigned, unsigned);
    BinkOpen original_movie_open = nullptr;
    BinkFrame original_movie_frame = nullptr;
    BinkAsync original_movie_async = nullptr;
    BinkClose original_movie_close = nullptr;
    BinkGoto movie_goto = nullptr;
    std::vector<InputHook> movie_hooks;
    std::atomic_bool movie_skip_enabled{false};
    std::atomic<void *> skip_movie{nullptr};
    bool activision_movie(const char *name)
    {
        std::string path;
        for (unsigned i = 0; name && i < 512; ++i)
        {
            char c = 0;
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), name + i, &c, 1, &got) || got != 1)
                return false;
            if (!c)
            {
                const auto slash = path.find_last_of("/\\");
                return !lstrcmpiA(path.c_str() + (slash == std::string::npos ? 0 : slash + 1), "bumper_atvi.bk2");
            }
            path += c;
        }
        return false;
    }
    void *WINAPI movie_open(const char *name, unsigned flags)
    {
        const auto handle = original_movie_open(name, flags);
        if (handle && movie_skip_enabled && activision_movie(name))
            skip_movie = handle;
        return handle;
    }
    void skip_before_decode(void *handle)
    {
        auto pending = handle;
        if (!handle || !movie_skip_enabled || !skip_movie.compare_exchange_strong(pending, nullptr))
            return;
        unsigned frames = 0;
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), static_cast<char *>(handle) + 8, &frames, 4, &got) && got == 4 && frames && frames <= INT_MAX)
        {
            movie_goto(handle, frames, 2);
            std::cout << "Activision startup movie skipped.\n";
        }
    }
    int WINAPI movie_frame(void *handle)
    {
        skip_before_decode(handle);
        return original_movie_frame(handle);
    }
    int WINAPI movie_async(void *handle, unsigned first, unsigned second)
    {
        skip_before_decode(handle);
        return original_movie_async(handle, first, second);
    }
    void WINAPI movie_close(void *handle)
    {
        auto pending = handle;
        skip_movie.compare_exchange_strong(pending, nullptr);
        original_movie_close(handle);
    }
    bool install_movie_skip()
    {
        std::lock_guard<std::recursive_mutex> guard(render_mutex);
        if (movie_hooks.empty())
        {
            const auto bink = GetModuleHandleW(L"bink2w64.dll");
            if (!bink)
                return false;
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            struct Import
            {
                unsigned rva;
                const char *name;
                void *replacement;
            };
            // these are NST import-table slots, not Bink function offsets. verify each resolved export before replacing it.
            const Import imports[]{{0x13820e8, "BinkOpen", reinterpret_cast<void *>(movie_open)},
                                   {0x13820e0, "BinkDoFrame", reinterpret_cast<void *>(movie_frame)},
                                   {0x1382138, "BinkDoFrameAsync", reinterpret_cast<void *>(movie_async)},
                                   {0x13820d8, "BinkClose", reinterpret_cast<void *>(movie_close)}};
            std::vector<InputHook> verified;
            for (const auto &import : imports)
            {
                const auto slot = reinterpret_cast<void **>(base + import.rva);
                const auto original = reinterpret_cast<void *>(GetProcAddress(bink, import.name));
                void *current = nullptr;
                SIZE_T got = 0;
                if (!original || !ReadProcessMemory(GetCurrentProcess(), slot, &current, sizeof(current), &got) || got != sizeof(current) || current != original)
                    return false;
                verified.push_back({slot, original, import.replacement});
            }
            movie_goto = reinterpret_cast<BinkGoto>(GetProcAddress(bink, "BinkGoto"));
            if (!movie_goto)
                return false;
            original_movie_open = reinterpret_cast<BinkOpen>(verified[0].original);
            original_movie_frame = reinterpret_cast<BinkFrame>(verified[1].original);
            original_movie_async = reinterpret_cast<BinkAsync>(verified[2].original);
            original_movie_close = reinterpret_cast<BinkClose>(verified[3].original);
            movie_hooks = std::move(verified);
        }
        for (const auto &hook : movie_hooks)
            if (*hook.slot != hook.replacement && !replace_slot(hook.slot, hook.original, hook.replacement))
            {
                movie_skip_enabled = false;
                skip_movie = nullptr;
                for (const auto &installed : movie_hooks)
                    replace_slot(installed.slot, installed.replacement, installed.original);
                return false;
            }
        movie_skip_enabled = true;
        return true;
    }
    void *input_original(void **slot)
    {
        std::lock_guard<std::mutex> guard(input_mutex);
        for (const auto &hook : input_hooks)
            if (hook.slot == slot)
                return hook.original;
        return nullptr;
    }
    bool keyboard_device(IUnknown *device)
    {
        // buffered mouse offsets overlap keyboard scan codes, so only keyboards get Reverse Controls
        static std::mutex mutex;
        static std::map<IUnknown *, bool> known;
        std::lock_guard<std::mutex> guard(mutex);
        const auto found = known.find(device);
        if (found != known.end())
            return found->second;
        // IDirectInputDevice8 vtable slot 3 is GetCapabilities in both ANSI and Unicode interfaces
        DIDEVCAPS caps{};
        caps.dwSize = sizeof(caps);
        const auto get_caps = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IUnknown *, DIDEVCAPS *)>((*reinterpret_cast<void ***>(device))[3]);
        const bool keyboard = SUCCEEDED(get_caps(device, &caps)) && GET_DIDEVICE_TYPE(caps.dwDevType) == DI8DEVTYPE_KEYBOARD;
        return known[device] = keyboard;
    }
    HRESULT STDMETHODCALLTYPE device_state(IUnknown *device, DWORD bytes, void *data)
    {
        // IDirectInputDevice8 vtable slots 9/10 are GetDeviceState/GetDeviceData in both ANSI and Unicode interfaces
        const auto original = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IUnknown *, DWORD, void *)>(input_original(*reinterpret_cast<void ***>(device) + 9));
        if (!original)
            return DIERR_GENERIC;
        const auto result = original(device, bytes, data);
        if (active && (console_open || frozen()) && SUCCEEDED(result) && data && (bytes == 256 || bytes == sizeof(DIMOUSESTATE) || bytes == sizeof(DIMOUSESTATE2)))
            std::memset(data, 0, bytes);
        else if (SUCCEEDED(result) && data && bytes == 256)
        {
            auto keys = static_cast<unsigned char *>(data);
            if (reversed())
                reverse_keyboard(keys);
            const auto now = GetTickCount64();
            if (slippery())
            {
                std::lock_guard<std::mutex> guard(momentum_mutex);
                for (const auto code : direction_scan_codes)
                    keys[code] = scan_momentum.apply(code, keys[code] & 0x80, now) ? 0x80 : 0;
            }
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE device_data(IUnknown *device, DWORD bytes, DIDEVICEOBJECTDATA *data, DWORD *count, DWORD flags)
    {
        const auto original = reinterpret_cast<HRESULT(STDMETHODCALLTYPE *)(IUnknown *, DWORD, DIDEVICEOBJECTDATA *, DWORD *, DWORD)>(input_original(*reinterpret_cast<void ***>(device) + 10));
        if (!original)
            return DIERR_GENERIC;
        const bool capture = active && (console_open || frozen());
        const auto result = original(device, bytes, data, count, capture ? 0 : flags);
        if (capture && SUCCEEDED(result) && count)
            *count = 0;
        else if (reversed() && SUCCEEDED(result) && data && count && bytes >= sizeof(DWORD) && keyboard_device(device))
            for (DWORD i = 0; i < *count; ++i)
            {
                // dwOfs is the first field and holds the scan code for keyboard events
                auto &offset = reinterpret_cast<DIDEVICEOBJECTDATA *>(reinterpret_cast<unsigned char *>(data) + i * bytes)->dwOfs;
                offset = reverse_scan_code(offset);
            }
        return result;
    }
    SHORT WINAPI async_key(int key)
    {
        if (active && (console_open || frozen()))
            return 0;
        auto state = GetAsyncKeyState(reversed() ? reverse_virtual_key(key) : key);
        if (direction_virtual_key(key))
        {
            const auto now = GetTickCount64();
            if (slippery())
            {
                std::lock_guard<std::mutex> guard(momentum_mutex);
                if (virtual_momentum.apply(static_cast<unsigned>(key), state & 0x8000, now))
                    state = static_cast<SHORT>(state | 0x8000);
            }
        }
        return state;
    }
    // last controller poll with no movement input, and the latest pad values for the Slow log
    std::atomic<std::uint64_t> neutral_at{0};
    std::atomic<int> pad_in_x{0}, pad_in_y{0}, pad_in_buttons{0}, pad_out_x{0}, pad_out_y{0};
    void apply_pad_traps(DWORD index, XINPUT_STATE *state)
    {
        auto &pad = state->Gamepad;
        const auto now = GetTickCount64();
        if (std::hypot(float(pad.sThumbLX), float(pad.sThumbLY)) <= stick_deadzone && !(pad.wButtons & 0xF))
            neutral_at = now;
        pad_in_x = pad.sThumbLX, pad_in_y = pad.sThumbLY, pad_in_buttons = pad.wButtons;
        if (reversed())
        {
            pad.sThumbLX = reverse_axis(pad.sThumbLX);
            pad.sThumbLY = reverse_axis(pad.sThumbLY);
            pad.wButtons = reverse_dpad(pad.wButtons);
        }
        if (index < 4)
        {
            std::lock_guard<std::mutex> guard(momentum_mutex);
            if (slippery())
            {
                std::uint16_t buttons = pad.wButtons;
                stick_momentum[index].apply(pad.sThumbLX, pad.sThumbLY, buttons, now, walk_tilt);
                pad.wButtons = buttons;
            }
            else
                stick_momentum[index].primed = false;
        }
        if (slowed())
        {
            std::uint16_t buttons = pad.wButtons;
            walk_stick(pad.sThumbLX, pad.sThumbLY, buttons, walk_tilt);
            pad.wButtons = buttons;
        }
        pad_out_x = pad.sThumbLX, pad_out_y = pad.sThumbLY;
    }
    template <DWORD(WINAPI *Self)(DWORD, XINPUT_STATE *)>
    DWORD pad_query(DWORD index, XINPUT_STATE *state)
    {
        using Query = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
        Query query = nullptr;
        {
            std::lock_guard<std::mutex> guard(input_mutex);
            for (const auto &hook : input_hooks)
                if (hook.replacement == reinterpret_cast<void *>(Self))
                {
                    query = reinterpret_cast<Query>(hook.original);
                    break;
                }
        }
        if (!query)
            return ERROR_DEVICE_NOT_CONNECTED;
        const auto result = query(index, state);
        if (!active || console_open || frozen())
            neutral_at = GetTickCount64(); // the game gets no movement from the pad now
        if (active && (console_open || frozen()) && result == ERROR_SUCCESS && state)
            std::memset(&state->Gamepad, 0, sizeof(state->Gamepad));
        else if (result == ERROR_SUCCESS && state)
            apply_pad_traps(index, state);
        return result;
    }
    DWORD WINAPI controller_state(DWORD index, XINPUT_STATE *state);
    // XInputGetStateEx (xinput ordinal 100) returns the same state plus the guide button
    DWORD WINAPI controller_state_ex(DWORD index, XINPUT_STATE *state);
    DWORD WINAPI controller_state(DWORD index, XINPUT_STATE *state) { return pad_query<controller_state>(index, state); }
    DWORD WINAPI controller_state_ex(DWORD index, XINPUT_STATE *state) { return pad_query<controller_state_ex>(index, state); }
    void add_input_hook(void **slot, void *replacement)
    {
        std::lock_guard<std::mutex> guard(input_mutex);
        for (const auto &hook : input_hooks)
            if (hook.slot == slot)
            {
                replace_slot(slot, hook.original, hook.replacement);
                return;
            }
        input_hooks.push_back({slot, *slot, replacement});
        if (!replace_slot(slot, *slot, replacement))
            input_hooks.pop_back();
    }
    void install_input_hooks()
    {
        // hook both dInput interface variants
        IDirectInput8A *ansi = nullptr;
        IDirectInput8W *unicode = nullptr;
        if (SUCCEEDED(DirectInput8Create(self, DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void **>(&ansi), nullptr)))
        {
            for (const auto &guid : {GUID_SysKeyboard, GUID_SysMouse})
            {
                IDirectInputDevice8A *device = nullptr;
                if (SUCCEEDED(ansi->CreateDevice(guid, &device, nullptr)))
                {
                    auto v = *reinterpret_cast<void ***>(device);
                    add_input_hook(v + 9, reinterpret_cast<void *>(device_state));
                    add_input_hook(v + 10, reinterpret_cast<void *>(device_data));
                    device->Release();
                }
            }
            ansi->Release();
        }
        if (SUCCEEDED(DirectInput8Create(self, DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&unicode), nullptr)))
        {
            for (const auto &guid : {GUID_SysKeyboard, GUID_SysMouse})
            {
                IDirectInputDevice8W *device = nullptr;
                if (SUCCEEDED(unicode->CreateDevice(guid, &device, nullptr)))
                {
                    auto v = *reinterpret_cast<void ***>(device);
                    add_input_hook(v + 9, reinterpret_cast<void *>(device_state));
                    add_input_hook(v + 10, reinterpret_cast<void *>(device_data));
                    device->Release();
                }
            }
            unicode->Release();
        }
        // read NST's import table so input suppression affects its key/controller calls without patching system functions
        const auto base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        const auto pe = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        const auto directory = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress || directory.VirtualAddress + directory.Size > pe->OptionalHeader.SizeOfImage)
            return;
        const auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + directory.VirtualAddress);
        std::string hooked;
        for (unsigned i = 0; i < directory.Size / sizeof(*imports) && imports[i].Name; ++i)
        {
            if (!imports[i].OriginalFirstThunk || !imports[i].FirstThunk)
                continue;
            std::string library = reinterpret_cast<const char *>(base + imports[i].Name);
            std::transform(library.begin(), library.end(), library.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + imports[i].OriginalFirstThunk);
            auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64 *>(base + imports[i].FirstThunk);
            for (unsigned j = 0; j < 4096 && names[j].u1.AddressOfData; ++j)
            {
                const auto slot = reinterpret_cast<void **>(&addresses[j].u1.Function);
                if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal))
                {
                    if (library.rfind("xinput", 0) == 0 && IMAGE_ORDINAL64(names[j].u1.Ordinal) == 100)
                    {
                        add_input_hook(slot, reinterpret_cast<void *>(controller_state_ex));
                        hooked += " XInputGetStateEx(" + library + ")";
                    }
                    continue;
                }
                const auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names[j].u1.AddressOfData)->Name;
                if (!std::strcmp(name, "GetAsyncKeyState"))
                    add_input_hook(slot, reinterpret_cast<void *>(async_key));
                if (!std::strcmp(name, "XInputGetState") || !std::strcmp(name, "XInputGetStateEx"))
                {
                    add_input_hook(slot, reinterpret_cast<void *>(std::strcmp(name, "XInputGetState") ? controller_state_ex : controller_state));
                    hooked += std::string(" ") + name + "(" + library + ")";
                }
                if (library.rfind("xinput", 0) == 0 && std::strcmp(name, "XInputGetState") && std::strcmp(name, "XInputGetStateEx"))
                    hooked += std::string(" [not hooked: ") + name + "]";
            }
        }
        std::cout << "Controller input hooks:" << (hooked.empty() ? " none found" : hooked) << "\n" << std::flush;
        // the game's own sounds (the TNT countdown for the Nitro Trap), through NST's FMOD imports
        install_sound_hooks();
    }

    bool bridge_lock(DWORD wait = 0)
    {
        const auto result = WaitForSingleObject(bridge_mutex, wait);
        return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    void error(const std::string &text)
    {
        if (shared && bridge_lock())
        {
            copy(shared->error, text);
            ReleaseMutex(bridge_mutex);
        }
    }
    void command(const std::string &text)
    {
        if (text.empty() || text.size() > 1023 || !bridge_lock())
            return;
        if (valid(*shared) && shared->command_write - shared->command_read < max_commands)
            copy(shared->commands[shared->command_write++ % max_commands], text);
        else
            copy(shared->error, "Console command queue is full");
        ReleaseMutex(bridge_mutex);
    }
    void toggle_console()
    {
        console_open = !console_open.load();
        if (console_open)
        {
            popups.clear();
            discard_popup_history = true;
        }
        if (bridge_lock())
        {
            shared->console_open = console_open;
            if (console_open)
            {
                popup_seen = shared->line_write;
                discard_popup_history = false;
            }
            ReleaseMutex(bridge_mutex);
        }
    }
    std::wstring wide(const std::string &text)
    {
        const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(size, 0);
        if (size)
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }
    std::string utf8(wchar_t c)
    {
        char bytes[4]{};
        const auto size = WideCharToMultiByte(CP_UTF8, 0, &c, 1, bytes, 4, nullptr, nullptr);
        return std::string(bytes, size);
    }
    bool copy_selection(HWND window)
    {
        const auto text = wide(input.selected());
        if (text.empty() || !OpenClipboard(window))
            return false;
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        bool copied = false;
        if (memory)
        {
            if (const auto at = GlobalLock(memory))
            {
                std::memcpy(at, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(memory);
                if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory))
                    copied = true;
            }
            if (!copied)
                GlobalFree(memory);
        }
        CloseClipboard();
        return copied;
    }
    std::string clipboard_text(HWND window)
    {
        if (!OpenClipboard(window))
            return {};
        std::string result;
        if (const auto memory = GetClipboardData(CF_UNICODETEXT))
        {
            const auto capacity = std::min<std::size_t>(GlobalSize(memory) / sizeof(wchar_t), 4096);
            if (const auto at = static_cast<const wchar_t *>(GlobalLock(memory)))
            {
                std::size_t length = 0;
                while (length < capacity && at[length])
                    ++length;
                if (length && length == capacity && at[length - 1] >= 0xd800 && at[length - 1] <= 0xdbff)
                    --length;
                const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, at, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
                if (size)
                {
                    result.resize(size);
                    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, at, static_cast<int>(length), result.data(), size, nullptr, nullptr);
                }
                GlobalUnlock(memory);
            }
        }
        CloseClipboard();
        return result;
    }
    LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
    {
        if (!active)
            return CallWindowProcW(original_window, window, message, wp, lp);
        std::lock_guard<std::recursive_mutex> guard(render_mutex);
        if (startup_mode && message == WM_CLOSE)
        {
            command("/quit");
            return 0;
        }
        if (message == WM_KILLFOCUS && console_open && !startup_mode)
            toggle_console();
        if (message == WM_KILLFOCUS)
            history_held = false;
        // hold F2: the latest popups again (items found and received, traps, ...)
        if (wp == VK_F2 && (message == WM_KEYDOWN || message == WM_KEYUP))
        {
            history_held = message == WM_KEYDOWN;
            return 0;
        }
        // F10 is a system key. Consume releases and repeats too, so Windows is stopped here from entering menu mode
        if (wp == VK_F10 && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP))
        {
            if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lp & (1LL << 30)))
                command("/crashsanity toggle");
            return 0;
        }
        if (message == WM_KEYDOWN && !(lp & (1LL << 30)))
        {
            if (wp == VK_F1)
            {
                toggle_console();
                return 0;
            }
            if (wp == VK_F7)
            {
                command("/markers toggle");
                return 0;
            }
        }
        if (console_open)
        {
            if (message == WM_KEYDOWN)
            {
                if (wp == VK_F1)
                    return 0;
                if (wp == VK_ESCAPE)
                {
                    toggle_console();
                    return 0;
                }
                const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (ctrl && wp == 'C')
                {
                    copy_selection(window);
                    return 0;
                }
                if (ctrl && wp == 'V')
                {
                    input.insert(clipboard_text(window));
                    tab_prefix.clear();
                    tab_active = false;
                    return 0;
                }
                if (ctrl && wp == 'A')
                {
                    input.anchor = 0;
                    input.cursor = input.text.size();
                    return 0;
                }
                if (wp == VK_RETURN)
                {
                    if (input.text == "/clear")
                    {
                        if (bridge_lock())
                        {
                            clear_console_lines(*shared);
                            ReleaseMutex(bridge_mutex);
                        }
                        popups.clear();
                        command(input.text);
                    }
                    else
                    {
                        command(input.text);
                        if (!input.text.empty() && !secret_input(input.text))
                        {
                            history.push_back(input.text);
                            if (history.size() > 100)
                                history.erase(history.begin());
                        }
                    }
                    input.set("");
                    tab_prefix.clear();
                    tab_active = false;
                    history_index = static_cast<unsigned>(history.size());
                    scroll = 0;
                }
                else if (wp == VK_BACK || wp == VK_DELETE)
                {
                    input.erase(wp == VK_BACK);
                    tab_prefix.clear();
                    tab_active = false;
                }
                else if (wp == VK_LEFT || wp == VK_RIGHT)
                {
                    input.move(wp == VK_LEFT ? -1 : 1, shift);
                    tab_prefix.clear();
                    tab_active = false;
                }
                else if (wp == VK_HOME || wp == VK_END)
                {
                    input.edge(wp == VK_END, shift);
                    tab_prefix.clear();
                    tab_active = false;
                }
                else if (wp == VK_UP)
                {
                    if (history_index > 0)
                        input.set(history[--history_index]);
                    tab_prefix.clear();
                    tab_active = false;
                }
                else if (wp == VK_DOWN)
                {
                    if (history_index < history.size())
                        ++history_index;
                    input.set(history_index < history.size() ? history[history_index] : "");
                    tab_prefix.clear();
                    tab_active = false;
                }
                else if (wp == VK_PRIOR)
                    scroll = std::min(scroll + 8, snapshot->line_count ? snapshot->line_count - 1 : 0);
                else if (wp == VK_NEXT)
                    scroll = scroll > 8 ? scroll - 8 : 0;
                else if (wp == VK_TAB && bridge_lock())
                {
                    if (!tab_active)
                    {
                        tab_prefix = input.text;
                        tab_index = 0;
                        tab_active = true;
                    }
                    const auto matches = complete(tab_prefix, commands(), unpack_names(*shared, shared->item_start, shared->location_start),
                                                  unpack_names(*shared, shared->location_start, shared->completion_bytes));
                    if (!matches.empty())
                        input.set(matches[tab_index++ % matches.size()]);
                    ReleaseMutex(bridge_mutex);
                }
                return 0;
            }
            if (message == WM_CHAR)
            {
                if (wp >= 32 && wp != 127)
                {
                    input.insert(utf8(static_cast<wchar_t>(wp)));
                    tab_prefix.clear();
                    tab_active = false;
                }
                return 0;
            }
            if (message == WM_MOUSEWHEEL)
            {
                if (GET_WHEEL_DELTA_WPARAM(wp) > 0)
                    scroll = std::min(scroll + 3, snapshot->line_count ? snapshot->line_count - 1 : 0);
                else
                    scroll = scroll > 3 ? scroll - 3 : 0;
                return 0;
            }
            if (message == WM_INPUT || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
                (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST))
                return 0;
        }
        return CallWindowProcW(original_window, window, message, wp, lp);
    }

    template <class T>
    void release(T *&p)
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }
    void checked(HRESULT value, const char *message)
    {
        if (FAILED(value))
            throw std::runtime_error(message);
    }
    struct Vertex
    {
        float x, y, u, v;
        unsigned color;
    };
    struct Glyph
    {
        float u, v, width;
        float x = 0, y = 0;
    };
    const GameFont *installed_font()
    {
        static const auto font = []() -> std::unique_ptr<GameFont>
        {
            std::vector<wchar_t> exe(32768);
            const auto length = GetModuleFileNameW(nullptr, exe.data(), exe.size());
            if (!length || length >= exe.size())
                return nullptr;
            const auto path = std::filesystem::path(exe.data()).parent_path() / L"archives" / L"permanent.pak";
            if (!std::filesystem::exists(path))
                return nullptr;
            try
            {
                auto result = std::make_unique<GameFont>(load_game_font(path));
                std::cout << "AP text: installed NST Zoinks font loaded (" << result->glyphs.size() << " glyphs)." << std::endl;
                return result;
            }
            catch (const std::exception &e)
            {
                std::cout << "AP text font fallback: " << e.what() << std::endl;
                return nullptr;
            }
        }();
        return font.get();
    }
    struct Renderer
    {
        ID3D11Device *device = nullptr;
        ID3D11DeviceContext *context = nullptr;
        ID3D11VertexShader *vs = nullptr;
        ID3D11PixelShader *ps = nullptr;
        ID3D11InputLayout *layout = nullptr;
        ID3D11Buffer *vertices = nullptr;
        ID3D11Texture2D *atlas = nullptr;
        ID3D11ShaderResourceView *srv = nullptr;
        ID3D11SamplerState *sampler = nullptr;
        ID3D11BlendState *blend = nullptr;
        ID3D11DepthStencilState *depth = nullptr;
        ID3D11RasterizerState *raster = nullptr;
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HFONT font = nullptr;
        HGDIOBJ old_bitmap = nullptr, old_font = nullptr;
        std::uint32_t *pixels = nullptr;
        std::map<wchar_t, Glyph> glyphs;
        std::vector<std::uint32_t> texture;
        std::vector<Vertex> draw;
        const GameFont *crash_font = nullptr;
        unsigned width = 0, height = 0, next_glyph = 0;
        bool dirty = false;
        static constexpr unsigned atlas_size = 2048, cell = 32, max_vertices = 200000;
        ~Renderer()
        {
            release(vs);
            release(ps);
            release(layout);
            release(vertices);
            release(srv);
            release(atlas);
            release(sampler);
            release(blend);
            release(depth);
            release(raster);
            release(context);
            release(device);
            if (dc)
            {
                SelectObject(dc, old_font);
                SelectObject(dc, old_bitmap);
                DeleteObject(font);
                DeleteObject(bitmap);
                DeleteDC(dc);
            }
        }
        Renderer() = default;
        explicit Renderer(IDXGISwapChain *chain) : Renderer()
        {
            checked(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&device)), "NST renderer is not Direct3D 11");
            device->GetImmediateContext(&context);
            const char *shader = "Texture2D tex:register(t0);SamplerState samp:register(s0);struct V{float2 p:POSITION;float2 uv:TEXCOORD;float4 c:COLOR;};struct P{float4 p:SV_POSITION;float2 uv:TEXCOORD;float4 c:COLOR;};P VS(V i){P o;o.p=float4(i.p,0,1);o.uv=i.uv;o.c=i.c;return o;}float4 PS(P i):SV_TARGET{return i.c*tex.Sample(samp,i.uv);}";
            ID3DBlob *v = nullptr, *p = nullptr, *errors = nullptr;
            try
            {
                checked(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &v, &errors), "Cannot compile HUD vertex shader");
                release(errors);
                checked(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &p, &errors), "Cannot compile HUD pixel shader");
                release(errors);
                checked(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs), "Cannot create HUD vertex shader");
                checked(device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps), "Cannot create HUD pixel shader");
                const D3D11_INPUT_ELEMENT_DESC fields[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                           {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                           {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
                checked(device->CreateInputLayout(fields, 3, v->GetBufferPointer(), v->GetBufferSize(), &layout), "Cannot create HUD vertex layout");
                release(v);
                release(p);
            }
            catch (...)
            {
                release(v);
                release(p);
                release(errors);
                throw;
            }
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = max_vertices * sizeof(Vertex);
            buffer.Usage = D3D11_USAGE_DYNAMIC;
            buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            checked(device->CreateBuffer(&buffer, nullptr, &vertices), "Cannot create HUD vertices");
            D3D11_TEXTURE2D_DESC td{};
            td.Width = td.Height = atlas_size;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            checked(device->CreateTexture2D(&td, nullptr, &atlas), "Cannot create HUD font atlas");
            checked(device->CreateShaderResourceView(atlas, nullptr, &srv), "Cannot create HUD font texture");
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            checked(device->CreateSamplerState(&sd, &sampler), "Cannot create HUD sampler");
            D3D11_BLEND_DESC bd{};
            auto &rt = bd.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            checked(device->CreateBlendState(&bd, &blend), "Cannot create HUD blend state");
            D3D11_DEPTH_STENCIL_DESC dd{};
            checked(device->CreateDepthStencilState(&dd, &depth), "Cannot create HUD depth state");
            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            checked(device->CreateRasterizerState(&rd, &raster), "Cannot create HUD raster state");
            dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = atlas_size;
            info.bmiHeader.biHeight = -static_cast<LONG>(atlas_size);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);
            font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
            if (!dc || !bitmap || !font)
                throw std::runtime_error("Cannot create HUD font");
            old_bitmap = SelectObject(dc, bitmap);
            old_font = SelectObject(dc, font);
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkMode(dc, OPAQUE);
            crash_font = installed_font();
            texture.resize(atlas_size * atlas_size);
            texture[0] = 0xffffffff;
            dirty = true;
            glyph(L'?');
        }
        Glyph glyph(wchar_t c)
        {
            if (const auto it = glyphs.find(c); it != glyphs.end())
                return it->second;
            if (next_glyph + 1 >= atlas_size / cell * (atlas_size / cell))
                return glyphs.at(L'?');
            const auto slot = ++next_glyph, x = (slot % (atlas_size / cell)) * cell, y = (slot / (atlas_size / cell)) * cell;
            if (crash_font)
            {
                const auto found = crash_font->glyphs.find(c);
                if (found != crash_font->glyphs.end())
                {
                    const auto &source = found->second;
                    const float scale = 20 / crash_font->line_height;
                    const float gw = source.width * scale, gh = source.height * scale;
                    // the HUD uses one 20px font size
                    if (gw <= cell && gh <= cell)
                    {
                        for (unsigned yy = 0; yy < std::ceil(gh); ++yy)
                            for (unsigned xx = 0; xx < std::ceil(gw); ++xx)
                            {
                                // four samples preserve the original outline and alpha at the smaller HUD size.
                                unsigned sums[4]{};
                                for (const auto oy : {0.25f, 0.75f})
                                    for (const auto ox : {0.25f, 0.75f})
                                    {
                                        const auto sx = std::min(crash_font->width - 1, static_cast<unsigned>((source.u + std::min(1.0f, (xx + ox) / gw) * (source.uu - source.u)) * crash_font->width));
                                        const auto sy = std::min(crash_font->height - 1, static_cast<unsigned>((source.v + std::min(1.0f, (yy + oy) / gh) * (source.vv - source.v)) * crash_font->height));
                                        const auto pixel = crash_font->pixels[sy * crash_font->width + sx];
                                        for (unsigned i = 0; i < 4; ++i)
                                            sums[i] += (pixel >> (i * 8)) & 255;
                                    }
                                unsigned pixel = 0;
                                for (unsigned i = 0; i < 4; ++i)
                                    pixel |= ((sums[i] + 2) / 4) << (i * 8);
                                texture[(y + yy) * atlas_size + x + xx] = pixel;
                            }
                        const float advance = std::max(1.0f, (source.left + source.step + source.right) * scale);
                        Glyph g{static_cast<float>(x) / atlas_size, static_cast<float>(y) / atlas_size, advance, (source.x + source.left) * scale, source.y * scale};
                        dirty = true;
                        glyphs[c] = g;
                        return g;
                    }
                }
            }
            SIZE size{};
            GetTextExtentPoint32W(dc, &c, 1, &size);
            RECT r{static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + cell), static_cast<LONG>(y + cell)};
            ExtTextOutW(dc, x, y, ETO_OPAQUE | ETO_CLIPPED, &r, &c, 1, nullptr);
            GdiFlush();
            for (unsigned yy = y; yy < y + cell; ++yy)
                for (unsigned xx = x; xx < x + cell; ++xx)
                {
                    const auto px = pixels[yy * atlas_size + xx];
                    const auto alpha = std::max({px & 255, (px >> 8) & 255, (px >> 16) & 255});
                    texture[yy * atlas_size + xx] = (alpha << 24) | 0xffffff;
                }
            dirty = true;
            Glyph g{static_cast<float>(x) / atlas_size, static_cast<float>(y) / atlas_size, static_cast<float>(std::min<LONG>(size.cx, cell))};
            glyphs[c] = g;
            return g;
        }
        Vertex vertex(float x, float y, float u, float v, unsigned color) { return {x * 2 / width - 1, 1 - y * 2 / height, u, v, color}; }
        void quad(float x, float y, float w, float h, unsigned color, float u = 0.00024f, float v = 0.00024f, float uw = 0, float vh = 0)
        {
            if (draw.size() + 6 > max_vertices)
                return;
            const auto a = vertex(x, y, u, v, color), b = vertex(x + w, y, u + uw, v, color), c = vertex(x + w, y + h, u + uw, v + vh, color), d = vertex(x, y + h, u, v + vh, color);
            draw.insert(draw.end(), {a, b, c, a, c, d});
        }
        void line(float x, float y, float xx, float yy, unsigned color)
        {
            const float dx = xx - x, dy = yy - y, length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.1f || draw.size() + 6 > max_vertices)
                return;
            const float ox = -dy / length, oy = dx / length;
            const auto a = vertex(x + ox, y + oy, 0.00024f, 0.00024f, color), b = vertex(xx + ox, yy + oy, 0.00024f, 0.00024f, color), c = vertex(xx - ox, yy - oy, 0.00024f, 0.00024f, color), d = vertex(x - ox, y - oy, 0.00024f, 0.00024f, color);
            draw.insert(draw.end(), {a, b, c, a, c, d});
        }
        float text(float x, float y, const std::string &value, unsigned color, float limit = 0)
        {
            const auto origin = x;
            for (const auto c : wide(value))
            {
                if (c == L'\n')
                {
                    x = origin;
                    y += 22;
                    continue;
                }
                const auto g = glyph(c);
                if (limit && x + g.width > limit)
                {
                    x = origin;
                    y += 22;
                }
                quad(x + g.x, y + g.y, cell, cell, color, g.u, g.v, static_cast<float>(cell) / atlas_size, static_cast<float>(cell) / atlas_size);
                x += g.width;
            }
            return x;
        }
        // text drawn larger (the glyphs are scaled), for warnings in the middle of the screen
        float big_text(float x, float y, const std::string &value, unsigned color, float scale)
        {
            for (const auto c : wide(value))
            {
                const auto g = glyph(c);
                quad(x + g.x * scale, y + g.y * scale, cell * scale, cell * scale, color, g.u, g.v, static_cast<float>(cell) / atlas_size,
                     static_cast<float>(cell) / atlas_size);
                x += g.width * scale;
            }
            return x;
        }
        float text_width(const std::string &value)
        {
            float width = 0;
            for (const auto c : wide(value))
                width += glyph(c).width;
            return width;
        }
        float rich_line(float x, float y, const Line &value, float right, bool paint = true, float bottom = 100000, float opacity = 1)
        {
            const auto origin = x, top = y;
            for (unsigned i = 0; i < value.count; ++i)
                for (const auto c : wide(value.spans[i].text))
                {
                    const auto g = glyph(c);
                    if (c == L'\n' || x + g.width > right)
                    {
                        x = origin;
                        y += 22;
                        if (c == L'\n')
                            continue;
                    }
                    if (paint && y + 22 <= bottom)
                        quad(x + g.x, y + g.y, cell, cell, faded_color(value.spans[i].color, opacity), g.u, g.v, static_cast<float>(cell) / atlas_size, static_cast<float>(cell) / atlas_size);
                    x += g.width;
                }
            return y - top + 22;
        }
        void submit(IDXGISwapChain *chain)
        {
            if (draw.empty())
                return;
            ID3D11Texture2D *back = nullptr;
            ID3D11RenderTargetView *target = nullptr;
            checked(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back)), "Cannot read NST back buffer");
            const auto created = device->CreateRenderTargetView(back, nullptr, &target);
            release(back);
            checked(created, "Cannot bind NST HUD back buffer");
            // save every binding changed below, including dynamic shader class instances
            ID3D11RenderTargetView *old_targets[8]{};
            ID3D11DepthStencilView *old_ds = nullptr;
            ID3D11BlendState *old_blend = nullptr;
            FLOAT old_factor[4]{};
            UINT old_mask = 0;
            ID3D11DepthStencilState *old_depth = nullptr;
            UINT old_ref = 0;
            ID3D11RasterizerState *old_raster = nullptr;
            D3D11_VIEWPORT old_viewports[16]{};
            UINT old_count = 16;
            ID3D11InputLayout *old_layout = nullptr;
            ID3D11Buffer *old_buffer = nullptr;
            UINT old_stride = 0, old_offset = 0;
            D3D11_PRIMITIVE_TOPOLOGY topology;
            ID3D11VertexShader *old_vs = nullptr;
            ID3D11PixelShader *old_ps = nullptr;
            ID3D11GeometryShader *old_gs = nullptr;
            ID3D11HullShader *old_hs = nullptr;
            ID3D11DomainShader *old_dsh = nullptr;
            ID3D11ClassInstance *vs_classes[256]{}, *ps_classes[256]{}, *gs_classes[256]{}, *hs_classes[256]{}, *ds_classes[256]{};
            UINT vn = 256, pn = 256, gn = 256, hn = 256, dn = 256;
            ID3D11ShaderResourceView *old_srv = nullptr;
            ID3D11SamplerState *old_sampler = nullptr;
            ID3D11UnorderedAccessView *old_uavs[8]{};
            UINT keep[8];
            std::fill_n(keep, 8, ~0U);
            context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 8, old_uavs);
            context->OMGetRenderTargets(8, old_targets, &old_ds);
            context->OMGetBlendState(&old_blend, old_factor, &old_mask);
            context->OMGetDepthStencilState(&old_depth, &old_ref);
            context->RSGetState(&old_raster);
            context->RSGetViewports(&old_count, old_viewports);
            context->IAGetInputLayout(&old_layout);
            context->IAGetVertexBuffers(0, 1, &old_buffer, &old_stride, &old_offset);
            context->IAGetPrimitiveTopology(&topology);
            context->VSGetShader(&old_vs, vs_classes, &vn);
            context->PSGetShader(&old_ps, ps_classes, &pn);
            context->GSGetShader(&old_gs, gs_classes, &gn);
            context->HSGetShader(&old_hs, hs_classes, &hn);
            context->DSGetShader(&old_dsh, ds_classes, &dn);
            context->PSGetShaderResources(0, 1, &old_srv);
            context->PSGetSamplers(0, 1, &old_sampler);
            UINT rt_count = 0;
            for (UINT i = 0; i < 8; ++i)
                if (old_targets[i])
                    rt_count = i + 1;
            const auto restore = [&]
            {context->OMSetRenderTargetsAndUnorderedAccessViews(rt_count,old_targets,old_ds,rt_count,8-rt_count,old_uavs+rt_count,keep+rt_count);context->OMSetBlendState(old_blend,old_factor,old_mask);context->OMSetDepthStencilState(old_depth,old_ref);
            context->RSSetState(old_raster);context->RSSetViewports(old_count,old_viewports);context->IASetInputLayout(old_layout);context->IASetVertexBuffers(0,1,&old_buffer,&old_stride,&old_offset);context->IASetPrimitiveTopology(topology);
            context->VSSetShader(old_vs,vs_classes,vn);context->PSSetShader(old_ps,ps_classes,pn);context->GSSetShader(old_gs,gs_classes,gn);context->HSSetShader(old_hs,hs_classes,hn);context->DSSetShader(old_dsh,ds_classes,dn);
            context->PSSetShaderResources(0,1,&old_srv);context->PSSetSamplers(0,1,&old_sampler);
            for(auto& p:old_targets)release(p);for(auto& p:old_uavs)release(p);release(old_ds);release(old_blend);release(old_depth);release(old_raster);release(old_layout);release(old_buffer);release(old_vs);release(old_ps);release(old_gs);release(old_hs);release(old_dsh);release(old_srv);release(old_sampler);
            for(UINT i=0;i<vn;++i)release(vs_classes[i]);for(UINT i=0;i<pn;++i)release(ps_classes[i]);for(UINT i=0;i<gn;++i)release(gs_classes[i]);for(UINT i=0;i<hn;++i)release(hs_classes[i]);for(UINT i=0;i<dn;++i)release(ds_classes[i]);release(target); };
            try
            {
                if (dirty)
                {
                    context->UpdateSubresource(atlas, 0, nullptr, texture.data(), atlas_size * 4, 0);
                    dirty = false;
                }
                D3D11_MAPPED_SUBRESOURCE mapped{};
                checked(context->Map(vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Cannot update HUD vertices");
                std::memcpy(mapped.pData, draw.data(), draw.size() * sizeof(Vertex));
                context->Unmap(vertices, 0);
                const UINT stride = sizeof(Vertex), offset = 0;
                D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
                context->OMSetRenderTargets(1, &target, nullptr);
                context->OMSetBlendState(blend, nullptr, 0xffffffff);
                context->OMSetDepthStencilState(depth, 0);
                context->RSSetState(raster);
                context->RSSetViewports(1, &viewport);
                context->IASetInputLayout(layout);
                context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->VSSetShader(vs, nullptr, 0);
                context->PSSetShader(ps, nullptr, 0);
                context->GSSetShader(nullptr, nullptr, 0);
                context->HSSetShader(nullptr, nullptr, 0);
                context->DSSetShader(nullptr, nullptr, 0);
                context->PSSetShaderResources(0, 1, &srv);
                context->PSSetSamplers(0, 1, &sampler);
                context->Draw(static_cast<UINT>(draw.size()), 0);
                restore();
            }
            catch (...)
            {
                restore();
                throw;
            }
        }
    };
    std::unique_ptr<Renderer> renderer;
    bool project(const std::array<float, 16> &m, const std::array<float, 3> &p, float &x, float &y, unsigned width, unsigned height)
    {
        const float w = p[0] * m[3] + p[1] * m[7] + p[2] * m[11] + m[15];
        if (!std::isfinite(w) || w < 0.01f)
            return false;
        x = (p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12]) / w;
        y = (p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13]) / w;
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 2 || std::abs(y) > 2)
            return false;
        x = (x + 1) * width / 2;
        y = (1 - y) * height / 2;
        return true;
    }
    const Level *level(const std::string &key)
    {
        for (unsigned i = 0; i < snapshot->level_count; ++i)
            if (key == snapshot->levels[i].key)
                return snapshot->levels + i;
        return nullptr;
    }
    void frame(IDXGISwapChain *chain)
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow)
            return;
        DWORD pid = 0;
        GetWindowThreadProcessId(desc.OutputWindow, &pid);
        if (pid != GetCurrentProcessId() || !(GetWindowLongPtrW(desc.OutputWindow, GWL_STYLE) & WS_VISIBLE))
            return;
        if (!game_window)
        {
            original_window = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC));
            if (!original_window)
                throw std::runtime_error("NST window procedure is unavailable");
            const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(window_proc)));
            if (!previous)
                throw std::runtime_error("Cannot attach AP console input to NST's window");
            original_window = previous;
            game_window = desc.OutputWindow;
        }
        if (game_window != desc.OutputWindow)
            return;
        {
            // is the pause menu up? checked a few times a second, outside the lock
            static std::uint64_t next_menu_check = 0;
            const auto now = GetTickCount64();
            if (now >= next_menu_check)
            {
                next_menu_check = now + 100;
                try
                {
                    menu_open = !startup_mode && pause_menu_visible();
                    darkness_exempt = !startup_mode && darkness_trap_exempt();
                }
                catch (...)
                {
                    menu_open = false;
                }
            }
        }
        if (bridge_lock())
        {
            if (!valid(*shared) || shared->stop || GetTickCount64() - shared->heartbeat > 15000)
            {
                active = false;
                ReleaseMutex(bridge_mutex);
                return;
            }
            shared->menu_paused = menu_open ? 1u : 0u;
            const auto previous_lines = snapshot->line_write, previous_clear = snapshot->line_clear;
            const auto deaths = take_observed_deaths();
            if (!startup_mode && shared->death_link)
                shared->deaths += deaths;
            copy_console_snapshot(*snapshot, *shared);
            ReleaseMutex(bridge_mutex);
            if (snapshot->line_clear != previous_clear)
            {
                scroll = 0;
                popups.clear();
                popup_seen = previous_lines;
            }
            else if (scroll && snapshot->line_write >= previous_lines)
                scroll = static_cast<unsigned>(std::min<std::uint64_t>(
                    scroll + snapshot->line_write - previous_lines, snapshot->line_count ? snapshot->line_count - 1 : 0));
            if (console_open || discard_popup_history)
            {
                popups.clear();
                discard_popup_history = false;
            }
            else if (snapshot->line_write > popup_seen)
            {
                const auto count = static_cast<unsigned>(std::min<std::uint64_t>(snapshot->line_write - popup_seen, snapshot->line_count));
                for (unsigned i = snapshot->line_count - count; i < snapshot->line_count; ++i)
                {
                    const auto &line = console_line(*snapshot, i);
                    if (!line.ticks)
                        continue;
                    if (popups.pending.size() == max_popup_lines)
                        popups.pending.pop_front();
                    popups.pending.push_back(line);
                    if (popup_history.size() == popup_history_lines)
                        popup_history.pop_front();
                    popup_history.push_back(line);
                }
            }
            popup_seen = snapshot->line_write;
        }
        ID3D11Device *current_device = nullptr;
        chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&current_device));
        if (renderer && renderer->device != current_device)
            renderer.reset();
        release(current_device);
        if (!renderer)
            renderer = std::make_unique<Renderer>(chain);
        ID3D11Texture2D *back = nullptr;
        if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back))))
            return;
        D3D11_TEXTURE2D_DESC bd{};
        back->GetDesc(&bd);
        release(back);
        auto &r = *renderer;
        r.width = bd.Width;
        r.height = bd.Height;
        r.draw.clear();
        if (!r.width || !r.height)
            return;
        const auto scene = startup_mode ? SceneView{} : ingame::scene();
        if (startup_mode)
        {
            r.quad(0, 0, r.width, r.height, 0xff241810);
            r.text(24, r.height - 92, "Crash N. Sane Trilogy | Archipelago", 0xff20d8ff);
            r.text(24, r.height - 68, "Waiting for this seed's saves. /quit: close NST", 0xffeeeeee);
            r.text(24, r.height - 44, "/server host:port   /slot name   /password value   /connect", 0xffeeeeee);
        }
        // Darkness Trap: a spotlight that follows Crash, drawn under the AP HUD even when F7 hides markers
        if (!startup_mode && scene.camera_active && !snapshot->traps_paused && !menu_open && !darkness_exempt && timer_active(snapshot->darkness_until, GetTickCount64()))
        {
            const float side = static_cast<float>(std::min(r.width, r.height));
            float cx = r.width / 2.0f, cy = r.height * 0.55f, px = 0, py = 0;
            if (scene.player_active && project(scene.view_projection, scene.player_position, px, py, r.width, r.height) &&
                px > -side && px < r.width + side && py > -side && py < r.height + side)
                cx = px, cy = py - side * 0.05f; // the position is at Crash's feet; light his body
            darkness_mesh(static_cast<float>(r.width), static_cast<float>(r.height), cx, cy,
                          [&](const DarkVertex &a, const DarkVertex &b, const DarkVertex &c)
                          {
                              if (r.draw.size() + 3 > Renderer::max_vertices)
                                  return;
                              for (const auto *v : {&a, &b, &c})
                                  r.draw.push_back(r.vertex(v->x, v->y, 0.00024f, 0.00024f, v->color));
                          });
        }
        // the Nitro Trap's countdown, near the top middle of the screen (shown even with the HUD hidden), a number per
        // TNT beat; it goes away when the countdown ends and the Nitro comes
        if (!startup_mode && scene.camera_active && snapshot->nitro_countdown_end)
            if (const auto now = GetTickCount64(); now < snapshot->nitro_countdown_end)
            {
                const auto step = nitro_countdown_step_ms;
                const auto left = static_cast<unsigned>((snapshot->nitro_countdown_end - now + step - 1) / step);
                const std::string warning = "NITRO INCOMING!  " + std::to_string(left);
                const float scale = r.height >= 1000 ? 2.0f : 1.6f;
                const float width = r.text_width(warning) * scale, x = (r.width - width) / 2, y = r.height * 0.16f;
                r.quad(x - 18 * scale, y - 6 * scale, width + 36 * scale, 34 * scale, 0xbb101010);
                // Nitro green, blinking brighter as each number appears
                const bool flash = (snapshot->nitro_countdown_end - now) % step > step - 200;
                r.big_text(x + 2 * scale, y + 2 * scale, warning, 0xff000000, scale);
                r.big_text(x, y, warning, flash ? 0xffb0ffb0 : 0xff30ff50, scale);
            }
        if (snapshot->visible && !startup_mode)
        {
            if (scene.camera_active)
            {
                float goal_y = 16;
                // a box in the top-right corner, one under another; a leading \x02 makes its text green
                const auto corner_box = [&](std::string text) {
                    const bool green = !text.empty() && text[0] == '\x02';
                    if (green)
                        text.erase(0, 1);
                    const float width = r.text_width(text) + 16, x = std::max(4.0f, r.width - width - 16);
                    r.quad(x, goal_y, width, 26, 0xbb181818);
                    r.text(x + 8, goal_y + 2, text, green ? 0xff40e040 : 0xff20d8ff);
                    goal_y += 32;
                };
                const auto hub_game = hub_campaign(scene.zone);
                if (hub_game && snapshot->campaign_counts[hub_game - 1][0])
                    corner_box(snapshot->campaign_counts[hub_game - 1]);
                if (!main_menu(scene.zone) && snapshot->goal_counts[0])
                    corner_box(snapshot->goal_counts);
                // how many of this game's levels you can enter now
                if (hub_game && snapshot->campaign_levels[hub_game - 1][0])
                    corner_box(snapshot->campaign_levels[hub_game - 1]);
                {
                    // running trap timers
                    const auto now = GetTickCount64();
                    std::string traps;
                    struct TrapTimer
                    {
                        std::uint64_t until;
                        const char *label;
                        unsigned bit; // trap_bit() of a kind that takes turns with others, or 0
                    };
                    // a trap with no effect in this level says "(off here)": its time stands still until a level where it works
                    const auto off = [&](unsigned kind, const char *on, const char *off_here) {
                        return (snapshot->traps_off_here & trap_bit(kind)) && !snapshot->traps_paused ? off_here : on;
                    };
                    const TrapTimer timers[]{{snapshot->move_lock_until, off(TrapMoveLock, "Move Lock", "Move Lock (off here)"), 0},
                                             {snapshot->reverse_until, "Reversed", trap_bit(TrapReverse)},
                                             {snapshot->freeze_until, "Frozen", 0},
                                             {snapshot->slippery_until, off(TrapSlippery, "Slippery", "Slippery (off here)"), trap_bit(TrapSlippery)},
                                             {snapshot->slow_until, off(TrapSlow, "Slow", "Slow (off here)"), trap_bit(TrapSlow)},
                                             {snapshot->darkness_until, off(TrapDarkness, "Darkness", "Darkness (off here)"), 0}};
                    // Slow, Slippery and Reverse take turns: the ones waiting for theirs say "next", their time standing still
                    for (const auto &[until, label, bit] : timers)
                        if (timer_active(until, now))
                        {
                            traps += (traps.empty() ? "" : "  ") + std::string(label) + ((snapshot->traps_waiting & bit) ? " (next)" : "") + " " +
                                     std::to_string(seconds_left(until, now)) + "s";
                        }
                    if (!traps.empty() && snapshot->traps_paused)
                        traps = "Paused: " + traps;
                    if (!traps.empty())
                    {
                        const float width = r.text_width(traps) + 16, x = std::max(4.0f, r.width - width - 16);
                        r.quad(x, goal_y, width, 26, 0xbb181818);
                        r.text(x + 8, goal_y + 2, traps, 0xff4040ff);
                    }
                }
                for (const auto &box : live_boxes(*snapshot, scene.zone))
                {
                    std::array<std::array<float, 2>, 8> points{};
                    bool ok = true;
                    for (unsigned i = 0; i < 8; ++i)
                        if (!project(scene.view_projection, box.corners[i], points[i][0], points[i][1], r.width, r.height))
                        {
                            ok = false;
                            break;
                        }
                    if (ok)
                        for (unsigned i = 0; i < 8; ++i)
                            for (unsigned bit = 1; bit <= 4; bit *= 2)
                                if (!(i & bit))
                                    r.line(points[i][0], points[i][1], points[i | bit][0], points[i | bit][1], box.color ? box.color : 0xff20d8ff);
                }
                const auto key = ingame::level_key(scene.zone);
                if (const auto current = level(key))
                {
                    // game and level, then spelled-out check counts (all checks, then gems, power crystal, relic), then
                    // Crashsanity
                    std::vector<std::string> lines;
                    const std::string detail = current->detail;
                    for (std::size_t start = 0; start < detail.size();)
                    {
                        const auto end = std::min(detail.find('\n', start), detail.size());
                        lines.push_back(detail.substr(start, end - start));
                        start = end + 1;
                    }
                    const std::string crashsanity = !current->style              ? ""
                                                    : snapshot->vanilla_controls ? "Crashsanity: vanilla controls (F10)"
                                                    : snapshot->applied_style    ? "Crashsanity: Crash " + std::to_string(snapshot->applied_style) + " (F10: vanilla controls)"
                                                                                 : "Crashsanity: vanilla controls (style not applied)";
                    float width = std::max(r.text_width(current->title), r.text_width(crashsanity));
                    for (const auto &line : lines)
                        width = std::max(width, r.text_width(!line.empty() && (line[0] == '\x01' || line[0] == '\x02') ? line.substr(1) : line));
                    const auto rows = 1 + lines.size() + (crashsanity.empty() ? 0 : 1);
                    r.quad(16, 16, std::min<float>(std::max(width + 16, 300.0f), r.width - 32), 22.0f * rows + 6, 0xbb181818);
                    r.text(22, 20, current->title, 0xffeeeeee);
                    float y = 42;
                    for (const auto &line : lines)
                    {
                        // a leading \x01: you cannot get it with your current items (red); \x02: you can now (green)
                        const char mark = line.empty() ? 0 : line[0];
                        const bool marked = mark == '\x01' || mark == '\x02';
                        r.text(22, y, marked ? line.substr(1) : line, mark == '\x01' ? 0xff4040ff : mark == '\x02' ? 0xff40e040 : 0xff20d8ff), y += 22;
                    }
                    if (!crashsanity.empty())
                        r.text(22, y, crashsanity, 0xffeeeeee);
                }
                std::vector<std::array<float, 4>> labels;
                const auto campaign = ingame::hub_campaign(scene.zone);
                for (unsigned i = 0; i < snapshot->entrance_count; ++i)
                {
                    const auto &anchor = snapshot->entrances[i];
                    auto hub = std::string(anchor.hub);
                    hub[0] = 'l';
                    if (scene.zone.rfind("crash" + hub.substr(1, 1) + "/" + hub + "_", 0) != 0)
                        continue;
                    if (!campaign || !scene.player_active || !entrance_label_visible(scene.player_position, anchor.position, snapshot->hub_label_distances[campaign - 1]))
                        continue;
                    const auto target = level(anchor.destination);
                    if (!target)
                        continue;
                    float x = 0, y = 0;
                    if (!project(scene.view_projection, {anchor.position[0], anchor.position[1], anchor.position[2]}, x, y, r.width, r.height))
                        continue;
                    const float center = x;
                    y += 12;
                    // game and level, (All Done) once every check is found; checks found/total and available; then the
                    // level clear, C crystal, G gems, R relic
                    const bool extra = target->collectible_counts[0];
                    // line three: tab-separated pieces, red (\x01) when you cannot get it yet, green (\x02) when you can
                    std::vector<std::pair<std::string, unsigned>> pieces;
                    std::string plain;
                    {
                        const std::string all = target->collectible_counts;
                        for (std::size_t start = 0; start < all.size();)
                        {
                            const auto end = std::min(all.find('\t', start), all.size());
                            auto piece = all.substr(start, end - start);
                            const char mark = piece.empty() ? 0 : piece[0];
                            const unsigned color = mark == '\x01' ? 0xff4040ff : mark == '\x02' ? 0xff40e040 : 0xff20d8ff;
                            if (mark == '\x01' || mark == '\x02')
                                piece.erase(0, 1);
                            plain += (plain.empty() ? "" : "   ") + piece;
                            pieces.push_back({piece, color});
                            start = end + 1;
                        }
                    }
                    const std::string status = target->all_done ? " (All Done)" : "";
                    // line two: checks, green or red by its leading mark
                    std::string checks = target->counts;
                    const unsigned checks_color = checks.empty() ? 0xff20d8ff : checks[0] == '\x01' ? 0xff4040ff : checks[0] == '\x02' ? 0xff40e040 : 0xff20d8ff;
                    if (!checks.empty() && (checks[0] == '\x01' || checks[0] == '\x02'))
                        checks.erase(0, 1);
                    // line four (Secret Exit Checks): the level's secret exit, green or red by its leading mark
                    std::string secret = target->secret_exit;
                    const unsigned secret_color = secret.empty() ? 0xff20d8ff : secret[0] == '\x01' ? 0xff4040ff : secret[0] == '\x02' ? 0xff40e040 : 0xff20d8ff;
                    if (!secret.empty() && (secret[0] == '\x01' || secret[0] == '\x02'))
                        secret.erase(0, 1);
                    const float title_width = r.text_width(target->title);
                    const float label_height = (extra ? 68 : 46) + (secret.empty() ? 0 : 22);
                    const float label_width = std::min<float>(std::max({title_width + r.text_width(status), r.text_width(checks), r.text_width(plain),
                                                                         r.text_width(secret)}) + 12, 480);
                    for (const auto &previous : labels)
                        if (std::abs(previous[0] - center) < (previous[3] + label_width) / 2 &&
                            y < previous[1] + previous[2] + 2 && y + label_height > previous[1] - 2)
                            y = previous[1] + previous[2] + 2;
                    labels.push_back({center, y, label_height, label_width});
                    x = std::clamp(center - label_width / 2, 4.0f, std::max(4.0f, r.width - label_width - 4));
                    if (y < 0 || y + label_height > r.height)
                        continue;
                    r.quad(x, y, label_width, label_height, 0xbb181818);
                    r.text(x + 6, y, target->title, 0xff20d8ff);
                    if (!status.empty())
                        r.text(x + 6 + title_width, y, status, 0xff40e040);
                    r.text(x + 6, y + 22, checks, checks_color);
                    if (extra)
                    {
                        float piece_x = x + 6;
                        for (const auto &[piece, color] : pieces)
                        {
                            r.text(piece_x, y + 44, piece, color);
                            piece_x += r.text_width(piece) + r.text_width("   ");
                        }
                    }
                    if (!secret.empty())
                        r.text(x + 6, y + (extra ? 66 : 44), secret, secret_color);
                }
            }
        }
        const auto now = GetTickCount64();
        if (console_open)
        {
            const float console_height = std::min<float>(r.height - 24, 550);
            r.quad(12, 12, r.width - 24, console_height, 0xee141414);
            r.text(24, 20, "Archipelago | F1/Esc: close | Tab: complete | Up/Down: history | PgUp/PgDn: scroll", 0xff20d8ff);
            r.text(24, 44, snapshot->binding, 0xffdddddd);
            const unsigned end = snapshot->line_count > scroll ? snapshot->line_count - scroll : 0;
            unsigned start = end;
            float used = 0;
            while (start)
            {
                const auto height = r.rich_line(24, 0, console_line(*snapshot, start - 1), r.width - 30, false);
                if (used + height > console_height - 114)
                    break;
                used += height;
                --start;
            }
            float y = 74;
            for (unsigned i = start; i < end; ++i)
                y += r.rich_line(24, y, console_line(*snapshot, i), r.width - 30, true, console_height - 26);
            y = console_height - 30;
            const bool secret = secret_input(input.text);
            const auto prompt = secret ? "[password input hidden]" : input.text;
            const auto cursor_x = 24 + r.text_width("> " + (secret ? prompt : input.text.substr(0, input.cursor)));
            if (!secret && input.cursor != input.anchor)
            {
                const auto first = std::min(input.cursor, input.anchor), last = std::max(input.cursor, input.anchor);
                const auto start = 24 + r.text_width("> " + input.text.substr(0, first));
                r.quad(start, y + 6, r.text_width(input.text.substr(first, last - first)), 20, 0xc0805030);
            }
            const auto end_x = r.text(24, y + 6, "> " + prompt, 0xffeeeeee);
            if (!input.text.empty() && !secret && !tab_active && input.cursor == input.text.size() && input.cursor == input.anchor)
            {
                const auto matches = complete(input.text, commands(), unpack_names(*snapshot, snapshot->item_start, snapshot->location_start),
                                              unpack_names(*snapshot, snapshot->location_start, snapshot->completion_bytes));
                if (!matches.empty())
                    r.text(end_x, y + 6, matches.front().substr(input.text.size()), 0x70aaaaaa);
            }
            if ((now / 500) % 2)
                r.quad(cursor_x, y + 6, 1, 20, 0xffeeeeee);
        }
        else
        {
            popups.update(now);
            const float right = std::min<float>(r.width - 16, 700), max_height = std::max(22.0f, (r.height - 40.0f) / 3 - 8);
            if (history_held && !startup_mode)
            {
                // F2 held: the latest popups, newest at the bottom, as many as fit
                std::vector<float> heights;
                float total = 30;
                const float line_max = std::max(22.0f, (r.height - 80.0f) / popup_history_lines);
                for (const auto &line : popup_history)
                {
                    const auto height = std::min(line_max, r.rich_line(20, 0, line, right, false));
                    heights.push_back(height);
                    total += height + 6;
                }
                float y = std::max(8.0f, r.height - 16 - total);
                r.quad(14, y - 4, right - 8, r.height - 12 - y, 0xdd141414);
                r.text(20, y, popup_history.empty() ? std::string("Nothing found or received yet (hold F2)") : "Latest (hold F2)", 0xff20d8ff);
                y += 30;
                for (unsigned i = 0; i < popup_history.size(); ++i)
                {
                    r.rich_line(20, y, popup_history[i], right, true, y + heights[i], 1.0f);
                    y += heights[i] + 6;
                }
            }
            else
            {
                std::vector<float> heights;
                float total = 0;
                for (const auto &popup : popups.visible)
                {
                    const auto height = std::min(max_height, r.rich_line(20, 0, popup.line, right, false));
                    heights.push_back(height);
                    total += height + 8;
                }
                float y = r.height - 16 - total;
                for (unsigned i = 0; i < popups.visible.size(); ++i)
                {
                    const auto &popup = popups.visible[i];
                    const auto opacity = popup_opacity(now, popup.started);
                    r.quad(14, y - 2, right - 8, heights[i] + 4, faded_color(0xcc141414, opacity));
                    r.rich_line(20, y, popup.line, right, true, y + heights[i], opacity);
                    y += heights[i] + 8;
                }
            }
            if (!startup_mode && main_menu(scene.zone) && campaign_menu_visible())
            {
                r.text(20, 20, "F1: AP console", 0xffcccccc);
                if (snapshot->visible)
                {
                    const auto positions = campaign_label_positions(r.width, r.height);
                    // under each game's logo: its available checks (green when your items reach any), then its
                    // available levels below
                    for (unsigned i = 0; i < positions.size(); ++i)
                    {
                        std::string counts = snapshot->campaign_counts[i];
                        if (counts.empty())
                            continue;
                        const std::string levels = snapshot->campaign_levels[i];
                        const bool green = counts[0] == '\x02';
                        if (green)
                            counts.erase(0, 1);
                        const float bottom = std::clamp<float>(positions[i].y, 4, std::max(4.0f, r.height - 30.0f));
                        float y = levels.empty() ? bottom : std::max(4.0f, bottom - 30);
                        for (const auto &[text, color] : {std::pair{counts, green ? 0xff40e040u : 0xff20d8ffu}, std::pair{levels, 0xff20d8ffu}})
                        {
                            if (text.empty())
                                continue;
                            const auto width = r.text_width(text) + 16;
                            const float x = std::clamp(positions[i].x - width / 2, 4.0f, std::max(4.0f, r.width - width - 4));
                            r.quad(x, y, width, 26, 0xbb181818);
                            r.text(x + 8, y + 2, text, color);
                            y += 30;
                        }
                    }
                }
            }
        }
        r.submit(chain);
        if (bridge_lock())
        {
            shared->attached = 1;
            ReleaseMutex(bridge_mutex);
        }
    }
    HRESULT STDMETHODCALLTYPE present(IDXGISwapChain *chain, UINT interval, UINT flags)
    {
        static std::uint64_t retry_after = 0;
        Present original = nullptr;
        {
            std::lock_guard<std::recursive_mutex> guard(render_mutex);
            for (const auto &hook : present_hooks)
                if (hook->chain == chain)
                {
                    original = hook->original;
                    break;
                }
        }
        if (!original)
            return DXGI_ERROR_INVALID_CALL;
        if (active && !startup_mode && !(flags & DXGI_PRESENT_TEST))
        {
            std::lock_guard<std::recursive_mutex> guard(render_mutex);
            if (GetTickCount64() >= retry_after)
                try
                {
                    frame(chain);
                }
                catch (const std::exception &e)
                {
                    error(e.what());
                    renderer.reset();
                    retry_after = GetTickCount64() + 5000;
                    if (bridge_lock())
                    {
                        shared->attached = 0;
                        ReleaseMutex(bridge_mutex);
                    }
                }
        }
        return original(chain, interval, flags);
    }
    bool replace_slot(void **at, void *expected, void *replacement)
    {
        DWORD old = 0;
        if (!VirtualProtect(at, sizeof(void *), PAGE_READWRITE, &old))
            return false;
        const auto previous = InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(at), replacement, expected);
        DWORD ignored = 0;
        VirtualProtect(at, sizeof(void *), old, &ignored);
        return previous == expected;
    }
    bool attach_present(IDXGISwapChain *chain)
    {
        std::lock_guard<std::recursive_mutex> guard(render_mutex);
        for (const auto &hook : present_hooks)
            if (hook->chain == chain)
                return replace_slot(reinterpret_cast<void **>(chain), hook->original_table, hook->table.data()) ||
                       *reinterpret_cast<void ***>(chain) == hook->table.data();
        if (present_hooks.size() == 8)
            return false;
        auto hook = std::make_unique<PresentHook>();
        hook->chain = chain;
        hook->original_table = *reinterpret_cast<void ***>(chain);
        // copy every method exposed by this same COM pointer. newer swap-chain interfaces extend the original 18 slots.
        unsigned count = 18;
        const auto version = [&](const IID &iid, unsigned methods)
        {IUnknown* newer=nullptr;
        if(SUCCEEDED(chain->QueryInterface(iid,reinterpret_cast<void**>(&newer)))){if(newer==chain)count=methods;newer->Release();} };
        version(__uuidof(IDXGISwapChain1), 29);
        version(__uuidof(IDXGISwapChain2), 36);
        version(__uuidof(IDXGISwapChain3), 40);
        version(__uuidof(IDXGISwapChain4), 41);
        std::copy_n(hook->original_table, count, hook->table.begin());
        // present is ABI slot 8. only this chain's vtable pointer is replaced, leaving other renderers untouched.
        hook->original = reinterpret_cast<Present>(hook->table[8]);
        if (!hook->original || hook->original == present)
            return false;
        hook->table[8] = reinterpret_cast<void *>(present);
        chain->AddRef();
        present_hooks.push_back(std::move(hook));
        const auto &added = present_hooks.back();
        return replace_slot(reinterpret_cast<void **>(chain), added->original_table, added->table.data());
    }
    HRESULT WINAPI create_device(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                 const D3D_FEATURE_LEVEL *levels, UINT level_count, UINT sdk, const DXGI_SWAP_CHAIN_DESC *desc,
                                 IDXGISwapChain **chain, ID3D11Device **device, D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **context)
    {
        const auto result = original_create(adapter, type, software, flags, levels, level_count, sdk, desc, chain, device, level, context);
        if (SUCCEEDED(result) && chain && *chain)
            try
            {
                if (!attach_present(*chain))
                    error("Cannot attach NST's private swap-chain renderer");
            }
            catch (const std::exception &e)
            {
                error(e.what());
            }
        return result;
    }
    bool install_present()
    {
        // NST's D3D11CreateDeviceAndSwapChain import slot captures the real game chain when it is created.
        create_slot = reinterpret_cast<void **>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + 0x13821b8);
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(create_slot, &region, sizeof(region)) || region.State != MEM_COMMIT)
            return false;
        if (*create_slot != reinterpret_cast<void *>(create_device))
        {
            const auto original = reinterpret_cast<CreateDevice>(*create_slot);
            if (!original)
                return false;
            original_create = original;
            if (!replace_slot(create_slot, reinterpret_cast<void *>(original), reinterpret_cast<void *>(create_device)))
                return false;
        }
        std::lock_guard<std::recursive_mutex> guard(render_mutex);
        for (const auto &hook : present_hooks)
            replace_slot(reinterpret_cast<void **>(hook->chain), hook->original_table, hook->table.data());
        return true;
    }
    void restore_hooks()
    {
        active = false;
        console_open = false;
        std::lock_guard<std::recursive_mutex> guard(render_mutex);
        // the DLL and mapping remain resident for late calls. only owned hooks restore
        bool handover = false;
        if (startup_mode && bridge_lock(1000))
        {
            handover = valid(*shared) && shared->stop == startup_handover;
            ReleaseMutex(bridge_mutex);
        }
        // NST can create its swap chain as soon as save sync releases startup
        if (!handover)
        {
            movie_skip_enabled = false;
            skip_movie = nullptr;
            for (const auto &hook : movie_hooks)
                replace_slot(hook.slot, hook.replacement, hook.original);
            if (create_slot)
                replace_slot(create_slot, reinterpret_cast<void *>(create_device), reinterpret_cast<void *>(original_create));
            for (const auto &hook : present_hooks)
                replace_slot(reinterpret_cast<void **>(hook->chain), hook->table.data(), hook->original_table);
        }
        {
            std::lock_guard<std::mutex> input_guard(input_mutex);
            for (const auto &hook : input_hooks)
                replace_slot(hook.slot, hook.replacement, hook.original);
        }
        if (game_window && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(game_window, GWLP_WNDPROC)) == window_proc)
            SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_window));
        if (bridge_lock(1000))
        {
            if (!startup_mode)
                restore_gameplay();
            shared->applied_style = 0;
            shared->console_open = 0;
            shared->attached = 0;
            ReleaseMutex(bridge_mutex);
        }
        game_window = nullptr;
        renderer.reset();
        worker_running = false;
    }
    DWORD WINAPI worker(void *)
    {
        while (active)
        {
            if (bridge_lock(100))
            {
                const bool stop = !valid(*shared) || shared->stop || GetTickCount64() - shared->heartbeat > 15000;
                ReleaseMutex(bridge_mutex);
                if (stop)
                    break;
            }
            Sleep(100);
        }
        restore_hooks();
        if (bridge_lock(1000))
        {
            shared->running = 0;
            ReleaseMutex(bridge_mutex);
        }
        return 0;
    }
    DWORD WINAPI startup_worker(void *)
    {
        // reuse the in-game renderer/console in this process before NST has a window. 
        // no engine function calls, gameplay hooks, or save reads here!!!
        HWND window = nullptr;
        IDXGISwapChain *chain = nullptr;
        ID3D11Device *device = nullptr;
        ID3D11DeviceContext *context = nullptr;
        try
        {
            while (active)
            {
                bool show = false, stop = false;
                if (bridge_lock(100))
                {
                    show = shared->startup_popup;
                    stop = shared->stop || GetTickCount64() - shared->heartbeat > 15000;
                    ReleaseMutex(bridge_mutex);
                }
                if (stop)
                {
                    active = false;
                    break;
                }
                if (show)
                    break;
                Sleep(20);
            }
            if (!active)
            {
                restore_hooks();
                if (bridge_lock(1000))
                {
                    shared->running = 0;
                    ReleaseMutex(bridge_mutex);
                }
                return 0;
            }
            WNDCLASSW type{};
            type.hInstance = self;
            type.lpfnWndProc = DefWindowProcW;
            type.lpszClassName = L"CrashNST_AP_Startup";
            RegisterClassW(&type);
            const int width = 1000, height = 720;
            window = CreateWindowW(type.lpszClassName, L"Crash NST — Archipelago startup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                   std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2), std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2), width, height, nullptr, nullptr, self, nullptr);
            if (!window)
                throw std::runtime_error("Cannot create AP startup screen");
            DXGI_SWAP_CHAIN_DESC desc{};
            desc.BufferDesc.Width = width;
            desc.BufferDesc.Height = height;
            desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 1;
            desc.OutputWindow = window;
            desc.Windowed = TRUE;
            const auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context);
            checked(result, "Cannot create AP startup renderer");
            ShowWindow(window, startup_check ? SW_SHOWNOACTIVATE : SW_SHOW);
            if (!startup_check)
                SetForegroundWindow(window);
            toggle_console();
            while (active)
            {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                {
                    std::lock_guard<std::recursive_mutex> guard(render_mutex);
                    frame(chain);
                }
                checked(chain->Present(1, 0), "Cannot present AP startup console");
                Sleep(10);
            }
        }
        catch (const std::exception &e)
        {
            error(e.what());
        }
        restore_hooks();
        release(context);
        release(device);
        release(chain);
        if (window)
            DestroyWindow(window);
        UnregisterClassW(L"CrashNST_AP_Startup", self);
        if (bridge_lock(1000))
        {
            shared->running = 0;
            ReleaseMutex(bridge_mutex);
        }
        return 0;
    }
}
extern "C" bool NST_PrepareRenderer()
{
    const bool ready = install_present();
    if (ready && !install_movie_skip())
        std::cerr << "Activision movie skip unavailable. Using normal playback.\n";
    return ready;
}
extern "C" __declspec(dllexport) DWORD WINAPI NST_Start(void *startup)
{
    if (worker_running)
        return active ? 1 : 0;
    if (worker_thread)
    {
        WaitForSingleObject(worker_thread, INFINITE);
        CloseHandle(worker_thread);
        worker_thread = nullptr;
    }
    try
    {
        if (!shared)
        {
            bridge_mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, ingame::name(GetCurrentProcessId(), L"_mutex").c_str());
            mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, ingame::name(GetCurrentProcessId(), L"_state").c_str());
            if (!bridge_mutex || !mapping)
                return 0;
            shared = static_cast<ingame::Shared *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ingame::Shared)));
        }
        if (!shared || !bridge_lock(1000))
            return 0;
        const bool ok = valid(*shared);
        ReleaseMutex(bridge_mutex);
        if (!ok)
            return 0;
        startup_mode = startup != nullptr;
        startup_check = startup == reinterpret_cast<void *>(2);
        input.set("");
        scroll = 0;
        tab_active = false;
        popups.clear();
        popup_history.clear();
        popup_seen = 0;
        discard_popup_history = false;
        snapshot = std::make_unique<Shared>();
        active = true;
        if (!startup_check)
        {
            if (!NST_PrepareRenderer())
            {
                active = false;
                error("Cannot capture NST's swap-chain creation");
                return 0;
            }
            if (!startup_mode)
                install_input_hooks();
        }
        if (bridge_lock(1000))
        {
            shared->running = 1;
            ReleaseMutex(bridge_mutex);
        }
        worker_running = true;
        worker_thread = CreateThread(nullptr, 0, startup_mode ? startup_worker : worker, nullptr, 0, nullptr);
        if (!worker_thread)
        {
            restore_hooks();
            error("Cannot start in-game restoration worker");
            return 0;
        }
        return 1;
    }
    catch (const std::exception &e)
    {
        restore_hooks();
        error(e.what());
        return 0;
    }
}
namespace
{
    // Slow Trap walk tuner, fed every game frame. Each 250 ms window adds up the distance Crash covers frame by frame
    // (going back and forth counts in full, so quick direction changes never look like standing still), and a window
    // in which the pushed direction turned by more than about 45 degrees says nothing to the tuner.
    WalkTuner walk_tuner;
    float walk_setting = -1;
    struct WalkWindow
    {
        std::array<float, 3> start{}, last{};
        std::uint64_t at = 0;
        bool any_slow = false, all_slow = true;
        float path = 0;              // horizontal distance covered in this window
        float dir_x = 0, dir_y = 0;  // the pushed direction when it began
        bool turned = false;
    } walk_window;
    std::uint64_t walk_log_at = 0;
    float logged_run_speed = 0;
    int percent(float tilt) { return static_cast<int>(tilt * 100 + 0.5f); }
    void update_walk_tuner(Shared &state)
    {
        // /slow_tilt (or the last tuned value the client saved) is where tuning starts
        if (std::abs(state.slow_tilt - walk_setting) > 0.001f)
        {
            walk_setting = state.slow_tilt;
            if (std::abs(walk_tuner.tilt - walk_setting) > 0.001f)
                walk_tuner.restart(walk_setting);
        }
        walk_tilt = walk_tuner.tilt;
        const auto now = GetTickCount64();
        const bool slow_now = slowed();
        std::array<float, 3> at{};
        if (!crash_position(at))
        {
            walk_window.at = 0;
            return;
        }
        // the direction pushed now (the stick the game gets)
        const float px = static_cast<float>(pad_out_x.load()), py = static_cast<float>(pad_out_y.load()), plen = std::hypot(px, py);
        if (walk_window.at && now - walk_window.at < 250)
        {
            walk_window.any_slow = walk_window.any_slow || slow_now;
            walk_window.all_slow = walk_window.all_slow && slow_now;
            const float step = std::hypot(at[0] - walk_window.last[0], at[1] - walk_window.last[1]);
            if (step < 300) // not a respawn or teleport
                walk_window.path += step;
            walk_window.last = at;
            const float dlen = std::hypot(walk_window.dir_x, walk_window.dir_y);
            if (plen > 4000 && dlen > 4000 && (px * walk_window.dir_x + py * walk_window.dir_y) / (plen * dlen) < 0.7f)
                walk_window.turned = true;
            else if (plen > 4000 && dlen <= 4000)
                walk_window.dir_x = px, walk_window.dir_y = py;
            return;
        }
        if (walk_window.at)
        {
            const float seconds = (now - walk_window.at) / 1000.0f;
            walk_window.path += std::hypot(at[0] - walk_window.last[0], at[1] - walk_window.last[1]);
            const float ground = walk_window.path / seconds;
            const float climb = std::abs(at[2] - walk_window.start[2]) / seconds;
            // a window with a turn in it can show running, but not standing still
            const bool pushing = neutral_at.load() < walk_window.at;
            const bool all_slow = walk_window.all_slow && slow_now, any_slow = walk_window.any_slow || slow_now;
            const float before = walk_tuner.tilt;
            const int change = all_slow == any_slow ? walk_tuner.window(ground, climb, pushing, all_slow, walk_window.turned) : 0;
            if (walk_tuner.run_speed > logged_run_speed * 1.2f)
            {
                logged_run_speed = walk_tuner.run_speed;
                std::cout << "Slow tuner: normal running speed " << static_cast<int>(logged_run_speed) << " units/s\n" << std::flush;
            }
            if (change)
            {
                std::cout << "Slow tuner: Crash " << (change > 0 ? "stood still" : "ran") << " (" << static_cast<int>(ground)
                          << " units/s) at " << percent(before) << "% tilt, trying " << percent(walk_tuner.tilt) << "%\n" << std::flush;
                state.slow_tilt_tuned = walk_tuner.tilt;
            }
            if (all_slow && pushing && now - walk_log_at >= 2000)
            {
                walk_log_at = now;
                std::cout << "Slow: pad stick " << pad_in_x.load() << "," << pad_in_y.load() << " d-pad " << (pad_in_buttons.load() & 0xF)
                          << " -> game stick " << pad_out_x.load() << "," << pad_out_y.load() << " (" << percent(walk_tuner.tilt)
                          << "%); Crash ground speed " << static_cast<int>(ground) << ", climb " << static_cast<int>(climb) << " units/s\n"
                          << std::flush;
            }
        }
        walk_window = {at, at, now, slow_now, slow_now, 0, plen > 4000 ? px : 0, plen > 4000 ? py : 0, false};
    }
}
extern "C" __declspec(dllexport) void NST_Tick()
{
    if (!shared)
        return;
    if (!bridge_lock())
        return;
    try
    {
        if (!startup_mode)
        {
            if (active && valid(*shared) && !shared->stop)
            {
                // traps do nothing while paused in a hub or menu
                const bool paused = shared->traps_paused != 0;
                // Slow, Slippery and Reverse take turns: a waiting one does nothing (update_gameplay holds its timer)
                static TrapTurns turns;
                const std::uint64_t *const turn_timers[turn_trap_count]{&shared->slippery_until, &shared->slow_until,
                                                                        &shared->reverse_until};
                static_assert(turn_traps[0] == TrapSlippery && turn_traps[1] == TrapSlow && turn_traps[2] == TrapReverse, "turn_timers order");
                const unsigned waiting = shared->traps_waiting = turns.update(turn_timers, GetTickCount64());
                reverse_until = paused || (waiting & trap_bit(TrapReverse)) ? 0 : shared->reverse_until;
                freeze_until = paused ? 0 : shared->freeze_until;
                // Slippery has no effect in gimmick levels
                slippery_until = paused || (waiting & trap_bit(TrapSlippery)) || gimmick_stage() ? 0 : shared->slippery_until;
                // Slow has no effect in gimmick and chase levels
                slow_until = paused || (waiting & trap_bit(TrapSlow)) || slow_trap_exempt() ? 0 : shared->slow_until;
                update_walk_tuner(*shared);
                update_gameplay(*shared);
            }
            else
            {
                clear_input_traps();
                restore_gameplay(true);
            }
        }
    }
    catch (const std::exception &e)
    {
        clear_input_traps();
        shared->vanilla_controls = 1;
        copy(shared->error, e.what());
        restore_gameplay(true);
    }
    ReleaseMutex(bridge_mutex);
}
extern "C" __declspec(dllexport) unsigned NST_MovementCampaign(std::uintptr_t manager)
{
    if (active)
        return ingame::movement_campaign(manager);
    return reinterpret_cast<unsigned (*)(std::uintptr_t)>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + 0x131400)(manager);
}
extern "C" __declspec(dllexport) unsigned NST_DoubleJumpAllowed(unsigned owned)
{
    return active ? ingame::movement_double_jump(owned) : owned;
}
extern "C" __declspec(dllexport) std::uintptr_t NST_CrateIdentity(std::uintptr_t owner, const char **name)
{
    try
    {
        return ingame::crate_identity_owner(owner, name);
    }
    catch (...)
    {
        return 0;
    }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
