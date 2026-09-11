#include "utils/DataLoader.hpp"
#include "utils/Rng.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>

using nlohmann::json;

namespace DataLoader {
namespace {

std::vector<CardData> g_catalogue;
bool g_loaded = false;

// ---- enum parsing -----------------------------------------------------------

CardCategory parseCategory(const std::string& s) {
    if (s == "Spell") return CardCategory::Spell;
    if (s == "Trap")  return CardCategory::Trap;
    return CardCategory::Unit;
}

CardTier parseTier(int tier) {
    if (tier >= 3) return CardTier::Tier3;
    if (tier == 2) return CardTier::Tier2;
    return CardTier::Tier1;
}

Keyword::Mask parseKeyword(const std::string& s) {
    static const std::unordered_map<std::string, Keyword::Mask> table = {
        { "taunt",    Keyword::Taunt },    { "rush",     Keyword::Rush },
        { "ranged",   Keyword::Ranged },   { "reactive", Keyword::Reactive },
        { "overkill", Keyword::Overkill }, { "plasma",   Keyword::Plasma },
        { "emp",      Keyword::EMP },      { "aerial",   Keyword::Aerial },
        { "splash",   Keyword::Splash },   { "thruster", Keyword::Thruster },
        { "intercept", Keyword::Intercept }
    };
    auto it = table.find(s);
    if (it == table.end()) {
        std::cerr << "[DataLoader] Unknown keyword: " << s << "\n";
        return Keyword::None;
    }
    return it->second;
}

AbilityTrigger parseTrigger(const std::string& s) {
    static const std::unordered_map<std::string, AbilityTrigger> table = {
        { "None",        AbilityTrigger::None },
        { "OnDeploy",    AbilityTrigger::OnDeploy },
        { "OnTurnEnd",   AbilityTrigger::OnTurnEnd },
        { "OnTurnStart", AbilityTrigger::OnTurnStart },
        { "OnDestroyed", AbilityTrigger::OnDestroyed },
        { "OnKill",      AbilityTrigger::OnKill },
        { "OnDamaged",   AbilityTrigger::OnDamaged },
        { "Aura",        AbilityTrigger::Aura }
    };
    auto it = table.find(s);
    if (it == table.end()) {
        std::cerr << "[DataLoader] Unknown ability trigger: " << s << "\n";
        return AbilityTrigger::None;
    }
    return it->second;
}

AbilityKind parseAbilityKind(const std::string& s) {
    static const std::unordered_map<std::string, AbilityKind> table = {
        { "RepairWoundedAlly",        AbilityKind::RepairWoundedAlly },
        { "RepairSelfEachTurn",       AbilityKind::RepairSelfEachTurn },
        { "DamageEnemyFrontline",     AbilityKind::DamageEnemyFrontline },
        { "DamageEnemyBackline",      AbilityKind::DamageEnemyBackline },
        { "DamageKiller",             AbilityKind::DamageKiller },
        { "DrawCards",                AbilityKind::DrawCards },
        { "RepairReactor",            AbilityKind::RepairReactor },
        { "GainOvercharge",           AbilityKind::GainOvercharge },
        { "SpendOverchargeToStrike",  AbilityKind::SpendOverchargeToStrike },
        { "DumpOverchargeOnDeploy",   AbilityKind::DumpOverchargeOnDeploy },
        { "AuraBuffFrontline",        AbilityKind::AuraBuffFrontline },
        { "AuraArmourFrontline",      AbilityKind::AuraArmourFrontline },
        { "AuraRangedBonus",          AbilityKind::AuraRangedBonus },
        { "GainStatsOnKill",          AbilityKind::GainStatsOnKill },
        { "ExtraAttackPerTurn",       AbilityKind::ExtraAttackPerTurn },
        { "RetreatAfterKill",         AbilityKind::RetreatAfterKill },
        { "ReviveBestFromScrap",      AbilityKind::ReviveBestFromScrap },
        { "SpawnScrapDroneOnAllyLoss",AbilityKind::SpawnScrapDroneOnAllyLoss },
        { "RevealEnemyTrap",          AbilityKind::RevealEnemyTrap },
        { "SeizeUnitOnTrapFlip",      AbilityKind::SeizeUnitOnTrapFlip },
        { "BuffPerArmedCounter",      AbilityKind::BuffPerArmedCounter },
        { "ReflectToRow",             AbilityKind::ReflectToRow }
    };
    auto it = table.find(s);
    if (it == table.end()) {
        std::cerr << "[DataLoader] Unknown ability kind: " << s << "\n";
        return AbilityKind::None;
    }
    return it->second;
}

SpellKind parseSpell(const std::string& s) {
    static const std::unordered_map<std::string, SpellKind> table = {
        { "ArmourAllAllies",        SpellKind::ArmourAllAllies },
        { "DestroyHighAttackEnemy", SpellKind::DestroyHighAttackEnemy },
        { "WipeLowHealthUnits",     SpellKind::WipeLowHealthUnits },
        { "DrawThenRepairIfLosses", SpellKind::DrawThenRepairIfLosses },
        { "OverchargeSurge",        SpellKind::OverchargeSurge },
        { "DamageEnemyFrontline",   SpellKind::DamageEnemyFrontline }
    };
    auto it = table.find(s);
    if (it == table.end()) {
        std::cerr << "[DataLoader] Unknown spell kind: " << s << "\n";
        return SpellKind::None;
    }
    return it->second;
}

TrapTrigger parseTrapTrigger(const std::string& s) {
    static const std::unordered_map<std::string, TrapTrigger> table = {
        { "OnEnemyAttackReactor",    TrapTrigger::OnEnemyAttackReactor },
        { "OnEnemySpellCast",        TrapTrigger::OnEnemySpellCast },
        { "OnEnemyHighTierDeploy",   TrapTrigger::OnEnemyHighTierDeploy },
        { "OnEnemyAdvance",          TrapTrigger::OnEnemyAdvance },
        { "OnAllyTargetedByRemoval", TrapTrigger::OnAllyTargetedByRemoval },
        { "OnOwnTitanDestroyed",     TrapTrigger::OnOwnTitanDestroyed },
        { "OnAllyDestroyed",         TrapTrigger::OnAllyDestroyed },
        { "OnEnemyUnitAttack",       TrapTrigger::OnEnemyUnitAttack },
        { "OnEnemyAerialAttack",     TrapTrigger::OnEnemyAerialAttack }
    };
    auto it = table.find(s);
    if (it == table.end()) {
        std::cerr << "[DataLoader] Unknown trap trigger: " << s << "\n";
        return TrapTrigger::None;
    }
    return it->second;
}

TrapKind parseTrapKind(const std::string& s) {
    static const std::unordered_map<std::string, TrapKind> table = {
        { "BlockAndCrushWeak",      TrapKind::BlockAndCrushWeak },
        { "NegateSpellAndBurn",     TrapKind::NegateSpellAndBurn },
        { "OverloadSummon",         TrapKind::OverloadSummon },
        { "WeakenAdvancingUnit",    TrapKind::WeakenAdvancingUnit },
        { "RecallTargetToHand",     TrapKind::RecallTargetToHand },
        { "VentOverchargeAtReactor", TrapKind::VentOverchargeAtReactor },
        { "BlindAttacker",           TrapKind::BlindAttacker },
        { "ReassembleDyingAlly",     TrapKind::ReassembleDyingAlly },
        { "LeechAndMend",            TrapKind::LeechAndMend },
        { "MinefieldSplash",         TrapKind::MinefieldSplash },
        { "ShootDownFlier",          TrapKind::ShootDownFlier },
        { "DetonateForTitanAttack", TrapKind::DetonateForTitanAttack }
    };
    auto it = table.find(s);
    if (it == table.end()) {
        std::cerr << "[DataLoader] Unknown trap kind: " << s << "\n";
        return TrapKind::None;
    }
    return it->second;
}

CardData parseCard(const json& node) {
    CardData card;
    card.id = node.value("id", std::string{});
    card.name = node.value("name", card.id);
    card.category = parseCategory(node.value("category", std::string("Unit")));
    card.tier = parseTier(node.value("tier", 1));

    const std::string roleText = node.value("role", std::string("Vanguard"));
    if (!parseMechRole(roleText, card.role)) {
        std::cerr << "[DataLoader] Unknown role '" << roleText << "' on card "
                  << card.id << " -> defaulting to Vanguard.\n";
    }

    card.manaCost = node.value("manaCost", 1);
    card.attack = node.value("attack", 0);
    card.health = node.value("health", 0);
    card.overchargeCost = node.value("overchargeCost", 0);
    card.deckCount = node.value("deckCount", 1);
    card.description = node.value("description", std::string{});
    card.textureFile = node.value("textureFile", std::string{});

    if (node.contains("keywords")) {
        for (const auto& kw : node["keywords"]) {
            card.keywords |= parseKeyword(kw.get<std::string>());
        }
    }

    if (node.contains("abilities")) {
        for (const auto& node2 : node["abilities"]) {
            Ability ability;
            ability.trigger = parseTrigger(node2.value("trigger", std::string("None")));
            ability.kind = parseAbilityKind(node2.value("kind", std::string("None")));
            ability.value = node2.value("value", 0);
            ability.value2 = node2.value("value2", 0);
            card.abilities.push_back(ability);
        }
    }

    if (node.contains("spell")) {
        card.spell = parseSpell(node["spell"].get<std::string>());
        card.spellValue = node.value("spellValue", 0);
        card.spellValue2 = node.value("spellValue2", 0);
    }
    if (node.contains("trapTrigger")) {
        card.trapTrigger = parseTrapTrigger(node["trapTrigger"].get<std::string>());
        card.trapKind = parseTrapKind(node.value("trapKind", std::string("None")));
        card.trapValue = node.value("trapValue", 0);
        card.trapValue2 = node.value("trapValue2", 0);
    }
    return card;
}

/// A tiny catalogue that keeps the game playable if the JSON is unusable.
CardData quickUnit(const char* id, const char* name, MechRole role, int cost,
                   int atk, int hp, int copies, Keyword::Mask keywords) {
    CardData card;
    card.id = id;
    card.name = name;
    card.role = role;
    card.category = CardCategory::Unit;
    card.manaCost = cost;
    card.attack = atk;
    card.health = hp;
    card.deckCount = copies;
    card.keywords = keywords;
    card.description = "A stock chassis off the line.";
    return card;
}

} // namespace

std::vector<CardData> builtinCatalogue() {
    // One usable frame per doctrine, in enough copies to fill a legal deck.
    return {
        quickUnit("fallback_vg", "Guard Chassis",   MechRole::Vanguard,   2, 1, 4, 30, Keyword::Taunt),
        quickUnit("fallback_pl", "Plasma Chassis",  MechRole::Paladin,    3, 3, 2, 30, Keyword::Plasma),
        quickUnit("fallback_vk", "Repair Chassis",  MechRole::Valkyrie,   2, 2, 3, 30, Keyword::None),
        quickUnit("fallback_dg", "Thruster Chassis",MechRole::Dragoon,    3, 4, 2, 30, Keyword::Rush),
        quickUnit("fallback_sg", "Gun Chassis",     MechRole::Siege,      3, 2, 3, 30, Keyword::Ranged),
        quickUnit("fallback_iq", "Jammer Chassis",  MechRole::Inquisitor, 3, 2, 4, 30, Keyword::EMP),
    };
}

const std::vector<CardData>& loadCatalogue(const std::string& path) {
    g_catalogue.clear();
    g_loaded = true;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[DataLoader] Could not open " << path << " -> using built-in catalogue.\n";
        g_catalogue = builtinCatalogue();
        return g_catalogue;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[DataLoader] JSON syntax error in " << path << ": " << e.what()
                  << " -> using built-in catalogue.\n";
        g_catalogue = builtinCatalogue();
        return g_catalogue;
    }

    if (!root.contains("cards") || !root["cards"].is_array()) {
        std::cerr << "[DataLoader] " << path << " has no card array -> using built-in catalogue.\n";
        g_catalogue = builtinCatalogue();
        return g_catalogue;
    }

    for (const auto& node : root["cards"]) {
        CardData card = parseCard(node);
        if (card.id.empty()) {
            std::cerr << "[DataLoader] Skipping a card with no id.\n";
            continue;
        }
        g_catalogue.push_back(std::move(card));
    }

    if (g_catalogue.empty()) {
        std::cerr << "[DataLoader] No valid cards -> using built-in catalogue.\n";
        g_catalogue = builtinCatalogue();
    } else {
        std::cout << "[DataLoader] Loaded " << g_catalogue.size() << " cards from " << path << "\n";
    }
    return g_catalogue;
}

const std::vector<CardData>& catalogue() {
    if (!g_loaded) loadCatalogue();
    return g_catalogue;
}

const CardData* findCard(const std::string& id) {
    const auto& cards = catalogue();
    auto it = std::find_if(cards.begin(), cards.end(),
                           [&](const CardData& c) { return c.id == id; });
    return it == cards.end() ? nullptr : &(*it);
}

std::vector<CardData> rewardPool(MechRole primary, MechRole secondary) {
    std::vector<CardData> pool;
    for (const CardData& card : catalogue()) {
        if (card.role == primary) {
            pool.push_back(card);
        } else if (card.role == secondary && card.tier != CardTier::Tier3) {
            // The splash core lends tools, never its titan - so never offer one.
            pool.push_back(card);
        }
    }
    return pool;
}

std::vector<CardData> buildDeck(const std::vector<std::string>& ids) {
    std::vector<CardData> deck;
    for (const std::string& id : ids) {
        if (const CardData* card = findCard(id)) deck.push_back(*card);
    }
    std::shuffle(deck.begin(), deck.end(), Rng::engine());
    return deck;
}

} // namespace DataLoader
