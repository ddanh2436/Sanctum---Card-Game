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

    /**
     * How hard the campaign pushes back.
     *
     * It scales the ENEMY's reactor and the repair you get between fights, not
     * the rules: a difficulty that changes what cards do would mean the player
     * learns a different game on each setting. The measured curve on Knight is
     * steep - roughly 91/51/29/27/20% across the five fights - so Recruit
     * exists to make the back half finishable and Warlord to make the front
     * half cost something.
     *
     * 0 Recruit, 1 Knight, 2 Warlord. Stored as an int so a hand-edited file
     * that says 7 clamps rather than breaking.
     */
    int difficulty = 1;

    /// Multiplier on an enemy commander's reactor.
    float enemyReactorScale() const {
        switch (difficulty) {
        case 0:  return 0.78f;
        case 2:  return 1.22f;
        default: return 1.0f;
        }
    }
    /**
     * Change to YOUR reactor, applied for the whole run.
     *
     * Scaling only the enemy turned out to be a weak lever: measured across the
     * campaign it moved the mean win rate by five points, because the fights
     * that are lost are lost on the board rather than on the enemy's last few
     * hit points. Your own reactor is felt in every exchange of every fight.
     */
    int playerReactorBonus() const {
        switch (difficulty) {
        case 0:  return 8;
        case 2:  return -6;
        default: return 0;
        }
    }
    /// Extra HP repaired between fights, on top of the usual.
    int bonusRepair() const {
        switch (difficulty) {
        case 0:  return 4;
        case 2:  return -2;
        default: return 0;
        }
    }
    static const char* difficultyName(int value) {
        switch (value) {
        case 0:  return "Recruit";
        case 2:  return "Warlord";
        default: return "Knight";
        }
    }

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
