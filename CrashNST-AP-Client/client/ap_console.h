#pragma once
#include "ingame_protocol.h"
#include <vector>
#include <utility>
#include <string>
std::vector<ingame::Line> ap_messages(int own_slot, bool *drained = nullptr);
std::vector<ingame::Line> ap_received_message(std::int64_t item, int flags, int player, const std::string &alias,
                                              const std::string &location, int own_slot);
std::pair<std::vector<std::string>, std::vector<std::string>> ap_completion_names();
std::vector<ingame::Line> console_lines(const std::string &text, unsigned color = 0xffeeeeee);
