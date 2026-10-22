#pragma once
#include "ingame_protocol.h"
#include <vector>
#include <deque>
#include <array>

namespace ingame
{
    std::vector<std::string> complete(const std::string &input, const std::vector<std::string> &commands,
                                      const std::vector<std::string> &items, const std::vector<std::string> &locations);
    std::vector<std::string> unpack_names(const Shared &state, unsigned begin, unsigned end);
    const std::vector<std::string> &commands();
    unsigned box_marker_mode(const std::string &name);
    unsigned hub_distance(const std::string &value);
    float tint_opacity(const std::string &value);
    float slow_tilt(const std::string &value);
    const char *box_marker_mode_name(unsigned mode);
    bool valid_server(const std::string &server);
    struct ConsoleInput
    {
        std::string text;
        std::size_t cursor = 0, anchor = 0;
        void set(std::string value);
        void move(int direction, bool selecting);
        void edge(bool end, bool selecting);
        std::string selected() const;
        void insert(std::string value);
        void erase(bool backwards);
    };
    bool main_menu(std::string zone);
    struct StartupConnection
    {
        bool fallback = false, show = false;
        void retry(const std::string &server = "") { fallback = server.rfind("ws://", 0) == 0; }
        void packet(const std::string &command, bool fallback_pending = true)
        {
            // APCpp connection states
            if (command == "NativeFallback")
                fallback = true;
            else if (command == "NativeDisconnected" && (fallback || !fallback_pending))
                show = true;
            else if (command == "Connected")
                fallback = false;
            else if (command == "ConnectionRefused")
                show = true;
        }
    };
    std::array<POINT, 3> campaign_label_positions(unsigned width, unsigned height);
    bool entrance_label_visible(const std::array<float, 3> &player, const float *entrance, unsigned distance);
    struct Popup
    {
        Line line;
        std::uint64_t started;
    };
    // a popup (an item found or received, a trap, ...) stays this long, fading out over its last popup_fade_ms
    inline constexpr std::uint64_t popup_total_ms = 7000, popup_fade_ms = 500;
    // hold F2 to see the latest popups again, as many as fit; the mouse wheel, Page Up/Down or the arrow keys scroll
    // back through this many
    inline constexpr unsigned popup_history_lines = 300;
    struct PopupQueue
    {
        std::deque<Line> pending;
        std::deque<Popup> visible;
        void clear()
        {
            pending.clear();
            visible.clear();
        }
        void update(std::uint64_t now);
    };
    float popup_opacity(std::uint64_t now, std::uint64_t started);
    unsigned faded_color(unsigned color, float opacity);
}
