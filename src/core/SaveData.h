#pragma once

// A save file: named values in a JSON file in the player's save folder.
//
//   core::SaveData save("save.json");          // a member of your game
//   save.load();                               // in onInit (a missing file is fine)
//   bestScore_ = save.getInt("bestScore");     // 0 the first time
//   ...
//   save.set("bestScore", bestScore_);
//   save.set("stats.rounds", rounds);          // dots make nested groups
//   save.save();                               // writes the file safely
//
// The file is plain, readable JSON:  { "bestScore": 42, "stats": { "rounds": 7 } }
//
// Writing is crash-safe: the new data goes to a temporary file first, which then
// replaces the old one, and the previous version is kept as <name>.bak. If the file is
// damaged (power cut, a bad hand edit), load() falls back to the backup.
//
// Settings the player chooses (volume, controls...) belong in app.settings(), which is
// one of these that the framework loads and saves for you. Use your own SaveData for
// progress: scores, unlocked levels, save slots ("slot1.json", "slot2.json"...).

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace core {

class SaveData {
public:
    // `fileName` is inside saveFolder() (core/Paths.h); an absolute path is used as is.
    explicit SaveData(std::string_view fileName = "save.json");
    ~SaveData();
    SaveData(SaveData&&) noexcept;
    SaveData& operator=(SaveData&&) noexcept;

    // Reads the file, replacing the values in memory. Returns false if there is no file
    // yet or it can't be read even from the backup: the data is then empty, so every
    // get returns its fallback.
    bool load();
    // Writes the file (creating the folder if needed). Returns false on failure, with a
    // warning in the log; the values in memory are kept.
    bool save();
    bool exists() const;  // the file is on disk
    void erase();         // deletes the file and its backup, and empties the data ("new game")
    const std::string& path() const; // the full path of the file

    // --- Values -------------------------------------------------------------------
    // Keys can use dots for groups: "audio.music" is "music" inside "audio". A missing
    // key, or a value of the wrong type, gives the fallback.
    bool has(std::string_view key) const;
    bool getBool(std::string_view key, bool fallback = false) const;
    int getInt(std::string_view key, int fallback = 0) const;
    float getFloat(std::string_view key, float fallback = 0.0f) const;
    std::string getString(std::string_view key, std::string_view fallback = {}) const;

    void set(std::string_view key, bool value);
    void set(std::string_view key, int value);
    void set(std::string_view key, float value);
    void set(std::string_view key, double value);
    void set(std::string_view key, const char* value);
    void set(std::string_view key, std::string_view value);
    void remove(std::string_view key);
    void clear(); // empties the data in memory (save() then writes "{}")

    // The whole document, for lists and anything else (include <nlohmann/json.hpp>):
    //   save.json()["unlocked"] = {1, 2, 5};
    nlohmann::json& json() { return *data_; }
    const nlohmann::json& json() const { return *data_; }

    // The data as JSON text, and back (tests, cloud APIs). parse returns false and leaves
    // the data unchanged if `text` isn't a JSON object.
    std::string toString() const;
    bool parse(std::string_view text);

private:
    const nlohmann::json* find(std::string_view key) const;
    nlohmann::json& slot(std::string_view key); // creates the groups on the way

    std::string fileName_;
    mutable std::string path_; // worked out on first use, after Application set the folder names
    std::unique_ptr<nlohmann::json> data_;
    bool mainDamaged_ = false; // load() found the file damaged: don't make it the backup
};

} // namespace core
