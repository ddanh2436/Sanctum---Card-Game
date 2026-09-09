#pragma once

#include <string>

/**
 * @brief Player preferences, persisted to settings.json beside the executable.
 *
 * Every field has a sane default, so a missing or corrupt file simply means
 * "first run" rather than an error. Values are clamped on load, which keeps a
 * hand-edited file from putting the game into an unusable state.
 */
struct Settings {
    // --- display ---
    bool fullscreen = true;          // the game opens fullscreen out of the box
    int windowWidth = 1280;          // used when windowed
    int windowHeight = 720;
    bool vsync = true;

    // --- audio, all 0.0 - 1.0 ---
    float masterVolume = 0.80f;
    float musicVolume = 0.70f;
    float sfxVolume = 0.90f;

    // --- gameplay feel ---
    bool screenShake = true;
    /// Multiplier on the pause between enemy actions. Higher is faster.
    float enemyTurnSpeed = 1.0f;
    bool showBattleLog = true;

    /// Chosen commander portrait, as a bare filename inside assets/avatars/.
    /// Empty means "use whatever suits my primary core", which is also what a
    /// pick falls back to once its file is gone.
    std::string avatar;

    /// The mix an actual sound should play at, 0 - 100 for SFML.
    float musicMix() const { return masterVolume * musicVolume * 100.0f; }
    float sfxMix() const { return masterVolume * sfxVolume * 100.0f; }

    static Settings& get();

    /// Read settings.json if present. Safe to call more than once.
    void load(const std::string& path = "settings.json");
    /// Write the current values back out. Failure is logged, never fatal.
    void save(const std::string& path = "settings.json") const;

    void resetToDefaults();
    void clampAll();
};
