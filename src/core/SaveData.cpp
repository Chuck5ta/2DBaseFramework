#include "core/SaveData.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <limits>

namespace core {

namespace {

bool isAbsolute(std::string_view p)
{
    return (!p.empty() && (p[0] == '/' || p[0] == '\\')) || (p.size() > 1 && p[1] == ':');
}

bool fileExists(const std::string& path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

// The whole file as text; false if it can't be read.
bool readFile(const std::string& path, std::string& out)
{
    size_t size = 0;
    void* bytes = SDL_LoadFile(path.c_str(), &size);
    if (!bytes)
        return false;
    out.assign(static_cast<const char*>(bytes), size);
    SDL_free(bytes);
    return true;
}

// Parses text that must be a JSON object; false (with `error` set) otherwise.
bool parseObject(const std::string& text, nlohmann::json& out, std::string& error)
{
    nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        error = "not valid JSON";
        return false;
    }
    if (!j.is_object()) {
        error = "not a JSON object";
        return false;
    }
    out = std::move(j);
    return true;
}

// Splits "a.b.c" into its parts.
template <class Fn>
void forEachPart(std::string_view key, Fn&& fn)
{
    size_t start = 0;
    while (true) {
        const size_t dot = key.find('.', start);
        const bool last = dot == std::string_view::npos;
        fn(key.substr(start, last ? std::string_view::npos : dot - start), last);
        if (last)
            break;
        start = dot + 1;
    }
}

} // namespace

SaveData::SaveData(std::string_view fileName)
    : fileName_(fileName)
    , data_(std::make_unique<nlohmann::json>(nlohmann::json::object()))
{
}

SaveData::~SaveData() = default;

const std::string& SaveData::path() const
{
    if (path_.empty())
        path_ = isAbsolute(fileName_) ? fileName_ : savePath(fileName_);
    return path_;
}
SaveData::SaveData(SaveData&&) noexcept = default;
SaveData& SaveData::operator=(SaveData&&) noexcept = default;

bool SaveData::load()
{
    *data_ = nlohmann::json::object();
    mainDamaged_ = false;
    const std::string backup = path() + ".bak";
    const bool haveMain = fileExists(path());
    if (!haveMain && !fileExists(backup))
        return false; // nothing saved yet: a normal first run

    std::string text, error;
    if (haveMain && readFile(path(), text) && parseObject(text, *data_, error))
        return true;
    mainDamaged_ = haveMain;
    if (haveMain)
        LOG_WARN("Save file %s is damaged (%s); trying the backup", path().c_str(),
                 error.empty() ? SDL_GetError() : error.c_str());

    error.clear();
    if (readFile(backup, text) && parseObject(text, *data_, error)) {
        LOG_WARN("Loaded %s from its backup", path().c_str());
        return true;
    }
    LOG_WARN("Could not load %s or its backup; starting with no saved data", path().c_str());
    return false;
}

bool SaveData::save()
{
    const std::string text = data_->dump(2) + "\n";
    const std::string temp = path() + ".tmp";
    const std::string backup = path() + ".bak";

    // Make sure the folder is there (it may have been deleted while the game ran).
    const size_t slash = path().find_last_of("/\\");
    if (slash != std::string::npos)
        SDL_CreateDirectory(path().substr(0, slash).c_str());

    // 1. Write everything to a temporary file, and make sure it reached the disk.
    SDL_IOStream* io = SDL_IOFromFile(temp.c_str(), "wb");
    bool ok = io != nullptr;
    if (ok) {
        ok = SDL_WriteIO(io, text.data(), text.size()) == text.size();
        ok = SDL_FlushIO(io) && ok;
        ok = SDL_CloseIO(io) && ok;
    }
    if (!ok) {
        LOG_WARN("Could not write %s: %s", temp.c_str(), SDL_GetError());
        SDL_RemovePath(temp.c_str());
        return false;
    }
    // 2. Keep the previous version as the backup, 3. put the new one in place. A crash
    // at any moment leaves either the old file, the new one, or the backup.
    // (Not if the file was found damaged: the backup is the good copy.)
    if (!mainDamaged_ && fileExists(path()) && !SDL_RenamePath(path().c_str(), backup.c_str()))
        LOG_WARN("Could not keep a backup of %s: %s", path().c_str(), SDL_GetError());
    if (!SDL_RenamePath(temp.c_str(), path().c_str())) {
        LOG_WARN("Could not replace %s: %s", path().c_str(), SDL_GetError());
        return false;
    }
    mainDamaged_ = false;
    return true;
}

bool SaveData::exists() const
{
    return fileExists(path());
}

void SaveData::erase()
{
    SDL_RemovePath(path().c_str());
    SDL_RemovePath((path() + ".bak").c_str());
    *data_ = nlohmann::json::object();
}

const nlohmann::json* SaveData::find(std::string_view key) const
{
    const nlohmann::json* node = data_.get();
    bool found = true;
    forEachPart(key, [&](std::string_view part, bool) {
        if (!found || !node->is_object()) {
            found = false;
            return;
        }
        auto it = node->find(part);
        if (it == node->end()) {
            found = false;
            return;
        }
        node = &*it;
    });
    return found ? node : nullptr;
}

nlohmann::json& SaveData::slot(std::string_view key)
{
    nlohmann::json* node = data_.get();
    forEachPart(key, [&](std::string_view part, bool) {
        if (!node->is_object())
            *node = nlohmann::json::object(); // a value where a group is wanted: replace it
        node = &(*node)[std::string(part)];
    });
    return *node;
}

bool SaveData::has(std::string_view key) const
{
    return find(key) != nullptr;
}

bool SaveData::getBool(std::string_view key, bool fallback) const
{
    const nlohmann::json* v = find(key);
    return v && v->is_boolean() ? v->get<bool>() : fallback;
}

int SaveData::getInt(std::string_view key, int fallback) const
{
    const nlohmann::json* v = find(key);
    if (!v || !v->is_number())
        return fallback;
    const double d = v->get<double>(); // also accepts 3.0 typed by hand
    if (!std::isfinite(d) || d < double(std::numeric_limits<int>::min()) || d > double(std::numeric_limits<int>::max()))
        return fallback;
    return int(std::lround(d));
}

float SaveData::getFloat(std::string_view key, float fallback) const
{
    const nlohmann::json* v = find(key);
    return v && v->is_number() ? v->get<float>() : fallback;
}

std::string SaveData::getString(std::string_view key, std::string_view fallback) const
{
    const nlohmann::json* v = find(key);
    return v && v->is_string() ? v->get<std::string>() : std::string(fallback);
}

void SaveData::set(std::string_view key, bool value) { slot(key) = value; }
void SaveData::set(std::string_view key, int value) { slot(key) = value; }
void SaveData::set(std::string_view key, float value)
{
    // Stored as the shortest decimal that reads back as the same float: 0.7f is written
    // as 0.7, not 0.699999988079071.
    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), value);
    double d = double(value);
    std::from_chars(text, written.ptr, d);
    slot(key) = d;
}
void SaveData::set(std::string_view key, double value) { slot(key) = value; }
void SaveData::set(std::string_view key, const char* value) { slot(key) = std::string(value ? value : ""); }
void SaveData::set(std::string_view key, std::string_view value) { slot(key) = std::string(value); }

void SaveData::remove(std::string_view key)
{
    const size_t dot = key.rfind('.');
    nlohmann::json* parent = data_.get();
    if (dot != std::string_view::npos) {
        if (!has(key.substr(0, dot)))
            return;
        parent = &slot(key.substr(0, dot)); // exists, so nothing is created
        if (!parent->is_object())
            return;
    }
    parent->erase(std::string(dot == std::string_view::npos ? key : key.substr(dot + 1)));
}

void SaveData::clear()
{
    *data_ = nlohmann::json::object();
}

std::string SaveData::toString() const
{
    return data_->dump(2);
}

bool SaveData::parse(std::string_view text)
{
    std::string error;
    nlohmann::json j;
    if (!parseObject(std::string(text), j, error))
        return false;
    *data_ = std::move(j);
    return true;
}

} // namespace core
