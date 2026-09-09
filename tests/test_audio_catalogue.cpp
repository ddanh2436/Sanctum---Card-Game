// Proves the audio files on disk actually reach the events they were named for.
//
// This is the test that would have caught the state the project was in before:
// every call site asked for `explosion.wav`, `kiem_chem.wav`, `summon.wav` and
// `bgm_menu.ogg`, none of which existed, so the game ran completely silent and
// nothing said so.

#include "TestAssert.hpp"
#include "utils/AudioCatalogue.hpp"
#include "utils/DataLoader.hpp"

#include <SFML/Audio/SoundBuffer.hpp>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <set>
#include <string>

namespace {

using Cue = AudioCatalogue::Cue;

const char* cueName(Cue cue) {
    switch (cue) {
    case Cue::Deploy:      return "Deploy";
    case Cue::TitanDeploy: return "TitanDeploy";
    case Cue::Attack:      return "Attack";
    case Cue::Death:       return "Death";
    case Cue::Spell:       return "Spell";
    case Cue::TrapSet:     return "TrapSet";
    case Cue::TrapFlip:    return "TrapFlip";
    case Cue::Victory:     return "Victory";
    case Cue::Defeat:      return "Defeat";
    case Cue::Intro:       return "Intro";
    case Cue::MusicMenu:   return "MusicMenu";
    case Cue::MusicMap:    return "MusicMap";
    case Cue::MusicBattle: return "MusicBattle";
    }
    return "?";
}

/// The file name only, so the report reads like the folder listing.
std::string leaf(const std::string& path) {
    const size_t cut = path.find_last_of('/');
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

void test_folder_is_indexed() {
    AudioCatalogue& audio = AudioCatalogue::get();
    audio.scan("assets/audio");

    std::cout << "  indexed " << audio.stems().size() << " audio files\n";
    CHECK_MSG(!audio.empty(), "assets/audio indexed nothing - the game would be silent");
    std::cout << "[PASS] test_folder_is_indexed\n";
}

void test_global_cues_all_resolve() {
    AudioCatalogue& audio = AudioCatalogue::get();
    audio.scan("assets/audio");

    // Every one of these has a file today. If one stops resolving, either a
    // file was renamed or a cue alias was dropped - both are silent failures
    // in the game itself, which is why they are asserted here.
    const Cue required[] = { Cue::Spell, Cue::TrapFlip, Cue::Victory, Cue::Defeat,
                             Cue::Intro, Cue::MusicMenu, Cue::MusicBattle };

    for (Cue cue : required) {
        const std::string path = audio.resolve(cue);
        std::cout << "  " << cueName(cue) << " -> "
                  << (path.empty() ? std::string("(nothing)") : leaf(path)) << "\n";
        const std::string where = std::string("no audio file resolves for cue ") + cueName(cue);
        CHECK_MSG(!path.empty(), where.c_str());
    }
    std::cout << "[PASS] test_global_cues_all_resolve\n";
}

void test_special_frames_keep_their_own_voice() {
    AudioCatalogue& audio = AudioCatalogue::get();
    audio.scan("assets/audio");

    // iq_jammer.wav is named after a card, not a doctrine, so it must win over
    // the doctrine's generic Attack sample. This is the whole point of the
    // "some frames have their own sound" rule.
    const MechRole inquisitor = MechRole::Inquisitor;
    const std::string own = audio.resolve(Cue::Attack, "iq_jammer", &inquisitor);
    const std::string doctrine = audio.resolve(Cue::Attack, "iq_surge", &inquisitor);

    std::cout << "  iq_jammer attack -> " << leaf(own) << "\n";
    std::cout << "  iq_surge  attack -> " << leaf(doctrine) << "\n";

    CHECK_MSG(leaf(own) == "iq_jammer.wav",
              "a card with its own sample must beat the doctrine sample");
    CHECK_MSG(leaf(doctrine) == "IQ_Attack.wav",
              "a card with no sample of its own must fall back to its doctrine");
    std::cout << "[PASS] test_special_frames_keep_their_own_voice\n";
}

void test_naming_is_case_and_separator_insensitive() {
    AudioCatalogue& audio = AudioCatalogue::get();
    audio.scan("assets/audio");

    // The folder mixes conventions: `Vanguard_TItan_Deploy.wav` has a stray
    // capital, `Paladin_death.wav` a lowercase one, `Game-Over-sound.wav` uses
    // hyphens where everything else uses underscores. All three must still be
    // found, because renaming an author's files is not a fix.
    const MechRole vanguard = MechRole::Vanguard;
    const MechRole paladin = MechRole::Paladin;

    CHECK_MSG(leaf(audio.resolve(Cue::TitanDeploy, "", &vanguard)) == "Vanguard_TItan_Deploy.wav",
              "odd capitalisation must still resolve");
    CHECK_MSG(leaf(audio.resolve(Cue::Death, "", &paladin)) == "Paladin_death.wav",
              "lowercase event names must still resolve");
    CHECK_MSG(leaf(audio.resolve(Cue::Defeat)) == "Game-Over-sound.wav",
              "hyphenated names must still resolve");

    // The Dragoon set on disk is spelled Dragon_*. The lookup absorbs that
    // rather than the files being renamed, and the newer complete set has to
    // beat the single older Dragoon_Titan_Spawn.wav that sits beside it.
    const MechRole dragoon = MechRole::Dragoon;
    CHECK_MSG(leaf(audio.resolve(Cue::Attack, "", &dragoon)) == "Dragon_Attack.mp3",
              "the Dragoon set is spelled Dragon_ and must still resolve");
    CHECK_MSG(leaf(audio.resolve(Cue::TitanDeploy, "", &dragoon)) == "Dragon_Titan.mp3",
              "the newer Dragon_Titan must win over Dragoon_Titan_Spawn");
    std::cout << "  dragoon attack -> " << leaf(audio.resolve(Cue::Attack, "", &dragoon))
              << ",  titan -> " << leaf(audio.resolve(Cue::TitanDeploy, "", &dragoon))
              << "\n";
    std::cout << "[PASS] test_naming_is_case_and_separator_insensitive\n";
}

void test_every_file_actually_decodes() {
    // Resolving a name is only half the job: SFML has to be able to DECODE the
    // file. A static SFML built without the mp3 decoder resolves every .mp3
    // here and then plays none of them, with no error anywhere - exactly the
    // kind of silent failure this whole file exists to stop.
    //
    // Walks the folder rather than the cue table, so a file nothing points at
    // yet is still checked.
    int ok = 0;
    std::vector<std::string> broken;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("assets/audio", ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".wav" && ext != ".mp3" && ext != ".ogg" && ext != ".flac") continue;

        const std::string file = entry.path().generic_string();
        sf::SoundBuffer buffer;
        if (buffer.loadFromFile(file) && buffer.getSampleCount() > 0) ++ok;
        else                                                          broken.push_back(leaf(file));
    }

    std::cout << "  decoded " << ok << " files";
    if (!broken.empty()) {
        std::cout << ", FAILED on:";
        for (const std::string& name : broken) std::cout << " " << name;
    }
    std::cout << "\n";

    CHECK_MSG(broken.empty(),
              "SFML cannot decode one of the audio files - it will play silently");
    std::cout << "[PASS] test_every_file_actually_decodes\n";
}

/// Not an assertion: prints which doctrine x event slots have a sound and which
/// are still silent, so the gaps are a list to fill rather than a surprise.
void report_doctrine_coverage() {
    AudioCatalogue& audio = AudioCatalogue::get();
    audio.scan("assets/audio");

    const Cue events[] = { Cue::Deploy, Cue::Attack, Cue::Death, Cue::TitanDeploy };

    std::cout << "Doctrine coverage (. = silent):\n";
    std::cout << "                Deploy    Attack    Death     Titan\n";
    int filled = 0, total = 0;
    for (int r = 0; r < kMechRoleCount; ++r) {
        const MechRole role = static_cast<MechRole>(r);
        std::cout << "  " << toString(role);
        for (size_t pad = std::string(toString(role)).size(); pad < 14; ++pad) std::cout << ' ';
        for (Cue cue : events) {
            const std::string path = audio.resolve(cue, "", &role);
            ++total;
            if (!path.empty()) ++filled;
            std::string cell = path.empty() ? "." : leaf(path);
            if (cell.size() > 9) cell = cell.substr(0, 9);
            std::cout << cell;
            for (size_t pad = cell.size(); pad < 10; ++pad) std::cout << ' ';
        }
        std::cout << "\n";
    }
    std::cout << "  " << filled << " of " << total << " doctrine slots have a sound\n";

    // Card-specific voices, listed so adding one is visibly picked up.
    std::cout << "Frames with their own voice:\n";
    int own = 0;
    for (const CardData& card : DataLoader::catalogue()) {
        const MechRole role = card.role;
        const std::string path = audio.resolve(Cue::Attack, card.id, &role);
        if (!path.empty() && leaf(path).find(card.id) != std::string::npos) {
            std::cout << "  " << card.id << " -> " << leaf(path) << "\n";
            ++own;
        }
    }
    if (own == 0) std::cout << "  (none)\n";
    std::cout << "[INFO] report_doctrine_coverage\n";
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << " AUDIO CATALOGUE TESTS\n";
    std::cout << "========================================\n";

    DataLoader::loadCatalogue("assets/data/cards.json");

    test_folder_is_indexed();
    test_global_cues_all_resolve();
    test_special_frames_keep_their_own_voice();
    test_naming_is_case_and_separator_insensitive();
    test_every_file_actually_decodes();
    report_doctrine_coverage();

    std::cout << "========================================\n";
    std::cout << " ALL AUDIO CATALOGUE TESTS PASSED\n";
    std::cout << "========================================\n";
    return 0;
}
