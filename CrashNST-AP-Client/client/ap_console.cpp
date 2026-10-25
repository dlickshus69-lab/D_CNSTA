#include "ap_console.h"
#include "Archipelago.h"
#include "standalone.h"
#include <json/json.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace
{
    // AP TextClient UI colors
    unsigned color(const AP_MessagePart &part, int own_slot)
    {
        if (part.type == AP_PlayerText)
            return part.player == own_slot && own_slot >= 0 ? 0xffee00ee : 0xffd2fafa;
        if (part.type == AP_LocationText)
            return 0xff7fff00;
        if (part.type == AP_ItemText)
        {
            const auto flags = part.flags;
            return flags & 1 ? 0xffef99af : flags & 2 ? 0xffe88b6d
                                        : flags & 4   ? 0xff7280fa
                                                      : 0xffeeee00;
        }
        return 0xffffffff;
    }
    void append(std::vector<ingame::Line> &lines, const std::string &text, unsigned rgba)
    {
        std::size_t begin = 0;
        while (begin < text.size())
        {
            if (lines.empty() || lines.back().count == ingame::max_spans)
                lines.emplace_back();
            const auto newline = text.find('\n', begin);
            auto size = std::min<std::size_t>(255, std::min(text.size(), newline) - begin);
            // split UTF-8 only between characters
            while (size && begin + size < text.size() && (static_cast<unsigned char>(text[begin + size]) & 0xc0) == 0x80)
                --size;
            if (!size && text[begin] != '\n')
                throw std::runtime_error("Invalid UTF-8 AP message");
            if (size)
            {
                auto &span = lines.back().spans[lines.back().count++];
                span.color = rgba;
                ingame::copy(span.text, text.substr(begin, size));
                begin += size;
            }
            if (begin == newline)
            {
                ++begin;
                lines.emplace_back();
            }
        }
    }
}
std::vector<ingame::Line> console_lines(const std::string &text, unsigned rgba)
{
    std::vector<ingame::Line> lines;
    append(lines, text, rgba);
    return lines;
}
std::vector<ingame::Line> ap_received_message(std::int64_t item, int flags, int player,
                                              const std::string &alias, const std::string &location, int own_slot)
{
    std::vector<AP_MessagePart> parts{{"Received "}, {AP_GetItemName(item), AP_ItemText, flags}, {" from "}, {alias, AP_PlayerText, 0, player}};
    if (!location.empty())
    {
        parts.push_back({" ("});
        parts.push_back({location, AP_LocationText});
        parts.push_back({")"});
    }
    std::vector<ingame::Line> lines;
    for (const auto &part : parts)
        append(lines, part.text, color(part, own_slot));
    return lines;
}
std::vector<ingame::Line> ap_messages(int own_slot, bool *drained)
{
    std::vector<ingame::Line> lines;
    if (drained)
        *drained = false;
    for (unsigned i = 0; i < 128; ++i)
    {
        std::unique_ptr<AP_Message, decltype(&AP_FreeMessage)> message(AP_PopLatestMessage(), AP_FreeMessage);
        if (!message)
        {
            if (drained)
                *drained = true;
            break;
        }
        lines.emplace_back();
        if (message->messageParts.empty())
            append(lines, message->text, 0xffffffff);
        else
            for (const auto &part : message->messageParts)
                append(lines, part.text, color(part, own_slot));
    }
    return lines;
}
std::pair<std::vector<std::string>, std::vector<std::string>> ap_completion_names()
{
    const auto path = standalone::state_directory() / "APCpp_datapkg.cache";
    if (!std::filesystem::exists(path))
        return {};
    if (std::filesystem::file_size(path) > 32 * 1024 * 1024)
        throw std::runtime_error("AP name cache exceeds 32 MiB");
    std::ifstream file(path);
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(file, root))
        throw std::runtime_error("AP name cache is incomplete");
    // a Testing build's seed is "... (Testing)"
    const auto &testing = root["games"]["Crash Bandicoot N. Sane Trilogy (Testing)"];
    const auto &game = testing.isObject() ? testing : root["games"]["Crash Bandicoot N. Sane Trilogy"];
    if (!game["item_name_to_id"].isObject() || !game["location_name_to_id"].isObject())
        return {};
    return {game["item_name_to_id"].getMemberNames(), game["location_name_to_id"].getMemberNames()};
}
