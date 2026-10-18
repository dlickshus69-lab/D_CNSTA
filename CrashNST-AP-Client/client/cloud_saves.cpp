#include "cloud_saves.h"
#include "collectibles.h"
#include "Archipelago.h"
#include "standalone.h"
#include <wincrypt.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
    constexpr char executable_hash[] = "170c61d37fb5277dd94b3e41f7a918f227b76bc67aa338f5c97f63ac317b6af3";
    constexpr wchar_t save_folder[] = L"Crash Bandicoot(TM) N. Sane Trilogy";
    constexpr unsigned max_file = 1024 * 1024, max_total = 4 * 1024 * 1024;
    std::string json(const Json::Value &value)
    {
        Json::StreamWriterBuilder w;
        w["indentation"] = "";
        return Json::writeString(w, value);
    }
    bool same(const Json::Value &a, const Json::Value &b) { return json(a) == json(b); }
    std::string new_writer()
    {
        static std::atomic<unsigned> sequence{0};
        return state_digest(std::to_string(GetCurrentProcessId()) + ':' + std::to_string(GetTickCount64()) + ':' + std::to_string(++sequence));
    }
    std::vector<std::string> filenames()
    {
        std::vector<std::string> result{"SAVE_OPTIONS.sav"};
        for (unsigned i = 0; i < 12; ++i)
            result.push_back("SAVE_" + std::to_string(i) + ".sav");
        return result;
    }
    void no_reparse(const std::filesystem::path &path)
    {
        for (auto p = std::filesystem::absolute(path); !p.empty();)
        {
            const auto attributes = GetFileAttributesW(p.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("AP save profile contains a link/junction. Refusing file access");
            const auto parent = p.parent_path();
            if (parent == p)
                break;
            p = parent;
        }
    }
    std::string encode(const std::vector<std::uint8_t> &bytes)
    {
        DWORD size = 0;
        if (bytes.size() > max_file || !CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()),
                                                             CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size))
            throw std::runtime_error("Cannot encode game save");
        std::string result(size, '\0');
        if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                  result.data(), &size))
            throw std::runtime_error("Cannot encode game save");
        result.resize(size);
        if (!result.empty() && result.back() == '\0')
            result.pop_back();
        return result;
    }
    std::vector<std::uint8_t> decode(const Json::Value &entry)
    {
        if (!entry.isObject() || !entry["base64"].isString() || !entry["sha256"].isString())
            throw std::runtime_error("Invalid cloud save entry");
        const auto text = entry["base64"].asString();
        DWORD size = 0;
        if (text.size() > max_file * 4 / 3 + 4 || !CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr) || size < 512 || size > max_file)
            throw std::runtime_error("Invalid cloud save encoding/size");
        std::vector<std::uint8_t> bytes(size);
        if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, bytes.data(), &size, nullptr, nullptr) ||
            size != bytes.size() || encode(bytes) != text ||
            state_digest(std::string(bytes.begin(), bytes.end())) != entry["sha256"].asString())
            throw std::runtime_error("Cloud game save failed integrity verification");
        return bytes;
    }
    Json::Value capture(const std::filesystem::path &directory)
    {
        Json::Value result(Json::objectValue);
        std::size_t total = 0;
        const auto allowed = filenames();
        no_reparse(directory);
        for (const auto &name : filenames())
        {
            const auto path = directory / name;
            if (!std::filesystem::exists(path))
                continue;
            no_reparse(path);
            const auto bytes = save_probe::read_file(path);
            total += bytes.size();
            if (bytes.size() > max_file || total > max_total)
                throw std::runtime_error("Game save snapshot exceeds its bound");
            if (name == "SAVE_OPTIONS.sav")
            {
                for (const auto &[campaign, file] : save_probe::recent_slots(bytes))
                    if (std::find(allowed.begin(), allowed.end(), file) == allowed.end())
                        throw std::runtime_error("Game options refer to an unsupported save slot");
            }
            else
                save_probe::zones(bytes);
            result[name]["base64"] = encode(bytes);
            result[name]["sha256"] = state_digest(std::string(bytes.begin(), bytes.end()));
        }
        return result;
    }
    void write_binary(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
    {
        no_reparse(path);
        auto temporary = path;
        temporary += L".ap-tmp";
        no_reparse(temporary);
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot create AP game save replacement");
        DWORD written = 0;
        const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                        written == bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot persist AP game save replacement");
    }
    void inherit_display(const std::filesystem::path &source, const std::filesystem::path &saves, const std::filesystem::path &marker)
    {
        no_reparse(marker);
        if (std::filesystem::exists(marker))
            return;
        no_reparse(saves);
        std::filesystem::create_directories(saves);
        Json::Value inherited(Json::objectValue);
        for (const char *name : {"GraphicsOptions.sav", "WindowData.sav"})
        {
            const auto path = source / name;
            if (!std::filesystem::exists(path))
                continue;
            no_reparse(path);
            const auto bytes = save_probe::read_file(path);
            if (bytes.size() > max_file)
                throw std::runtime_error("Display preferences exceed their size bound");
            const auto target = saves / name;
            no_reparse(target);
            if (std::filesystem::exists(target))
            {
                auto backup = target;
                backup += L".before-inherit";
                no_reparse(backup);
                std::filesystem::copy_file(target, backup, std::filesystem::copy_options::skip_existing);
            }
            write_binary(target, bytes);
            inherited[name] = state_digest(std::string(bytes.begin(), bytes.end()));
        }
        write_state_json(marker, inherited);
    }
}

CloudSaves::CloudSaves(Session &session) : session_(session)
{
    root_ = session.profile_directory();
    saves_ = root_ / "AppData" / save_folder;
    key_ = session.storage_key();
    slot_hash_ = state_digest(json(session.slot_data()));
    writer_ = new_writer();
    no_reparse(root_);
    std::filesystem::create_directories(root_);
    const auto cache = root_ / "cloud-cache.json";
    if (std::filesystem::exists(cache))
    {
        no_reparse(cache);
        const auto state = read_state_json(cache);
        if (session_.server_reset_pending())
        {
            write_state_json(root_ / ("cloud-cache-before-server-reset-" + writer_ + ".json"), state);
            return;
        }
        acknowledged_ = state["acknowledged"];
        pending_ = state["pending"];
        if (!acknowledged_.isNull())
            validate(acknowledged_);
        if (!pending_.isNull())
            validate(pending_);
        ready_ = !acknowledged_.isNull();
        if (ready_)
            session_.restore_cloud_progress(pending_.isNull() ? acknowledged_["progress"] : pending_["progress"]);
        // dont discard valid AP working files saved after the last successful upload
        if (ready_ && pending_.isNull())
        {
            auto local = acknowledged_;
            const auto files = capture(saves_);
            for (const auto &name : files.getMemberNames())
                local["files"][name] = files[name];
            if (!same(local["files"], acknowledged_["files"]))
            {
                local["progress"] = session_.cloud_progress();
                local["generation"] = Json::UInt64(acknowledged_["generation"].asUInt64() + 1);
                local["writer"] = writer_;
                validate(local);
                pending_ = local;
                persist();
            }
        }
    }
}
Json::Value CloudSaves::initial() const
{
    Json::Value value(Json::objectValue);
    value["schema"] = 1;
    value["seed"] = session_.seed();
    value["team"] = session_.team();
    value["slot"] = session_.slot();
    value["executable_sha256"] = executable_hash;
    value["slot_data_sha256"] = slot_hash_;
    value["generation"] = Json::UInt64(0);
    value["writer"] = "initial";
    value["epoch"] = writer_; // newly inserted default identifies a new server save
    value["files"] = Json::Value(Json::objectValue);
    value["progress"]["earned"] = Json::Value(Json::arrayValue);
    value["progress"]["local_collectibles"] = Json::Value(Json::arrayValue);
    value["progress"]["filler_cursor"] = 0u;
    return value;
}
void CloudSaves::validate(const Json::Value &value) const
{
    if (!value.isObject() || value["schema"] != 1 || value["seed"] != session_.seed() || value["team"] != session_.team() ||
        value["slot"] != session_.slot() || value["executable_sha256"] != executable_hash || value["slot_data_sha256"] != slot_hash_ ||
        !value["generation"].isUInt64() || value["generation"].asUInt64() > 1000000000 ||
        !value["writer"].isString() || value["writer"].asString().size() > 128 || !value["files"].isObject() ||
        !value["progress"].isObject() || !value["progress"]["earned"].isArray() || value["progress"]["earned"].size() > 100000 ||
        json(value).size() > 8 * 1024 * 1024)
        throw std::runtime_error("Cloud snapshot identity/schema/size does not match this AP slot");
    if (!value["epoch"].isString() || value["epoch"].asString().size() != 64)
        throw std::runtime_error("Invalid cloud save epoch");
    const auto &local_collectibles = value["progress"]["local_collectibles"];
    if (!local_collectibles.isArray() || local_collectibles.size() > 4 * std::size(ingame::collectible_specs))
        throw std::runtime_error("Invalid cloud local collectible list");
    std::set<std::int64_t> local_collectible_ids;
    for (const auto &id : local_collectibles)
    {
        if (!id.isInt64() || !local_collectible_ids.insert(id.asInt64()).second)
            throw std::runtime_error("Invalid cloud local collectible identity");
        ingame::local_collectible_item(session_.slot_data(), id.asInt64());
    }
    const auto &cursor = value["progress"]["filler_cursor"];
    if (!cursor.isUInt() || cursor.asUInt() > 100000)
        throw std::runtime_error("Invalid cloud filler receipt cursor");
    std::size_t total = 0;
    const auto allowed = filenames();
    for (const auto &name : value["files"].getMemberNames())
    {
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            throw std::runtime_error("Unsafe cloud save filename");
        const auto bytes = decode(value["files"][name]);
        total += bytes.size();
        if (total > max_total)
            throw std::runtime_error("Cloud game saves exceed their bound");
        if (name == "SAVE_OPTIONS.sav")
        {
            for (const auto &[campaign, file] : save_probe::recent_slots(bytes))
                if (std::find(allowed.begin(), allowed.end(), file) == allowed.end())
                    throw std::runtime_error("Unsupported cloud save slot");
        }
        else
            save_probe::zones(bytes);
    }
    std::set<std::int64_t> unique;
    for (const auto &id : value["progress"]["earned"])
        if (!id.isInt64() || id.asInt64() <= 0 || !unique.insert(id.asInt64()).second ||
            !session_.slot_data()["checks"].isMember(std::to_string(id.asInt64())))
            throw std::runtime_error("Cloud progress includes an invalid AP location");
}
void CloudSaves::persist() const
{
    Json::Value cache(Json::objectValue);
    cache["acknowledged"] = acknowledged_;
    cache["pending"] = pending_;
    no_reparse(root_ / "cloud-cache.json");
    write_state_json(root_ / "cloud-cache.json", cache);
}
void CloudSaves::restore(const Json::Value &value)
{
    validate(value);
    no_reparse(saves_);
    std::filesystem::create_directories(saves_);
    static unsigned backup_sequence = 0;
    const auto backup = root_ / "backups" / (std::to_string(GetTickCount64()) + '-' + std::to_string(GetCurrentProcessId()) + '-' + std::to_string(++backup_sequence));
    // restore only with NST stopped. keep any replaced AP working files for recovery.
    for (const auto &name : filenames())
    {
        const auto path = saves_ / name;
        if (!std::filesystem::exists(path))
            continue;
        no_reparse(path);
        no_reparse(backup);
        std::filesystem::create_directories(backup);
        std::filesystem::copy_file(path, backup / name, std::filesystem::copy_options::none);
    }
    for (const auto &name : filenames())
    {
        const auto path = saves_ / name;
        if (value["files"].isMember(name))
            write_binary(path, decode(value["files"][name]));
        else if (std::filesystem::exists(path) && !DeleteFileW(path.c_str()))
            throw std::runtime_error("Cannot remove obsolete AP working save");
    }
    session_.restore_cloud_progress(value["progress"]);
}
void CloudSaves::send(const Json::Value &value, const std::string &operation, const std::string &key)
{
    std::string serialized = json(value);
    AP_SetServerDataRequest request;
    request.key = key;
    request.type = AP_DataType::Raw;
    request.want_reply = true;
    request.default_value = &serialized;
    request.operations = {{operation, &serialized}};
    AP_SetServerData(&request);
}
void CloudSaves::connected()
{
    if (session_.server_reset_pending() && !acknowledged_.isNull())
    {
        if (bound_)
            throw std::runtime_error("Server save was reset. Close NST before restoring its authoritative state");
        Json::Value backup;
        backup["acknowledged"] = acknowledged_;
        backup["pending"] = pending_;
        write_state_json(root_ / ("cloud-cache-before-server-reset-" + writer_ + ".json"), backup);
        acknowledged_ = pending_ = Json::Value();
        ready_ = false;
    }
    writer_ = new_writer();
    handshake_ = true;
    sent_ = Json::Value();
    last_send_ = std::chrono::steady_clock::now();
    AP_SetNotify(key_, AP_DataType::Raw);
    send(initial(), "default", key_);
    std::cout << "AP cloud state requested. Gameplay waits for the server snapshot.\n"
              << std::flush;
}
void CloudSaves::receive(const Json::Value &message)
{
    if (message["cmd"] != "SetReply" || message["key"] != key_)
        return;
    const auto &value = message["value"];
    validate(value);
    if (handshake_)
    {
        const bool active = bound_;
        const bool recreated = value["epoch"] == writer_ && value["generation"].asUInt64() == 0 &&
                               !acknowledged_.isNull() && !same(value, acknowledged_);
        if (recreated)
        {
            if (active)
                throw std::runtime_error("Server save was reset. Close NST and reconnect to restore its authoritative state");
            session_.reset_server_state();
            Json::Value backup;
            backup["acknowledged"] = acknowledged_;
            backup["pending"] = pending_;
            write_state_json(root_ / ("cloud-cache-before-server-reset-" + writer_ + ".json"), backup);
            acknowledged_ = value;
            pending_ = Json::Value();
            std::cout << "Server save was reset. Cached AP progress archived. Restoring server state.\n"
                      << std::flush;
        }
        else if (same(value, pending_))
        {
            acknowledged_ = value;
            pending_ = Json::Value();
        }
        else if (!acknowledged_.isNull() && !same(value, acknowledged_) && !pending_.isNull())
        {
            write_state_json(root_ / "cloud-conflict.json", message);
            throw std::runtime_error("Server state changed while this profile has offline progress. Both copies were retained. Stop NST before resolving the cloud conflict");
        }
        else if (!same(value, acknowledged_))
        {
            if (active)
                throw std::runtime_error("Close the running AP game before restoring a different server snapshot");
            acknowledged_ = value;
            pending_ = Json::Value();
        }
        if (!active)
            restore(pending_.isNull() ? acknowledged_ : pending_);
        session_.restore_cloud_progress(value["progress"]);
        ready_ = true;
        handshake_ = false;
        persist();
        if (session_.server_reset_pending())
            session_.finish_server_reset();
        std::cout << "AP cloud state ready (generation " << value["generation"].asUInt64() << "). No manual save setup needed.\n"
                  << "AP game save folder: " << saves_.string() << '\n'
                  << std::flush;
        return;
    }
    if (!sent_.isNull() && same(value, sent_))
    {
        const auto &original = message["original_value"];
        if (!same(original, acknowledged_) && !same(original, sent_))
        {
            write_state_json(root_ / "cloud-conflict.json", message);
            throw std::runtime_error("Another client wrote this AP slot concurrently. Stop NST. The competing snapshot is retained in cloud-conflict.json");
        }
        acknowledged_ = sent_;
        if (same(pending_, sent_))
            pending_ = Json::Value();
        sent_ = Json::Value();
        persist();
        std::cout << "AP cloud snapshot acknowledged (generation " << acknowledged_["generation"].asUInt64() << ").\n"
                  << std::flush;
    }
    else if (!same(value, acknowledged_))
    {
        write_state_json(root_ / "cloud-conflict.json", message);
        throw std::runtime_error("Another client changed this AP slot's game-save state. Stop NST and reconnect. Local state was retained");
    }
}
DWORD CloudSaves::game_pid() const { return bound_ ? GetCurrentProcessId() : 0; }
void CloudSaves::adopt(const std::function<void(const std::filesystem::path &)> &bind)
{
    no_reparse(saves_);
    std::filesystem::create_directories(saves_);
    if ((saves_ / "SAVE_OPTIONS.sav").string().size() >= 250)
        throw std::runtime_error("AP profile path exceeds NST's save-path buffer");
    wchar_t appdata[32768]{};
    const auto count = GetEnvironmentVariableW(L"APPDATA", appdata, std::size(appdata));
    if (!count || count >= std::size(appdata))
        throw std::runtime_error("Cannot locate normal display preferences");
    inherit_display(std::filesystem::path(appdata) / save_folder, saves_, root_ / "display-inherited.json");
    bind(root_ / L"AppData");
    bound_ = true;
    std::cout << "Standalone AP save profile active. NST continues startup.\n"
              << std::flush;
}
void CloudSaves::tick(bool online, bool force)
{
    const auto now = std::chrono::steady_clock::now();
    if (handshake_ && online && now - last_send_ > std::chrono::seconds(10))
    {
        send(initial(), "default", key_);
        last_send_ = now;
    }
    if (!ready_ || (online && handshake_))
        return;
    if (!force && !bound_)
        adopt(standalone::bind_saves);
    if (!force && now < next_poll_)
        return;
    next_poll_ = now + std::chrono::seconds(1);
    auto value = acknowledged_;
    try
    {
        value["files"] = capture(saves_);
        read_error_.clear();
    }
    catch (const std::exception &e)
    {
        if (read_error_ != e.what())
            std::cerr << "AP cloud save waiting for a stable game-save snapshot: " << e.what() << '\n';
        read_error_ = e.what();
        return;
    }
    value["progress"] = session_.cloud_progress();
    if (same(value["files"], acknowledged_["files"]) && same(value["progress"], acknowledged_["progress"]))
    {
        if (sent_.isNull() && !pending_.isNull())
        {
            pending_ = Json::Value();
            persist();
        }
    }
    else
    {
        value["generation"] = Json::UInt64(acknowledged_["generation"].asUInt64() + 1);
        value["writer"] = writer_;
        if (!same(value, pending_))
        {
            pending_ = value;
            persist();
        }
    }
    if (online && !handshake_ && sent_.isNull() && !pending_.isNull())
    {
        validate(pending_);
        // one writer per slot. SetNotify validates.
        send(acknowledged_, "replace", key_ + ":backup:previous");
        sent_ = pending_;
        send(sent_, "replace", key_);
        last_send_ = now;
    }
    else if (online && !handshake_ && !sent_.isNull() && now - last_send_ > std::chrono::seconds(10))
        connected();
}
