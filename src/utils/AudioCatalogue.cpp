#include "utils/AudioCatalogue.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace {

std::string fold(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Authors separate words with either character; treat them as the same one
    // so `Game-Over-sound` and `Game_Over_sound` both find the same cue.
    std::replace(text.begin(), text.end(), '-', '_');
    return text;
}

bool isAudio(const std::filesystem::path& path) {
    const std::string ext = fold(path.extension().string());
    return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
}

/// Short doctrine keys as they appear in file names, alongside the full word.
std::vector<std::string> roleAliases(MechRole role) {
    switch (role) {
    case MechRole::Vanguard:   return { "vanguard", "vg" };
    case MechRole::Paladin:    return { "paladin", "pl" };
    case MechRole::Valkyrie:   return { "valkyrie", "vk" };
    // "dragon" first, and it is not a typo to fix: the Dragoon set on disk is
    // named Dragon_Deploy, Dragon_Attack, Dragon_Dead, Dragon_Titan. Renaming an
    // author's files is not the job of the lookup - absorbing the spelling is.
    // It leads so the complete newer set wins over the single older
    // Dragoon_Titan_Spawn.wav, which the intro still names directly.
    case MechRole::Dragoon:    return { "dragon", "dragoon", "dg" };
    case MechRole::Siege:      return { "siege", "sg" };
    case MechRole::Inquisitor: return { "inquisitor", "iq" };
    }
    return {};
}

/// The event half of a `<doctrine>_<event>` name. More than one spelling per
/// cue because the files use several: Dead and death, Titan and Titan_Spawn.
std::vector<std::string> cueAliases(AudioCatalogue::Cue cue) {
    using Cue = AudioCatalogue::Cue;
    switch (cue) {
    case Cue::Deploy:      return { "deploy", "spawn", "summon" };
    case Cue::TitanDeploy: return { "titan_deploy", "titan_spawn", "titan" };
    case Cue::Attack:      return { "attack", "strike" };
    case Cue::Death:       return { "death", "dead", "die", "destroyed" };
    case Cue::Spell:       return { "spell", "cast" };
    case Cue::TrapSet:     return { "trap_set", "trap_arm" };
    case Cue::TrapFlip:    return { "trap_alarm", "trap_flip", "trap" };
    default:               return {};
    }
}

/// Names to try when nothing doctrine-specific exists.
std::vector<std::string> globalNames(AudioCatalogue::Cue cue) {
    using Cue = AudioCatalogue::Cue;
    switch (cue) {
    case Cue::TrapFlip:    return { "sfx_trap_alarm", "trap_alarm", "trap" };
    case Cue::Spell:       return { "spell_activate", "spell_cast", "spell" };
    case Cue::TrapSet:     return { "sfx_trap_set", "trap_set", "sfx_trap_alarm" };
    case Cue::Victory:     return { "victory_sound", "victory", "win" };
    case Cue::Defeat:      return { "game_over_sound", "game_over", "defeat", "lose" };
    case Cue::Intro:       return { "intro" };
    case Cue::MusicMenu:   return { "menu_main_screen", "menu_main", "bgm_menu", "menu" };
    case Cue::MusicMap:    return { "map_music", "bgm_map", "menu_main_screen", "game_music" };
    case Cue::MusicBattle: return { "game_music", "battle_music", "bgm_battle" };
    default:               return {};
    }
}

} // namespace

AudioCatalogue& AudioCatalogue::get() {
    static AudioCatalogue instance;
    return instance;
}

void AudioCatalogue::scan(const std::string& directory) {
    if (directory == m_directory && !m_files.empty()) return;

    m_directory = directory;
    m_files.clear();

    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::filesystem::path& path = entry.path();
        if (!isAudio(path)) continue;

        const std::string key = fold(path.stem().string());
        // First writer wins, so a .wav and .mp3 of the same name do not flicker
        // between builds depending on directory order.
        m_files.emplace(key, path.generic_string());
    }
}

std::vector<std::string> AudioCatalogue::candidatesFor(Cue cue, const std::string& cardId,
                                                       const MechRole* role) {
    std::vector<std::string> out;

    // 1. This exact frame's own voice.
    if (!cardId.empty()) out.push_back(fold(cardId));

    // 2. Doctrine plus event, in both spellings of each.
    if (role) {
        for (const std::string& doctrine : roleAliases(*role)) {
            for (const std::string& event : cueAliases(cue)) {
                out.push_back(doctrine + "_" + event);
            }
        }
    }

    // 3. Whatever stands in for the whole game.
    for (const std::string& name : globalNames(cue)) out.push_back(fold(name));

    return out;
}

std::string AudioCatalogue::resolve(Cue cue, const std::string& cardId,
                                    const MechRole* role) const {
    for (const std::string& candidate : candidatesFor(cue, cardId, role)) {
        auto found = m_files.find(candidate);
        if (found != m_files.end()) return found->second;
    }
    return {};
}

std::string AudioCatalogue::resolveFirst(const std::vector<std::string>& stems) const {
    for (const std::string& stem : stems) {
        auto found = m_files.find(fold(stem));
        if (found != m_files.end()) return found->second;
    }
    return {};
}

std::vector<std::string> AudioCatalogue::stems() const {
    std::vector<std::string> out;
    out.reserve(m_files.size());
    for (const auto& entry : m_files) out.push_back(entry.first);
    return out;
}
