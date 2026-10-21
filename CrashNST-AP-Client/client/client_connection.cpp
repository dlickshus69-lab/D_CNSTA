#include "client_runtime.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include "trap_rules.h"
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
                if (kind == std::end(kinds))
                    print_console("Use /trap_test slow, slippery, darkness, reverse, freeze, movelock or nitro. Close the console (F1) to play.");
                else if (!box_markers)
                    print_console("Trap test needs the in-game adapter. Load into a level first.");
                else if (kind->second == ingame::TrapNitro)
                {
                    box_markers->request_nitro_spawn();
                    print_console("Nitro Trap test. Not sent to AP. Close the console (F1); it spawns within 5 seconds once Crash has walked a few steps.");
                }
                else
                {
                    box_markers->apply_trap(kind->second, 30);
                    print_console(std::string(ingame::trap_names[kind->second]) + " test" + (ingame::timed_trap(kind->second) ? " for 30 seconds" : "") +
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
                    const unsigned mode = argument == "slide" ? 1u : argument == "floor" ? 2u : argument == "swap" ? 3u : argument == "push" ? 4u : 0u;
                    if (!mode && argument != "off")
                        print_console("Use /ice_test swap, /ice_test push, /ice_test slide, /ice_test floor or /ice_test off.");
                    box_markers->ice_test(mode);
                    print_console(mode == 1 ? "Ice test: slide on. Close the console and run, turn and stop on normal ground. /ice_test off when done."
                                  : mode == 2 ? "Ice test: floor on. Close the console and run, turn and stop on normal ground. /ice_test off when done."
                                  : mode == 3 ? "Ice test: swap on. Close the console, wait a few seconds, then walk onto normal ground (and off and on again). /ice_test off when done."
                                  : mode == 4 ? "Ice test: push on. Close the console and run, turn and stop on normal ground. /ice_test off when done."
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
