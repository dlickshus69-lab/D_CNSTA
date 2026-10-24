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
#include "crash_colors.h"
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
                    "/hubdistance_c1 /hubdistance_c2 /hubdistance_c3 0..100000 | /tint_opacity 0..1 | /slow_tilt 0.2..1 | /nitro_volume 0..100 | /boss name (test) | "
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
                              "/slow_tilt 0.2..1 sets where it starts.");
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
            else if (command == "/colors")
            {
                // /colors, /colors fur blue, /colors reset: Crash's colors, from the next game start
                for (const auto &line : crash_colors::command(argument))
                    print_console(line);
            }
            else if (command == "/markers" || command == "/crashsanity")
            {
                if (box_markers)
                    print_console(command + ": " +
                                  (box_markers->toggle(command.substr(1), argument.empty() ? "toggle" : argument)
                                       ? "on"
                                       : "off"));
            }
            else if (command == "/boss")
            {
                // test (Harder bosses): the next level entrance loads this boss fight instead. Counts as beating it in
                // this seed (the game saves it), so use a test seed
                static const std::vector<std::pair<std::string, std::vector<std::string>>> bosses{
                    {"B101", {"papu", "papupapu"}},       {"B102", {"ripper1", "ripperroo1"}}, {"B103", {"koala", "koalakong"}},
                    {"B104", {"pinstripe"}},              {"B105", {"brio"}},                  {"B106", {"cortex1"}},
                    {"B201", {"ripper2", "ripperroo2"}},  {"B202", {"komodo", "komodobros"}},  {"B203", {"tiny2"}},
                    {"B204", {"ngin2"}},                  {"B205", {"cortex2"}},               {"B301", {"tiny3"}},
                    {"B302", {"dingodile"}},              {"B303", {"tropy", "ntropy"}},       {"B304", {"ngin3"}},
                    {"B305", {"cortex3"}}};
                std::string wanted;
                for (auto c : argument)
                    if (std::isalnum(static_cast<unsigned char>(c)))
                        wanted += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string key;
                for (const auto &[k, names] : bosses)
                {
                    std::string lower_key = k;
                    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (wanted == lower_key || std::find(names.begin(), names.end(), wanted) != names.end())
                        key = k;
                }
                if (wanted == "off")
                {
                    boss_wanted.clear();
                    print_console("Boss test off: entrances lead to their levels again.");
                }
                else if (key.empty())
                    print_console("Boss test: /boss papu, ripper1, koala, pinstripe, brio, cortex1, ripper2, komodo, tiny2, ngin2, cortex2, tiny3, "
                                  "dingodile, tropy, ngin3 or cortex3 (or a key like B204), /boss off. The next level you enter loads that boss "
                                  "fight. Beating it counts in this seed, so use a test seed.");
                else
                {
                    boss_wanted = key;
                    print_console("Boss test: the next level you enter loads " + key + ". Beating it counts in this seed (use a test seed).");
                }
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
                        // a colored Crash made by an older client is made again with this one's colors
                        for (const auto &line : crash_colors::keep_current())
                            print_console(line);
                        // ... and its Crash colors (made from the game's own Crash.pak, also from the next game start)
                        if (slot_data["crash_colors"].isObject())
                        {
                            crash_colors::Choice yaml;
                            for (int p = 0; p < crash_colors::PartCount; ++p)
                            {
                                const auto &value = slot_data["crash_colors"][crash_colors::part_name(static_cast<crash_colors::Part>(p))];
                                yaml[p] = value.isString() ? value.asString() : "default";
                            }
                            for (const auto &line : crash_colors::apply_seed_choice(observed_seed + "/" + std::to_string(message["slot"].asInt()), yaml,
                                                                                  slot_data["crash_skin"].isString() ? slot_data["crash_skin"].asString() : "default"))
                                print_console(line);
                        }
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
