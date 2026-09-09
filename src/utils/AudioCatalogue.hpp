#pragma once

#include "battle/CardData.hpp"
#include <map>
#include <string>
#include <vector>

/**
 * @brief Finds the right audio file for a game event, by name.
 *
 * The sound files are authored and dropped in by hand, so their names are not
 * uniform: `Vanguard_Deploy.mp3` sits next to `iq_deploy.wav`, `Paladin_death`
 * next to `Vanguard_Dead`, and one file is called `Vanguard_TItan_Deploy`. A
 * lookup that expected an exact path would silently play nothing for most of
 * them, which is the failure mode this class exists to prevent: it indexes the
 * folder once, case-folded, and matches on the stem.
 *
 * Resolution runs most specific first:
 *
 *   1. the card's own id      - `iq_jammer`, a frame with its own voice
 *   2. doctrine + event       - `IQ_Attack`, `Vanguard_Deploy`
 *   3. the global event       - `sfx_trap_alarm`, `Victory_Sound`
 *
 * A miss returns an empty string rather than guessing: a Siege frame borrowing
 * a Paladin choir sample is worse than silence, and silence is visible in the
 * coverage report where a wrong sound would not be.
 */
class AudioCatalogue {
public:
    enum class Cue {
        Deploy,
        TitanDeploy,
        Attack,
        Death,
        Spell,
        TrapSet,
        TrapFlip,
        Victory,
        Defeat,
        Intro,
        MusicMenu,
        MusicMap,
        MusicBattle,
    };

    /// Index `directory` (defaults to assets/audio). Safe to call repeatedly;
    /// only the first call for a given directory does any work.
    static AudioCatalogue& get();
    void scan(const std::string& directory = "assets/audio");

    /**
     * @brief The file to play, or "" when nothing matches.
     * @param cardId  the frame's catalogue id, for cards with their own voice.
     * @param role    the doctrine to fall back to.
     */
    std::string resolve(Cue cue, const std::string& cardId = "",
                        const MechRole* role = nullptr) const;

    /**
     * @brief The first of these stems that exists, or "".
     *
     * For one-off moments that are not a doctrine event - the intro's bell, its
     * sword draw, its title slam. List the dedicated name first and a sensible
     * stand-in after it, so dropping `intro_slam.wav` into assets/audio takes
     * over from the borrowed sound with no code change.
     */
    std::string resolveFirst(const std::vector<std::string>& stems) const;

    /// Every stem that was indexed, for diagnostics and tests.
    std::vector<std::string> stems() const;
    bool empty() const { return m_files.empty(); }

    /// The candidate names `resolve` would try, in order. Exposed so a test can
    /// state the naming contract rather than restating it in the test body.
    static std::vector<std::string> candidatesFor(Cue cue, const std::string& cardId,
                                                  const MechRole* role);

private:
    AudioCatalogue() = default;

    std::string m_directory;
    /// case-folded stem -> full path
    std::map<std::string, std::string> m_files;
};
