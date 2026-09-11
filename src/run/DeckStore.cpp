#include "run/DeckStore.hpp"

#include "utils/DataLoader.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

using nlohmann::json;

namespace {

std::vector<DeckStore::SavedDeck> g_decks;
bool g_loaded = false;
std::string g_path = "custom_decks.json";

long long nowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

/// Newest first. Used for the list screen and for "which deck does a run use".
void sortNewestFirst() {
    std::stable_sort(g_decks.begin(), g_decks.end(),
        [](const DeckStore::SavedDeck& a, const DeckStore::SavedDeck& b) {
            return a.savedAt > b.savedAt;
        });
}

std::string mintId() {
    // Time plus a counter. Two decks saved in the same second still differ, and
    // the id never has to be unique across machines - it only addresses a row
    // in one player's own file.
    static int counter = 0;
    std::ostringstream out;
    out << "d" << nowSeconds() << "_" << counter++;
    return out.str();
}

} // namespace

namespace DeckStore {

void load(const std::string& path) {
    if (g_loaded) return;
    g_loaded = true;
    g_path = path;

    std::ifstream file(path);
    if (!file.is_open()) return;   // never built a deck: not an error

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
        deck.id = entry.value("id", std::string());
        deck.name = entry.value("name", std::string());
        // An unparseable role leaves the default in place rather than failing
        // the whole file: one bad entry should not cost the player every deck.
        parseMechRole(entry.value("primary", std::string("Vanguard")), deck.primary);
        parseMechRole(entry.value("secondary", std::string("Siege")), deck.secondary);
        deck.savedAt = entry.value("savedAt", 0LL);
        for (const json& id : entry.value("cards", json::array())) {
            if (id.is_string()) deck.cardIds.push_back(id.get<std::string>());
        }
        if (deck.cardIds.empty()) continue;
        if (deck.id.empty()) deck.id = mintId();
        if (deck.name.empty()) {
            deck.name = std::string(displayName(deck.primary)) + " / "
                      + displayName(deck.secondary);
        }
        g_decks.push_back(std::move(deck));
    }
    sortNewestFirst();
}

void save(const std::string& path) {
    json root = json::array();
    for (const SavedDeck& deck : g_decks) {
        json entry;
        entry["id"] = deck.id;
        entry["name"] = deck.name;
        entry["primary"] = toString(deck.primary);
        entry["secondary"] = toString(deck.secondary);
        entry["savedAt"] = deck.savedAt;
        entry["cards"] = deck.cardIds;
        root.push_back(entry);
    }
    const std::string target = path.empty() ? g_path : path;
    std::ofstream file(target);
    if (!file.is_open()) {
        std::cerr << "[DeckStore] could not write " << target << "\n";
        return;
    }
    file << root.dump(2) << "\n";
}

const std::vector<SavedDeck>& all() {
    load();
    return g_decks;
}

const SavedDeck* find(const std::string& id) {
    load();
    for (const SavedDeck& deck : g_decks) {
        if (deck.id == id) return &deck;
    }
    return nullptr;
}

const SavedDeck* newestFor(MechRole primary, MechRole secondary) {
    load();
    // g_decks is newest first, so the first match is the one a run uses.
    for (const SavedDeck& deck : g_decks) {
        if (deck.primary == primary && deck.secondary == secondary) return &deck;
    }
    return nullptr;
}

std::string store(const std::string& id, const std::string& name,
                  MechRole primary, MechRole secondary,
                  const std::vector<CardData>& cards) {
    load();
    SavedDeck deck;
    deck.id = id.empty() ? mintId() : id;
    deck.name = name.empty() ? suggestName(primary, secondary) : name;
    deck.primary = primary;
    deck.secondary = secondary;
    deck.savedAt = nowSeconds();
    for (const CardData& card : cards) deck.cardIds.push_back(card.id);

    auto existing = std::find_if(g_decks.begin(), g_decks.end(),
        [&](const SavedDeck& d) { return d.id == deck.id; });
    if (existing != g_decks.end()) *existing = deck;
    else g_decks.push_back(deck);

    sortNewestFirst();
    save(g_path);
    return deck.id;
}

void remove(const std::string& id) {
    load();
    g_decks.erase(std::remove_if(g_decks.begin(), g_decks.end(),
        [&](const SavedDeck& d) { return d.id == id; }), g_decks.end());
    save(g_path);
}

std::string suggestName(MechRole primary, MechRole secondary) {
    load();
    const std::string base = std::string(displayName(primary)) + " / " + displayName(secondary);
    bool taken = false;
    for (const SavedDeck& deck : g_decks) {
        if (deck.name == base) { taken = true; break; }
    }
    if (!taken) return base;

    for (int n = 2; n < 100; ++n) {
        const std::string candidate = base + " " + std::to_string(n);
        bool clash = false;
        for (const SavedDeck& deck : g_decks) {
            if (deck.name == candidate) { clash = true; break; }
        }
        if (!clash) return candidate;
    }
    return base;
}

DeckConfiguration configurationOf(const SavedDeck& deck) {
    DeckConfiguration config;
    config.primaryRole = deck.primary;
    config.secondaryRole = deck.secondary;
    for (const std::string& id : deck.cardIds) {
        if (const CardData* card = DataLoader::findCard(id)) config.cards.push_back(*card);
    }

    // A saved file outlives the catalogue. If a card was renamed or dropped
    // between versions the deck now has holes in it, and handing that to the
    // duel fails later and far less clearly than falling back here does.
    if (!config.isValidDeck()) {
        std::cerr << "[DeckStore] \"" << deck.name
                  << "\" no longer passes the rules -> using the generated deck.\n";
        return DeckBuilder::build(deck.primary, deck.secondary);
    }
    return config;
}

DeckConfiguration configurationFor(MechRole primary, MechRole secondary) {
    const SavedDeck* saved = newestFor(primary, secondary);
    if (!saved) return DeckBuilder::build(primary, secondary);
    return configurationOf(*saved);
}

} // namespace DeckStore
