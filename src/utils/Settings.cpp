#include "utils/Settings.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

using nlohmann::json;

Settings& Settings::get() {
    static Settings instance;
    return instance;
}

void Settings::resetToDefaults() {
    *this = Settings{};
}

void Settings::clampAll() {
    masterVolume = std::clamp(masterVolume, 0.0f, 1.0f);
    musicVolume = std::clamp(musicVolume, 0.0f, 1.0f);
    sfxVolume = std::clamp(sfxVolume, 0.0f, 1.0f);
    enemyTurnSpeed = std::clamp(enemyTurnSpeed, 0.5f, 3.0f);
    windowWidth = std::clamp(windowWidth, 960, 3840);
    windowHeight = std::clamp(windowHeight, 540, 2160);
    difficulty = std::clamp(difficulty, 0, 2);
}

void Settings::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // First run: keep the defaults and write them out so the file exists
        // and the player can see what is tunable.
        save(path);
        return;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[Settings] " << path << " is malformed (" << e.what()
                  << ") -> using defaults.\n";
        resetToDefaults();
        return;
    }

    fullscreen     = root.value("fullscreen", fullscreen);
    windowWidth    = root.value("windowWidth", windowWidth);
    windowHeight   = root.value("windowHeight", windowHeight);
    vsync          = root.value("vsync", vsync);
    masterVolume   = root.value("masterVolume", masterVolume);
    musicVolume    = root.value("musicVolume", musicVolume);
    sfxVolume      = root.value("sfxVolume", sfxVolume);
    screenShake    = root.value("screenShake", screenShake);
    enemyTurnSpeed = root.value("enemyTurnSpeed", enemyTurnSpeed);
    showBattleLog  = root.value("showBattleLog", showBattleLog);
    avatar         = root.value("avatar", avatar);
    difficulty     = root.value("difficulty", difficulty);

    clampAll();
}

void Settings::save(const std::string& path) const {
    json root;
    root["_comment"]     = "Sanctum settings. Deleting this file restores the defaults.";
    root["fullscreen"]   = fullscreen;
    root["windowWidth"]  = windowWidth;
    root["windowHeight"] = windowHeight;
    root["vsync"]        = vsync;
    root["masterVolume"] = masterVolume;
    root["musicVolume"]  = musicVolume;
    root["sfxVolume"]    = sfxVolume;
    root["screenShake"]  = screenShake;
    root["enemyTurnSpeed"] = enemyTurnSpeed;
    root["showBattleLog"]  = showBattleLog;
    root["avatar"]         = avatar;
    root["difficulty"]    = difficulty;

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Settings] Could not write " << path << " - preferences will not persist.\n";
        return;
    }
    file << root.dump(2) << "\n";
}
