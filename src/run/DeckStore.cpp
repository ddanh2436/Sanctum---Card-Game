#include "run/DeckStore.hpp"

#include "utils/DataLoader.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

using nlohmann::json;

namespace {

std::vector<DeckStore::SavedDeck> g_decks;
bool g_loaded = false;
std::string g_path = "custom_decks.json";

/// Ordered: the primary slot is what unlocks a Titan, so a pair is not a set.
bool samePair(const DeckStore::SavedDeck& deck, MechRole primary, MechRole secondary) {
    return deck.primary == primary && deck.secondary == secondary;
}

} // namespace

namespace DeckStore {

void load(const std::string& path) {
    if (g_loaded) return;
    g_loaded = true;
    g_path = path;

    std::ifstream file(path);
    if (!file.is_open()) return;   // never edited a deck: not an error

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        std::cerr << "[DeckStore] " << path << " is malformed (" << e.what()
                  << ") -> the generated decks will be used.\n";
        return;
    }
    if (!root.is_array()) return;

    for (const json& entry : root) {
        SavedDeck deck;
        // An unparseable role leaves the default in place rather than failing
        // the whole file: one bad entry should not cost the player every deck.
        parseMechRole(entry.value("primary", std::string("Vanguard")), deck.primary);
        parseMechRole(entry.value("secondary", std::string("Siege")), deck.secondary);
        for (const json& id : entry.value("cards", json::array())) {
            if (id.is_string()) deck.cardIds.push_back(id.get<std::string>());
        }
        if (!deck.cardIds.empty()) g_decks.push_back(std::move(deck));
    }
}

void save(const std::string& path) {
    json root = json::array();
    for (const SavedDeck& deck : g_decks) {
        json entry;
        entry["primary"] = toString(deck.primary);
        entry["secondary"] = toString(deck.secondary);
        entry["cards"] = deck.cardIds;
        root.push_back(entry);
    }
    std::ofstream file(path.empty() ? g_path : path);
    if (!file.is_open()) {
        std::cerr << "[DeckStore] could not write " << path << "\n";
        return;
    }
    file << root.dump(2) << "\n";
}

const SavedDeck* find(MechRole primary, MechRole secondary) {
    load();
    for (const SavedDeck& deck : g_decks) {
        if (samePair(deck, primary, secondary)) return &deck;
    }
    return nullptr;
}

void store(MechRole primary, MechRole secondary, const std::vector<CardData>& cards) {
    load();
    SavedDeck deck;
    deck.primary = primary;
    deck.secondary = secondary;
    for (const CardData& card : cards) deck.cardIds.push_back(card.id);

    auto existing = std::find_if(g_decks.begin(), g_decks.end(),
        [&](const SavedDeck& d) { return samePair(d, primary, secondary); });
    if (existing != g_decks.end()) *existing = std::move(deck);
    else g_decks.push_back(std::move(deck));

    save(g_path);
}

void clear(MechRole primary, MechRole secondary) {
    load();
    g_decks.erase(std::remove_if(g_decks.begin(), g_decks.end(),
        [&](const SavedDeck& d) { return samePair(d, primary, secondary); }), g_decks.end());
    save(g_path);
}

DeckConfiguration configurationFor(MechRole primary, MechRole secondary) {
    const SavedDeck* saved = find(primary, secondary);
    if (!saved) return DeckBuilder::build(primary, secondary);

    DeckConfiguration config;
    config.primaryRole = primary;
    config.secondaryRole = secondary;
    for (const std::string& id : saved->cardIds) {
        if (const CardData* card = DataLoader::findCard(id)) config.cards.push_back(*card);
    }

    // A saved file outlives the catalogue. If a card was renamed or dropped
    // between versions the deck now has holes in it, and handing that to the
    // duel fails later and far less clearly than falling back here does.
    if (!config.isValidDeck()) {
        std::cerr << "[DeckStore] the saved " << toString(primary) << "/" << toString(secondary)
                  << " deck no longer passes the rules -> using the generated one.\n";
        return DeckBuilder::build(primary, secondary);
    }
    return config;
}

} // namespace DeckStore
