#include "battle/CardData.hpp"

#include <algorithm>
#include <cctype>

namespace {

struct RoleInfo {
    // The data key and the displayed name are deliberately two different
    // things. `key` is the token in cards.json, the stem an avatar file is
    // looked up by, and what every saved or printed record has always said;
    // `display` is only ever read by a human on screen. Splitting them let two
    // doctrines be renamed without touching a single card id, art file or audio
    // filename - `pl_censer` is still `pl_censer`, `Paladin_Attack.mp3` still
    // resolves, and cards.json still says "role": "Paladin".
    const char* key;         // the token used in cards.json
    const char* display;     // what the player reads
    const char* title;       // the doctrine's in-fiction name
    const char* passive;     // the commander passive granted by a primary core
    const char* passiveText;
};

// Indexed by MechRole. Keep this table in the enum's order.
constexpr RoleInfo kRoles[kMechRoleCount] = {
    { "Vanguard", "Vanguard",
      "Iron Shield Knights",
      "Aegis Plating",
      "Your frontline frames take 1 less damage from every hit." },

    // Paladin is a holy-order title; Arclight is the same doctrine named for
    // what it actually fields - arc lances, plasma edges and an overcharge core.
    { "Paladin", "Arclight",
      "Radiant Core Division",
      "Overcharge Core",
      "Gain 2 overcharge each upkeep. Every plasma strike spends 1 for +2 damage." },

    { "Valkyrie", "Valkyrie",
      "Wings of the Scrap Yard",
      "Nanite Reclamation",
      "The first frame you lose each turn is rebuilt into your deck instead of\n"
      "being scrapped." },

    { "Dragoon", "Dragoon",
      "Thunderlance Cavalry",
      "Thruster Assault",
      "The first frame you advance each turn advances for free." },

    { "Siege", "Siege",
      "The Immovable Batteries",
      "Fire Support",
      "Your ranged frames deal 1 extra damage from the support row." },

    // Inquisitor names a church court; Overseer names what the doctrine does -
    // watch, read the enemy early, and answer with a counter-protocol.
    { "Inquisitor", "Overseer",
      "Cipher Directorate",
      "Cold Read",
      "Arm counter-protocols for free. Every counter that fires draws you a card\n"
      "and burns the enemy core for 2." }
};

} // namespace

const char* toString(MechRole role) {
    return kRoles[static_cast<int>(role)].key;
}

const char* displayName(MechRole role) {
    return kRoles[static_cast<int>(role)].display;
}

const char* roleTitle(MechRole role) {
    return kRoles[static_cast<int>(role)].title;
}

const char* rolePassiveName(MechRole role) {
    return kRoles[static_cast<int>(role)].passive;
}

const char* rolePassiveText(MechRole role) {
    return kRoles[static_cast<int>(role)].passiveText;
}

bool parseMechRole(const std::string& text, MechRole& out) {
    // cards.json is hand-edited, so accept any casing.
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (int i = 0; i < kMechRoleCount; ++i) {
        std::string key = kRoles[i].key;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key == lowered) {
            out = static_cast<MechRole>(i);
            return true;
        }
    }
    return false;
}
