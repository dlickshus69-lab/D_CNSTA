#pragma once
#include <cstdint>
#include <string>

inline unsigned hub_area(const std::string &key)
{
    if (key == "L226") return 3;
    if (key == "L227") return 4;
    if (key == "L331" || key == "L332") return 3;
    if (key.size() != 4 || (key[0] != 'L' && key[0] != 'B') || key[1] < '1' || key[1] > '3' ||
        key[2] < '0' || key[2] > '9' || key[3] < '0' || key[3] > '9')
        return 0;
    const auto number = unsigned(key[2] - '0') * 10 + unsigned(key[3] - '0');
    if (key[1] == '1')
        return key[0] == 'B' ? (number == 1 ? 1 : number <= 3 ? 2 : 3) : 1 + (number >= 9) + (number >= 16);
    if (key[0] == 'B') return number;
    // Bonus/DLC buttons in Crash 3 are outside the four electric gates
    if (key[1] == '3' && number > 25) return 1;
    return number ? (number - 1) / 5 + 1 : 0;
}

inline std::int64_t hub_access_item(unsigned campaign, unsigned area)
{
    return campaign >= 1 && campaign <= 3 && area >= 2 && area <= (campaign == 1 ? 3u : 5u)
               ? 0xcb0000 + 2300 + campaign * 10 + (campaign == 3 ? area : 2) : 0;
}

inline unsigned hub_item_limit(std::int64_t item)
{
    return item == hub_access_item(1, 2) ? 2 : item == hub_access_item(2, 2) ? 4 : 1;
}
