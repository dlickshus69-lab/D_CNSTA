#pragma once
#include "session.h"
#include <chrono>
#include <functional>

// one active game per AP slot. Game saves stay local, only extra AP progress uses DataStorage
class CloudSaves
{
public:
    explicit CloudSaves(Session &session);
    void connected();
    void receive(const Json::Value &message);
    void tick(bool online, bool force = false);
    bool ready() const { return ready_ && !handshake_; }
    bool uploading() const { return !sent_.isNull(); }
    const std::filesystem::path &save_directory() const { return saves_; }
    const std::string &key() const { return key_; }
    DWORD game_pid() const;

private:
    Session &session_;
    std::filesystem::path root_, saves_;
    std::string key_, writer_, slot_hash_;
    Json::Value acknowledged_, pending_, sent_;
    bool ready_ = false, handshake_ = false, bound_ = false;
    std::chrono::steady_clock::time_point next_poll_{}, last_send_{};
    Json::Value initial() const;
    void validate(const Json::Value &value) const;
    void persist() const;
    void archive_local_saves();
    void adopt(const std::function<void(const std::filesystem::path &)> &bind);
    void send(const Json::Value &value, const std::string &operation, const std::string &key);
};
