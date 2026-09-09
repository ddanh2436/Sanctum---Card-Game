#pragma once

#include "utils/AudioCatalogue.hpp"

#include <SFML/Audio.hpp>
#include <array>
#include <string>
#include <vector>

/**
 * @brief One place that owns music playback and sound effects.
 *
 * Volume is always master x channel, so moving the master slider moves
 * everything without the individual channels losing their relative mix.
 *
 * Nothing here fails hard on a missing file: a track that is not on disk yet is
 * simply remembered and skipped. That way the music hooks can be wired up now
 * and start working the moment the audio files are dropped in.
 */
class AudioManager {
public:
    using Cue = AudioCatalogue::Cue;

    static AudioManager& get();

    /// Re-read the volumes from Settings and apply them to anything playing.
    void applySettings();

    // --- music ---
    /// Start a track, looping. Re-requesting the current track does nothing,
    /// so calling this on every state entry will not restart the music.
    void playMusic(const std::string& path, bool loop = true);
    void stopMusic();
    void pauseMusic();
    void resumeMusic();
    const std::string& currentTrack() const { return m_track; }
    bool musicPlaying() const { return m_music.getStatus() == sf::Music::Playing; }

    /// Start the track for a cue, looking the file up by name. Does nothing
    /// when no file matches, so a missing soundtrack is silence, not a crash.
    void playMusicCue(Cue cue, bool loop = true);

    /// Start this track once the current one finishes. Used for the intro
    /// sting, which plays through and then hands over to the menu loop.
    void queueMusicCue(Cue cue);

    /// Pumped once a frame by the Engine so the queued track can take over.
    void update();

    // --- sound effects ---
    void playSfx(const std::string& path, float pitch = 1.0f);

    /**
     * @brief Play the sound for a game event.
     *
     * Prefers a voice belonging to this exact card, then one belonging to its
     * doctrine, then a global one. See AudioCatalogue for the naming contract.
     */
    void playCue(Cue cue, const CardData* card = nullptr, float pitch = 1.0f);

    /// A cue that belongs to a doctrine rather than to one card - the reactor
    /// of a commander coming apart, for instance.
    void playRoleCue(Cue cue, MechRole role, float pitch = 1.0f);

    /// Play the first of these file stems that exists. See
    /// AudioCatalogue::resolveFirst.
    void playNamed(const std::vector<std::string>& stems, float pitch = 1.0f);

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

private:
    AudioManager() = default;

    sf::Music m_music;
    std::string m_track;
    bool m_musicMissing = false;   // the requested track is not on disk
    Cue m_queued = Cue::MusicMenu;
    bool m_hasQueued = false;

    // A small pool so overlapping effects do not cut each other off.
    static constexpr int kVoices = 12;
    std::array<sf::Sound, kVoices> m_voices;
    int m_nextVoice = 0;
};
