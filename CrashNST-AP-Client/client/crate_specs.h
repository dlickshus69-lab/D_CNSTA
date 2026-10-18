#pragma once
#include <array>
#include <cstdint>
#include <string>

struct BoxSpec
{
    const char *level;
    const char *name;
    std::int64_t id;
    std::array<unsigned char, 12> position;
    const char *event = nullptr;
    std::string event_name() const { return event ? event : std::string("box:") + name; }
};
