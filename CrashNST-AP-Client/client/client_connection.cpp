#include "client_runtime.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fstream>
#include <array>
#include <map>
#include "trap_rules.h"
#include "game_sound.h"
#include "game_archive.h"
#include "skins.h"
void ClientRuntime::queue_ap_packet(std::string raw)
{
    std::lock_guard<std::mutex> lock(state_mutex);
    if (raw.size() > 8 * 1024 * 1024 || ap_packets.size() >= 256 ||
        ap_packet_bytes + raw.size() > 16 * 1024 * 1024)
    {
        ap_packet_error = "AP packet queue exceeded its bound. Restart the client to resynchronize";
        return;
    }
    ap_packets.push_back(std::move(raw));
    ap_packet_bytes += ap_packets.back().size();
}

void ClientRuntime::register_packet_callbacks()
{
    AP_SetNativePacketCallback([this](std::string raw) { queue_ap_packet(std::move(raw)); });
    AP_SetLocationInfoCallback([](std::vector<AP_NetworkItem>) {});
    AP_RegisterSetReplyCallback([this](AP_SetReply reply) {
        if (reply.key.rfind("CrashNST:", 0) != 0 || reply.key.find(":backup:") != std::string::npos)
            return;
        Json::Value message(Json::arrayValue);
        message[0]["cmd"] = "SetReply";
        message[0]["key"] = reply.key;
        Json::Reader reader;
        if (!reader.parse(*static_cast<std::string *>(reply.value), message[0]["value"]) ||
            !reader.parse(*static_cast<std::string *>(reply.original_value), message[0]["original_value"]))
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            ap_packet_error = "Invalid AP DataStorage reply";
            return;
        }
        Json::FastWriter writer;
        queue_ap_packet(writer.write(message));
    });
}

void ClientRuntime::receive_items(const Json::Value &message)
{
    if (!message["index"].isUInt())
        throw std::runtime_error("Invalid received-item index");
    const auto index = message["index"].asUInt(), previous_count = session.received_count();
    if (!session.received(index, message["items"]))
    {
        AP_RequestSync();
        std::cout << "Received-item gap. Requested complete item history." << std::endl;
    }
    else
    {
        if (popups_ready)
            for (unsigned i = previous_count; i < session.received_count(); ++i)
                fresh_receipts.push_back(message["items"][i - index]);
        if (!index)
            receipt_history_ready = true;
    }
}

void ClientRuntime::initialize_ap()
{
    popups_ready = false;
    receipt_history_ready = false;
    fresh_receipts.clear();
    player_aliases.clear();
    deferred_receipts.clear();
    deferred_receipt_bytes = 0;
    startup_connection.retry(server);
    AP_Init(server.c_str(), "Crash Bandicoot N. Sane Trilogy", slot.c_str(), connection_password.c_str());
    AP_NetworkVersion client_version{1, 0, 2};
    AP_SetClientVersion(&client_version);
    AP_EnableQueueItemRecvMsgs(false);
    AP_SetDeathLinkSupported(true);
    AP_SetDeathLinkRecvCallback(
        std::function<void(std::string, std::string)>([this](std::string source, std::string cause) {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (console_notifications.size() == ingame::max_lines)
                console_notifications.pop_front();
            console_notifications.push_back("DeathLink from " + source.substr(0, 255) +
                                            (cause.empty() ? "." : ": " + cause.substr(0, 1024)));
            if (deaths_seen.size() < 256)
                deaths_seen.push_back(source.substr(0, 60));
        }));
    register_packet_callbacks();
    AP_SetItemClearCallback([] {});
    AP_SetItemRecvCallback([](std::int64_t, int, bool) {});
    AP_SetLocationCheckedCallback([](std::int64_t) {});
    AP_Start();
}

void ClientRuntime::save_connection()
{
    Json::Value config;
    config["server"] = staged_server;
    config["slot"] = staged_slot;
    write_state_json(standalone::connection_file(), config);
}

int ClientRuntime::initialize()
{
    if (const auto password = std::getenv("CRASH_NST_AP_PASSWORD"))
        connection_password = password;
    try
    {
        if (session.resume(session_directory, session_profile, ""))
        {
            seed.emplace(session.slot_data());
            observed_seed = session.seed();
            server_matches_session = true;
            std::cout << "Resuming cached AP session " << observed_seed << ". Earned checks will queue while offline."
                      << std::endl;
        }
    }
    catch (const std::exception &error)
    {
        startup_connection.show = true;
        std::cerr << "Cached AP session unavailable: " << error.what()
                  << ". Cached files retained. Connect to a current seed using the startup console.\n";
    }
    const auto host = server.rfind("wss://", 0) == 0  ? server.substr(6)
                      : server.rfind("ws://", 0) == 0 ? server.substr(5)
                                                      : server;
    if (host.empty() || host.find('/') != std::string::npos || host.find(' ') != std::string::npos)
    {
        std::cerr << "Expected server:port, wss://server:port, or ws://server:port.\n";
        return 2;
    }

    try
    {
        if (std::filesystem::exists(display_file))
        {
            const auto config = read_state_json(display_file);
            if (config.isMember("box_markers"))
                box_marker_mode = ingame::box_marker_mode(config["box_markers"].asString());
            if (config.isMember("out_of_logic_markers"))
            {
                if (!config["out_of_logic_markers"].isBool())
                    throw std::runtime_error("Invalid out-of-logic marker setting");
                out_of_logic_markers = config["out_of_logic_markers"].asBool();
            }
            if (config.isMember("hub_label_distance"))
            {
                if (!config["hub_label_distance"].isUInt() || config["hub_label_distance"].asUInt() > ingame::max_hub_distance)
                    throw std::runtime_error("Invalid hub label distance");
                hub_label_distances.fill(config["hub_label_distance"].asUInt());
            }
            for (unsigned i = 0; i < hub_label_distances.size(); ++i)
            {
                const auto key = "hub_label_distance_c" + std::to_string(i + 1);
                if (!config.isMember(key))
                    continue;
                if (!config[key].isUInt() || config[key].asUInt() > ingame::max_hub_distance)
                    throw std::runtime_error("Invalid Crash " + std::to_string(i + 1) + " hub label distance");
                hub_label_distances[i] = config[key].asUInt();
            }
            if (config.isMember("tint_opacity"))
            {
                if (!config["tint_opacity"].isNumeric() || !ingame::valid_tint_opacity(config["tint_opacity"].asFloat()))
                    throw std::runtime_error("Invalid tint opacity");
                tint_opacity = config["tint_opacity"].asFloat();
            }
            if (config.isMember("slow_tilt") && config["slow_tilt"].isNumeric() && ingame::valid_slow_tilt(config["slow_tilt"].asFloat()))
                slow_tilt = config["slow_tilt"].asFloat();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Marker settings: " << e.what() << ". Using tint.\n";
    }
    for (int boss = 1; boss <= 5; ++boss)
        c2_access_levels.push_back("B20" + std::to_string(boss));

    initialize_ap();
    std::cout << "F1: AP console. Close NST normally to exit." << std::endl;
    return 0;
}

void ClientRuntime::handle_commands()
{
    while (!console_commands.empty())
    {
        const auto text = console_commands.front();
        console_commands.pop_front();
        const auto space = text.find(' ');
        const auto command = text.substr(0, space);
        const auto argument = space == std::string::npos ? std::string{} : text.substr(space + 1);
        try
        {
            if (command == "/help")
                print_console(
                    "/server host:port | /port number | /slot name | /password value\n/connect | /disconnect | /status "
                    "| /quit | /markers on/off/toggle | /boxmarkers bounds/tint/off | /outoflogic on/off | /hubdistance 0..100000 | "
                    "/hubdistance_c1 /hubdistance_c2 /hubdistance_c3 0..100000 | /tint_opacity 0..1 | /slow_tilt 0.2..1 | /trap_test name | "
                    "/crashsanity on/off/toggle\nChat and !commands go to the AP server. Tab completes commands and "
                    "!hint / !hint_location names.\nF1: console | F7: markers/HUD | F10: vanilla controls");
            else if (command == "/server")
            {
                if (argument.empty())
                    print_console("Server: " + staged_server);
                else if (ingame::valid_server(argument))
                {
                    staged_server = argument;
                    save_connection();
                    print_console("Server saved as " + argument + (standalone::save_ready() ? ". Restart NST to apply it." : ". /connect applies it."));
                }
                else
                    print_console("Invalid server. Use host:port, ws://host:port or wss://host:port.");
            }
            else if (command == "/port")
            {
                auto address = staged_server;
                const auto scheme = address.find("://");
                const auto start = scheme == std::string::npos ? 0 : scheme + 3;
                const auto colon = address.rfind(':');
                if (colon != std::string::npos && colon >= start)
                    address.erase(colon);
                address += ':' + argument;
                if (argument.empty() || argument.size() > 5 ||
                    !std::all_of(argument.begin(), argument.end(), [](unsigned char c) { return std::isdigit(c); }) ||
                    !ingame::valid_server(address))
                    print_console("Port must be 1..65535.");
                else
                {
                    staged_server = address;
                    save_connection();
                    print_console("Server saved as " + address + (standalone::save_ready() ? ". Restart NST to apply it." : ". /connect applies it."));
                }
            }
            else if (command == "/slot")
            {
                if (argument.empty())
                    print_console("Slot: " + staged_slot);
                else if (argument.size() <= 255 && std::none_of(argument.begin(), argument.end(),
                                                                [](unsigned char c) { return c < 32 || c == 127; }))
                {
                    staged_slot = argument;
                    save_connection();
                    print_console("Slot saved as " + argument + (standalone::save_ready() ? ". Restart NST to apply it." : ". /connect applies it."));
                }
                else
                    print_console("Invalid slot name.");
            }
            else if (command == "/password")
            {
                connection_password = argument;
                print_console(argument.empty() ? "Password cleared." : "Password set for the next connection.");
            }
            else if (command == "/disconnect")
            {
                AP_Shutdown();
                print_console("Disconnected. Earned checks remain queued locally.");
            }
            else if (command == "/quit")
                running = false;
            else if (command == "/connect")
            {
                save_connection();
                if ((staged_server != server || staged_slot != slot) && standalone::save_ready())
                    print_console("Connection settings saved. Restart NST to use the new server or slot. "
                                  "This running game keeps its current save profile.");
                else
                {
                    AP_Shutdown();
                    if (staged_server != server || staged_slot != slot || !standalone::save_ready())
                    {
                        cloud.reset();
                        bound_saves.clear();
                        save_dir.clear();
                        server_matches_session = false;
                        observed_seed.clear();
                        session_failed = false;
                        session_error.clear();
                    }
                    server = staged_server;
                    slot = staged_slot;
                    session_profile = server + '\n' + slot;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        ap_packets.clear();
                        ap_packet_bytes = 0;
                        ap_packet_error.clear();
                    }
                    initialize_ap();
                    names_loaded = false;
                    submitted_checks.clear();
                    goal_sent = false;
                    print_console("Connecting to " + server + " as " + slot + ".");
                }
            }
            else if (command == "/status")
                print_console("AP status " + std::to_string(static_cast<int>(AP_GetConnectionStatus())) + " | " +
                              server + " | " + slot);
            else if (command == "/boxmarkers")
            {
                if (!argument.empty())
                {
                    const auto mode = ingame::box_marker_mode(argument);
                    auto config = std::filesystem::exists(display_file) ? read_state_json(display_file)
                                                                        : Json::Value(Json::objectValue);
                    config["box_markers"] = ingame::box_marker_mode_name(mode);
                    std::filesystem::create_directories(display_file.parent_path());
                    write_state_json(display_file, config);
                    box_marker_mode = mode;
                    if (box_markers)
                        box_markers->marker_mode(mode);
                }
                print_console(std::string("Crate markers: ") + ingame::box_marker_mode_name(box_marker_mode) +
                              ". Use /boxmarkers bounds, tint or off.");
            }
            else if (command == "/tint_opacity")
            {
                if (!argument.empty())
                {
                    const auto opacity = ingame::tint_opacity(argument);
                    auto config = std::filesystem::exists(display_file) ? read_state_json(display_file)
                                                                        : Json::Value(Json::objectValue);
                    config["tint_opacity"] = opacity;
                    std::filesystem::create_directories(display_file.parent_path());
                    write_state_json(display_file, config);
                    tint_opacity = opacity;
                    if (box_markers)
                        box_markers->tint_opacity(opacity);
                }
                print_console("Tint opacity: " + std::to_string(tint_opacity) + ". Use /tint_opacity 0..1 (default 0.75).");
            }
            else if (command == "/slow_tilt")
            {
                if (!argument.empty())
                {
                    const auto tilt = ingame::slow_tilt(argument);
                    auto config = std::filesystem::exists(display_file) ? read_state_json(display_file)
                                                                        : Json::Value(Json::objectValue);
                    config["slow_tilt"] = tilt;
                    std::filesystem::create_directories(display_file.parent_path());
                    write_state_json(display_file, config);
                    slow_tilt = tilt;
                    if (box_markers)
                        box_markers->slow_tilt(tilt);
                }
                print_console("Slow Trap stick tilt: " + std::to_string(static_cast<int>(slow_tilt * 100 + 0.5f)) +
                              "% of a full push. Slow tunes this itself while you play (stood still: higher, ran: lower) and saves it. "
                              "/slow_tilt 0.2..1 sets where it starts. /trap_test slow tries Slow for 30 seconds.");
            }
            else if (command == "/slow_test" || command == "/trap_test")
            {
                // local trap preview: nothing is sent to AP
                static const std::pair<const char *, unsigned> kinds[]{{"slow", ingame::TrapSlow}, {"slippery", ingame::TrapSlippery},
                                                                        {"darkness", ingame::TrapDarkness}, {"reverse", ingame::TrapReverse},
                                                                        {"freeze", ingame::TrapFreeze}, {"movelock", ingame::TrapMoveLock},
                                                                        {"nitro", ingame::TrapNitro}};
                std::string name = command == "/slow_test" ? "slow" : argument;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto kind = std::find_if(std::begin(kinds), std::end(kinds), [&](const auto &k) { return name == k.first; });
                if (name == "deathlink")
                {
                    if (!box_markers)
                        print_console("DeathLink test needs the in-game adapter. Load into a level first.");
                    else
                    {
                        box_markers->receive_death();
                        print_console("DeathLink test: Crash dies as if someone else died (in a bonus round, the bonus round ends). Not sent to AP. Close the console (F1).");
                    }
                }
                else if (kind == std::end(kinds))
                    print_console("Use /trap_test slow, slippery, darkness, reverse, freeze, movelock, nitro or deathlink. Close the console (F1) to play.");
                else if (!box_markers)
                    print_console("Trap test needs the in-game adapter. Load into a level first.");
                else if (kind->second == ingame::TrapNitro)
                {
                    box_markers->request_nitro_drop(0);
                    print_console("Nitro Trap test: NITRO INCOMING 3, 2, 1, then a Nitro appears ahead of Crash or falls onto him (half and half). "
                                  "Not sent to AP. Close the console (F1) and move!");
                }
                else
                {
                    const auto after = box_markers->apply_trap(kind->second, 30);
                    print_console(std::string(ingame::trap_names[kind->second]) + " test" + (ingame::timed_trap(kind->second) ? " for 30 seconds" : "") +
                                  (after.empty() ? "" : ", once the " + after + " ends") +
                                  ". Not sent to AP. Close the console (F1) to play; it ignores the controller while open.");
                }
            }
            else if (command == "/hubdistance" || command == "/hubdistance_c1" || command == "/hubdistance_c2" || command == "/hubdistance_c3")
            {
                const int hub = command == "/hubdistance" ? -1 : command.back() - '1';
                if (!argument.empty())
                {
                    const auto distance = ingame::hub_distance(argument);
                    auto config = std::filesystem::exists(display_file) ? read_state_json(display_file)
                                                                        : Json::Value(Json::objectValue);
                    auto distances = hub_label_distances;
                    if (hub < 0)
                    {
                        distances.fill(distance);
                        config["hub_label_distance"] = distance;
                        for (unsigned i = 0; i < distances.size(); ++i)
                            config.removeMember("hub_label_distance_c" + std::to_string(i + 1));
                    }
                    else
                    {
                        distances[hub] = distance;
                        config["hub_label_distance_c" + std::to_string(hub + 1)] = distance;
                    }
                    std::filesystem::create_directories(display_file.parent_path());
                    write_state_json(display_file, config);
                    hub_label_distances = distances;
                    if (box_markers)
                        box_markers->hub_distance(distances);
                }
                if (hub < 0)
                    print_console("Hub label distances: Crash 1 " + std::to_string(hub_label_distances[0]) +
                                  ", Crash 2 " + std::to_string(hub_label_distances[1]) + ", Crash 3 " + std::to_string(hub_label_distances[2]) +
                                  " game units. /hubdistance sets all three. /hubdistance_c1, _c2 or _c3 sets one (0 hides labels).");
                else
                    print_console("Crash " + std::to_string(hub + 1) + " hub label distance: " + std::to_string(hub_label_distances[hub]) +
                                  " game units (0 hides labels).");
            }
            else if (command == "/outoflogic")
            {
                if (!argument.empty())
                {
                    if (argument != "on" && argument != "off")
                        throw std::invalid_argument("Use /outoflogic on or off");
                    auto config = std::filesystem::exists(display_file) ? read_state_json(display_file)
                                                                        : Json::Value(Json::objectValue);
                    config["out_of_logic_markers"] = argument == "on";
                    std::filesystem::create_directories(display_file.parent_path());
                    write_state_json(display_file, config);
                    out_of_logic_markers = argument == "on";
                }
                print_console(std::string("Out-of-logic crate markers: ") + (out_of_logic_markers ? "on" : "off") +
                              ". Use /outoflogic on or off. Check counts and AP logic are unchanged.");
            }
            else if (command == "/nitro_probe")
            {
                if (box_markers)
                {
                    box_markers->request_nitro_probe();
                    print_console("Nitro probe requested (read-only, changes nothing). Run it inside a level that has Nitro crates.");
                }
                else
                    print_console("Nitro probe needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/aku_lock")
            {
                if (argument == "on")
                    aku_lock_override = 1;
                else if (argument == "off")
                    aku_lock_override = 0;
                else if (argument == "auto")
                    aku_lock_override = -1;
                else if (!argument.empty())
                    throw std::invalid_argument("Use /aku_lock on, off or auto");
                if (box_markers && aku_lock_override >= 0)
                    box_markers->aku_lock(aku_lock_override == 1);
                print_console(std::string("Aku Aku lock: ") +
                              (aku_lock_override == 1 ? "on (test): Aku Aku crates should give no mask. /aku_lock auto goes back to the seed's setting."
                               : aku_lock_override == 0 ? "off (test): Aku Aku crates work normally. /aku_lock auto goes back to the seed's setting."
                                                        : "follows the seed (Aku Aku Sanity: no masks until you find the Aku Aku item)."));
            }
            else if (command == "/aku_give")
            {
                if (box_markers)
                {
                    box_markers->request_aku_give();
                    print_console("Aku give test: trying a few ways to hand Crash an Aku Aku mask, one a second (the log says which worked). Not sent to AP.");
                }
                else
                    print_console("Aku give needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/crash_probe")
            {
                if (box_markers)
                {
                    box_markers->request_crash_probe();
                    print_console("Crash probe: his model's parts and their names go to the log (\"Crash probe:\" lines). Read-only.");
                }
                else
                    print_console("Crash probe needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/nitro_volume")
            {
                // the Nitro Trap countdown's TNT sound volume, 0-100 (kept for next time)
                char *end = nullptr;
                const auto value = std::strtol(argument.c_str(), &end, 10);
                if (argument.empty() || !end || *end || value < 0 || value > 100)
                    print_console("Nitro Trap countdown volume: " + std::to_string(ingame::countdown_volume()) + ". Use /nitro_volume 0 to 100 (0 is silent).");
                else
                {
                    ingame::set_countdown_volume(static_cast<unsigned>(value));
                    std::ofstream(standalone::state_directory() / "nitro_volume.txt", std::ios::trunc) << value << '\n';
                    print_console("Nitro Trap countdown volume: " + std::to_string(value) + ".");
                }
            }
            else if (command == "/skin")
            {
                // /skin lists the skins in CrashNSTAP\skins, /skin name picks one (from the next game start), /skin default
                std::string list;
                for (const auto &name : skins::available())
                    list += (list.empty() ? "" : ", ") + name;
                if (argument.empty())
                {
                    const auto status = skins::status();
                    print_console((status.empty() ? std::string("Skin: Default Crash.") : status) + " Skins in CrashNSTAP\\skins: " +
                                  (list.empty() ? std::string("none yet") : list) + ". /skin name picks one, /skin default goes back.");
                }
                else
                {
                    const auto wanted = skins::normalize(argument);
                    skins::choose(wanted);
                    print_console(skins::worn() == wanted ? "Skin: already wearing " + skins::display(wanted) + "."
                                                          : "Skin: " + skins::display(wanted) + " chosen. Restart the game to wear it." +
                                                                (wanted == "default" || !list.empty() ? std::string() : " (CrashNSTAP\\skins is empty: put the skin there first.)"));
                }
            }
            else if (command == "/crash_tint")
            {
                // /crash_tint red green blue amount (each 0..1), a color name (blue, red, green, ...), or off
                std::istringstream values(argument);
                float red = 0, green = 0, blue = 0, amount = 0.6f;
                const bool off = argument == "off" || argument == "0";
                static const std::map<std::string, std::array<float, 3>> named{
                    {"red", {1, 0.1f, 0.1f}}, {"green", {0.1f, 0.9f, 0.2f}}, {"blue", {0.1f, 0.4f, 1}}, {"yellow", {1, 0.9f, 0.1f}},
                    {"purple", {0.6f, 0.2f, 1}}, {"pink", {1, 0.4f, 0.8f}}, {"orange", {1, 0.5f, 0.1f}}, {"black", {0.05f, 0.05f, 0.05f}},
                    {"white", {1, 1, 1}}, {"cyan", {0.1f, 0.9f, 1}}, {"gold", {1, 0.75f, 0.2f}}};
                std::string first;
                std::istringstream(argument) >> first;
                const auto name = named.find(first);
                if (name != named.end())
                {
                    red = name->second[0], green = name->second[1], blue = name->second[2];
                    values >> first; // skip the name; an amount may follow
                }
                if (!off && name == named.end() && !(values >> red >> green >> blue))
                    print_console("Use /crash_tint blue (or red, green, yellow, purple, pink, orange, black, white, cyan, gold) [amount 0 to 1], "
                                  "or /crash_tint red green blue [amount] with each 0 to 1, or /crash_tint off.");
                else if (!box_markers)
                    print_console("Crash tint needs the in-game adapter. Load into a level first.");
                else
                {
                    if (!off && !(values >> amount))
                        amount = 0.6f;
                    box_markers->crash_tint(red, green, blue, off ? 0.0f : amount);
                    print_console(off ? std::string("Crash tint off.")
                                      : "Crash tint test: his whole model blends toward that color (research for fur, pants and shoe colors). /crash_tint off to undo.");
                }
            }
            else if (command == "/nitro_drop")
            {
                if (box_markers)
                {
                    // /nitro_drop: the trap as it comes (half and half); /nitro_drop fall or ahead picks the ending
                    const unsigned kind = argument == "fall" ? 1 : argument == "ahead" ? 2 : 0;
                    box_markers->request_nitro_drop(kind);
                    print_console(std::string("Nitro Trap test: NITRO INCOMING 3, 2, 1, then a Nitro ") +
                                  (kind == 1 ? "falls onto Crash" : kind == 2 ? "appears ahead of Crash (keep running)" : "appears ahead of Crash or falls onto him") +
                                  ". Not sent to AP. Close the console (F1) and move! (/nitro_drop fall or /nitro_drop ahead picks one.)");
                }
                else
                    print_console("Nitro drop needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/sounds")
            {
                // research: the game sounds played in this level (a TNT's countdown shows up once one counts down)
                const auto names = ingame::game_sound_names();
                std::string list;
                for (const auto &name : names)
                    list += (list.empty() ? "" : ", ") + name;
                const auto tnt = ingame::tnt_tick_sound();
                print_console(names.empty() ? std::string("No game sounds seen in this level yet (or NST's FMOD isn't reachable: see \"Game sound:\" in the log).")
                                            : "Game sounds this level: " + list + (tnt.empty() ? "" : ". Nitro countdown uses: " + tnt));
            }
            else if (command == "/sound_find" || command == "/sound_dump" || command == "/nitro_sound")
            {
                // the game's own sound banks (its archives): /sound_find text [all] lists sounds, /sound_dump text saves
                // an archive file to send, /nitro_sound name picks the Nitro countdown's sound
                if (sound_job.valid())
                    print_console("A sound search is still running; its results print when it ends.");
                else if (argument.empty())
                    print_console(command == "/sound_find" ? "Use /sound_find tnt (or any part of a sound's name), /sound_find tnt all to search every archive."
                                  : command == "/sound_dump" ? "Use /sound_dump part of an archive file's name (from /sound_find)."
                                                             : "Use /nitro_sound part of a sound's name (or \"file #number\") from /sound_find, for the Nitro countdown; /nitro_sound off for the plain tick.");
                else if (command == "/nitro_sound" && argument == "off")
                {
                    ingame::set_countdown_sound({}, {});
                    std::error_code error;
                    std::filesystem::remove(standalone::state_directory() / "tnt_sound.txt", error);
                    print_console("Nitro Trap countdown: back to the client's tick.");
                }
                else
                {
                    auto text = argument;
                    bool everywhere = false;
                    if (text.size() > 4 && text.compare(text.size() - 4, 4, " all") == 0)
                        text.resize(text.size() - 4), everywhere = true;
                    if (command == "/sound_dump")
                        start_sound_job([text] {
                            const auto file = ingame::dump_archive_member(text, standalone::state_directory() / "dumps");
                            return std::vector<std::string>{file.empty() ? "Sound dump: no archive file matching \"" + text + "\""
                                                                         : "Sound dump: saved " + file.string() + " (send this file)"};
                        });
                    else
                        start_sound_job([text, everywhere, pick = command == "/nitro_sound"] {
                            return ingame::sound_search_report(text, everywhere, pick, pick ? text : std::string{},
                                                               pick ? standalone::state_directory() / "tnt_sound.txt" : std::filesystem::path{});
                        });
                    print_console("Searching the game's sound banks in the background (a few seconds, longer with \"all\")...");
                }
            }
            else if (command == "/sound_play")
                print_console(argument.empty() ? std::string("Use /sound_play part of a name from /sounds.")
                              : ingame::play_game_sound(argument) ? "Playing the game sound matching \"" + argument + "\"."
                                                                  : "No game sound matching \"" + argument + "\" was played in this level yet.");
            else if (command == "/nitro_spawn")
            {
                if (box_markers)
                {
                    box_markers->request_nitro_spawn();
                    print_console("Nitro spawn requested (experimental): the game spawns a Nitro next to Crash. Restart the level to undo.");
                }
                else
                    print_console("Nitro spawn needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/ice_watch")
            {
                if (box_markers)
                {
                    const bool on = argument != "off";
                    box_markers->ice_watch(on);
                    print_console(on ? "Ice watch on: walk on normal ground for a few seconds, then on ice for a few seconds, then /ice_watch off. The log lists what the game changes for ice."
                                     : "Ice watch off. The comparison is in the log.");
                }
                else
                    print_console("Ice watch needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/move_probe")
            {
                if (box_markers)
                {
                    const bool on = argument != "off";
                    box_markers->move_probe(on);
                    print_console(on ? "Move probe on: close the console, run around in all directions for half a minute (turns, stops, starts, jumps), then /move_probe off."
                                     : "Move probe off. The results are in the log.");
                }
                else
                    print_console("Move probe needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/ice_test")
            {
                if (box_markers)
                {
                    const unsigned mode = argument == "slide" ? 1u : argument == "floor" ? 2u : argument == "swap" ? 3u : argument == "push" ? 4u : argument == "const" ? 5u : argument == "copy" ? 6u : argument == "name" ? 7u : argument == "copyall" ? 8u : argument == "which" ? 9u : argument == "kick" ? 10u : argument == "track" ? 11u : argument == "who" ? 12u : argument == "whopage" ? 13u : argument == "contact" ? 14u : 0u;
                    if (!mode && argument != "off")
                        print_console("Use /ice_test contact, who, whopage, track, kick, which, copyall, swap, name, const, copy, push, slide, floor or off.");
                    box_markers->ice_test(mode);
                    print_console(mode == 1 ? "Ice test: slide on. Close the console and run, turn and stop on normal ground. /ice_test off when done."
                                  : mode == 2 ? "Ice test: floor on. Close the console and run, turn and stop on normal ground. /ice_test off when done."
                                  : mode == 3 ? "Ice test: swap on. Close the console, wait a few seconds, then walk onto normal ground (and off and on again). /ice_test off when done."
                                  : mode == 4 ? "Ice test: push on. Close the console and run, turn and stop on normal ground. /ice_test off when done."
                                  : mode == 5 ? "Ice test: const on. Walk from the ice onto the snow and back a few times. /ice_test off when done."
                                  : mode == 6 ? "Ice test: copy on. Walk from the ice onto the snow and back a few times. /ice_test off when done."
                                  : mode == 7 ? "Ice test: name on. Walk from the ice onto the snow and back a few times. /ice_test off when done."
                                  : mode == 8 ? "Ice test: copyall on. Walk from the ice onto the snow and back a few times. /ice_test off when done."
                                  : mode == 9 ? "Ice test: which on. Walk ice -> snow -> ice -> snow ... and follow the messages. /ice_test off when done."
                                  : mode == 10 ? "Ice test: kick on. Stand still on normal ground and follow the messages. /ice_test off when done."
                                  : mode == 11 ? "Ice test: track on. Stand still on the snow and follow the messages. /ice_test off when done."
                                  : mode == 12 || mode == 13 ? "Ice test: who on. Walk onto the ice, then back onto the snow. /ice_test off when done."
                                  : mode == 14 ? "Ice test: contact on. Walk ice -> snow -> ice -> snow, stopping a second on each. /ice_test off when done."
                                              : "Ice test off.");
                }
                else
                    print_console("Ice test needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/ice_find")
            {
                if (box_markers)
                {
                    box_markers->request_ice_find();
                    print_console("Ice find: searching game memory for where the ice surface is referenced. The results appear in the log after a few seconds.");
                }
                else
                    print_console("Ice find needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/ice_probe")
            {
                if (box_markers)
                {
                    box_markers->request_ice_probe();
                    print_console("Ice probe requested (read-only). Run it standing still on normal ground, then again standing on ice.");
                }
                else
                    print_console("Ice probe needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/nitro_scan")
            {
                if (box_markers)
                {
                    box_markers->request_nitro_scan();
                    print_console("Nitro scan requested (read-only, changes nothing). Restart the level first if you already ran /nitro_test.");
                }
                else
                    print_console("Nitro scan needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/nitro_test")
            {
                if (box_markers)
                {
                    box_markers->request_nitro_test();
                    print_console("Nitro test requested (experimental): the nearest Nitro moves next to Crash. Restart the level to undo.");
                }
                else
                    print_console("Nitro test needs the in-game adapter. Load into a level first.");
            }
            else if (command == "/markers" || command == "/crashsanity")
            {
                if (box_markers)
                    print_console(command + ": " +
                                  (box_markers->toggle(command.substr(1), argument.empty() ? "toggle" : argument)
                                       ? "on"
                                       : "off"));
            }
            else if (command == "/clear")
                feed_history.clear();
            else if (!text.empty() && text[0] == '/')
                print_console("Unknown local command. Use /help.");
            else if (AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated)
                AP_Say(text);
            else
                print_console("Chatting requires an authenticated AP connection. Use /connect.");
        }
        catch (const std::exception &e)
        {
            print_console(std::string("Console: ") + e.what());
        }
    }
}

void ClientRuntime::process_packets(AP_ConnectionStatus status)
{
    if (status != AP_ConnectionStatus::Authenticated)
        popups_ready = false;
    std::deque<std::string> packets;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        packets.swap(ap_packets);
        ap_packet_bytes = 0;
        if (!ap_packet_error.empty())
        {
            session_error = ap_packet_error;
            session_failed = true;
        }
    }
    if (!session_failed)
        try
        {
            for (const auto &raw : packets)
            {
                Json::Value packet;
                Json::Reader reader;
                if (!reader.parse(raw, packet) || !packet.isArray() || packet.empty() || packet.size() > 64)
                    throw std::runtime_error("Invalid raw AP packet");
                for (const auto &message : packet)
                {
                    const auto command = message["cmd"].asString();
                    startup_connection.packet(command, message.get("fallback_pending", true).asBool());
                    if (command == "NativeTransport")
                        transport_connected_at = std::chrono::steady_clock::now();
                    else if (command == "NativeDisconnected")
                    {
                        std::string notice = "AP disconnected";
                        if (transport_connected_at != std::chrono::steady_clock::time_point{})
                            notice += " after " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - transport_connected_at).count()) + "s";
                        const auto code = message["close_code"].asUInt();
                        if (code)
                            notice += " (" + std::string(message["remote"].asBool() ? "server" : "client") +
                                      " close " + std::to_string(code) + ")";
                        const auto reason = message["reason"].asString();
                        print_console(notice + ": " + (reason.empty() ? "No close reason supplied." : reason));
                        transport_connected_at = {};
                        if (message.get("retry_limit", false).asBool())
                        {
                            startup_connection.show = true;
                            print_console("Connection stopped after 5 failed attempts. Correct /server and /slot, then use /connect to try again.");
                        }
                    }
                    else if (command == "InvalidPacket")
                        print_console("AP rejected " + message["original_cmd"].asString() + ": " + message["text"].asString());
                    if ((command == "Connected" || command == "RoomUpdate") && message["players"].isArray())
                        for (const auto &player : message["players"])
                            if (player["slot"].isInt() && player["alias"].isString())
                                player_aliases[player["slot"].asInt()] = player["alias"].asString();
                    if (command == "RoomInfo")
                    {
                        if (!message["seed_name"].isString() || message["seed_name"].asString().empty())
                            throw std::runtime_error("Invalid AP seed identity");
                        observed_seed = message["seed_name"].asString();
                        server_matches_session = session.ready() && session.seed() == observed_seed;
                    }
                    else if (command == "Connected")
                    {
                        names_loaded = false;
                        popups_ready = false;
                        receipt_history_ready = false;
                        fresh_receipts.clear();
                        deferred_receipts.clear();
                        deferred_receipt_bytes = 0;
                        submitted_checks.clear();
                        const auto &slot_data = message["slot_data"];
                        if (!message["team"].isInt() || !message["slot"].isInt() || !slot_data["protocol"].isInt() ||
                            slot_data["protocol"].asInt() != seed_protocol)
                            throw std::runtime_error("Connected identity/protocol differs from the configured room");
                        if (cloud && cloud->game_pid() && session.ready() &&
                            (session.seed() != observed_seed || session.team() != message["team"].asInt() ||
                             session.slot() != message["slot"].asInt()))
                            throw std::runtime_error(
                                "AP identity changed. Close NST before switching its local save profile");
                        SeedConfig validated_seed(slot_data);
                        session.open(session_directory, observed_seed, message["team"].asInt(), message["slot"].asInt(),
                                     slot_data);
                        seed = std::move(validated_seed);
                        session.connected(message["checked_locations"], !cloud || !cloud->game_pid());
                        session.remember(session_directory, session_profile);
                        const auto scouts = session.scout_locations();
                        if (!scouts.empty())
                            AP_SendLocationScouts(scouts, 0);
                        save_connection();
                        if (cloud && cloud->key() != session.storage_key())
                            throw std::runtime_error(
                                "AP seed changed. Close NST and restart it through Steam for the new save profile");
                        if (!cloud)
                            cloud = std::make_unique<CloudSaves>(session);
                        save_dir = cloud->save_directory();
                        cloud->connected();
                        server_matches_session = true;
                        std::cout << "Session bound: " << observed_seed << ", team " << message["team"].asInt()
                                  << ", slot " << message["slot"].asInt() << std::endl;
                        // the YAML's skin, once per seed; it shows from the next game start
                        if (slot_data["crash_skin"].isString())
                            if (const auto note = skins::apply_seed_choice(observed_seed + "/" + std::to_string(message["slot"].asInt()),
                                                                           slot_data["crash_skin"].asString());
                                !note.empty())
                                print_console(note);
                    }
                    else if (command == "ReceivedItems")
                    {
                        if (cloud && (!cloud->ready() || !deferred_receipts.empty()))
                        {
                            deferred_receipt_bytes += Json::FastWriter().write(message).size();
                            if (deferred_receipts.size() >= 256 || deferred_receipt_bytes > 16 * 1024 * 1024)
                                throw std::runtime_error("Too much received-item data before AP progress sync");
                            deferred_receipts.push_back(message);
                        }
                        else
                            receive_items(message);
                    }
                    else if (command == "LocationInfo" && session.ready())
                    {
                        session.scouted(message["locations"]);
                    }
                    else if (command == "RoomUpdate" && message.isMember("checked_locations") && session.ready())
                    {
                        session.acknowledge(message["checked_locations"]);
                    }
                    else if (command == "SetReply" && cloud)
                    {
                        cloud->receive(message);
                    }
                }
            }
            if (cloud && cloud->ready())
            {
                for (const auto &message : deferred_receipts)
                    receive_items(message);
                deferred_receipts.clear();
                deferred_receipt_bytes = 0;
            }
            session.flush();
        }
        catch (const std::exception &error)
        {
            session_error = error.what();
            session_failed = true;
            server_matches_session = false;
            fresh_receipts.clear();
            popups_ready = false;
        }
}
