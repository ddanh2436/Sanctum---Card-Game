#include "utils/AudioManager.hpp"
#include "utils/AudioCatalogue.hpp"
#include "utils/ResourceManager.hpp"
#include "utils/Settings.hpp"
#include <algorithm>
#include <iostream>

AudioManager& AudioManager::get() {
    static AudioManager instance;
    return instance;
}

void AudioManager::applySettings() {
    const Settings& settings = Settings::get();
    m_music.setVolume(settings.musicMix());
    for (sf::Sound& voice : m_voices) {
        voice.setVolume(settings.sfxMix());
    }
}

void AudioManager::playMusic(const std::string& path, bool loop) {
    if (path == m_track) {
        // Already the current track. Restart only if it has been stopped.
        if (!m_musicMissing && m_music.getStatus() == sf::Music::Stopped) {
            m_music.play();
        }
        return;
    }

    m_music.stop();
    m_track = path;
    m_musicMissing = false;

    if (!ResourceManager::exists(path)) {
        // Not an error: the soundtrack simply has not been added yet.
        m_musicMissing = true;
        return;
    }

    if (!m_music.openFromFile(path)) {
        std::cerr << "[Audio] Could not open music: " << path << "\n";
        m_musicMissing = true;
        return;
    }

    m_music.setLoop(loop);
    m_music.setVolume(Settings::get().musicMix());
    m_music.play();
}

void AudioManager::playMusicCue(Cue cue, bool loop) {
    AudioCatalogue::get().scan();
    const std::string path = AudioCatalogue::get().resolve(cue);
    if (path.empty()) {
        // No track for this cue yet. Leave whatever is playing alone rather
        // than cutting to silence, so an unfilled slot is not a regression.
        return;
    }
    playMusic(path, loop);
}

void AudioManager::queueMusicCue(Cue cue) {
    m_queued = cue;
    m_hasQueued = true;
}

void AudioManager::update() {
    if (!m_hasQueued) return;
    // Take over the moment the current track is done - or straight away if the
    // track it was waiting on turned out not to be on disk.
    if (m_musicMissing || m_music.getStatus() == sf::Music::Stopped) {
        const Cue next = m_queued;
        m_hasQueued = false;
        playMusicCue(next, true);
    }
}

void AudioManager::stopMusic() {
    m_music.stop();
    m_track.clear();
    m_musicMissing = false;
    m_hasQueued = false;
}

void AudioManager::pauseMusic() {
    if (m_music.getStatus() == sf::Music::Playing) m_music.pause();
}

void AudioManager::resumeMusic() {
    if (m_music.getStatus() == sf::Music::Paused) m_music.play();
}

void AudioManager::playCue(Cue cue, const CardData* card, float pitch) {
    AudioCatalogue::get().scan();
    const MechRole role = card ? card->role : MechRole::Vanguard;
    const std::string path = AudioCatalogue::get().resolve(
        cue, card ? card->id : std::string(), card ? &role : nullptr);
    if (path.empty()) return;
    playSfx(path, pitch);
}

void AudioManager::playRoleCue(Cue cue, MechRole role, float pitch) {
    AudioCatalogue::get().scan();
    const std::string path = AudioCatalogue::get().resolve(cue, std::string(), &role);
    if (path.empty()) return;
    playSfx(path, pitch);
}

void AudioManager::playNamed(const std::vector<std::string>& stems, float pitch) {
    AudioCatalogue::get().scan();
    const std::string path = AudioCatalogue::get().resolveFirst(stems);
    if (path.empty()) return;
    playSfx(path, pitch);
}

void AudioManager::playSfx(const std::string& path, float pitch) {
    const Settings& settings = Settings::get();
    if (settings.sfxMix() <= 0.0f) return;
    if (!ResourceManager::exists(path)) return;

    const sf::SoundBuffer& buffer = ResourceManager::get().getSoundBuffer(path);
    if (buffer.getSampleCount() == 0) return;

    // Round-robin through the pool, preferring a voice that is free.
    int chosen = m_nextVoice;
    for (int i = 0; i < kVoices; ++i) {
        const int candidate = (m_nextVoice + i) % kVoices;
        if (m_voices[static_cast<size_t>(candidate)].getStatus() != sf::Sound::Playing) {
            chosen = candidate;
            break;
        }
    }
    m_nextVoice = (chosen + 1) % kVoices;

    sf::Sound& voice = m_voices[static_cast<size_t>(chosen)];
    voice.setBuffer(buffer);
    voice.setVolume(settings.sfxMix());
    voice.setPitch(std::clamp(pitch, 0.5f, 2.0f));
    voice.play();
}
