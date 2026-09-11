#pragma once

#include "battle/CardData.hpp"
#include "run/DeckBuilder.hpp"

#include <string>
#include <vector>

/**
 * @brief Decks the player built by hand, saved beside the executable.
 *
 * The game has always generated a deck the moment you picked two cores. That
 * stays the default - it is what keeps a run one click away and never faces a
 * new player with an empty list - but it also meant the catalogue's cards were
 * chosen for you every time.
 *
 * A player may keep as many decks as they like, including several on the same
 * pair of cores, which is the whole point of a builder: the interesting
 * question is usually "the aggressive Vanguard list or the grindy one", not
 * "which two cores". Each deck carries its own id and name.
 *
 * Which deck a RUN uses is decided by the cores you pick on the way in, so the
 * lookup is still by pair: the most recently saved deck for that pair wins, and
 * the list screen marks it IN USE so the rule is visible rather than guessed.
 * A pair you have never edited still gets the generated deck, so nothing is
 * taken away by this file existing and deleting it restores the old behaviour
 * exactly.
 *
 * Storage is one JSON file of player data, so it sits next to settings.json
 * rather than in assets/.
 */
namespace DeckStore {

struct SavedDeck {
    /// Stable across renames; what the UI passes around.
    std::string id;
    std::string name;
    MechRole primary = MechRole::Vanguard;
    MechRole secondary = MechRole::Siege;
    /// Catalogue ids, one entry per copy.
    std::vector<std::string> cardIds;
    /// Seconds since the epoch when it was last written. Decides which deck of
    /// a pair a run picks up, and orders the list screen.
    long long savedAt = 0;
};

/// Read the file. Safe to call more than once; later calls are no-ops.
void load(const std::string& path = "custom_decks.json");
void save(const std::string& path = "");

/// Every saved deck, newest first.
const std::vector<SavedDeck>& all();
/// The deck with this id, or nullptr.
const SavedDeck* find(const std::string& id);
/// The deck a run on this pair would use, or nullptr when there is none.
const SavedDeck* newestFor(MechRole primary, MechRole secondary);

/// Create or overwrite. An empty `id` mints a new one; the id used is returned.
std::string store(const std::string& id, const std::string& name,
                  MechRole primary, MechRole secondary,
                  const std::vector<CardData>& cards);
void remove(const std::string& id);

/// A name no existing deck is using, for a brand new deck on this pair.
std::string suggestName(MechRole primary, MechRole secondary);

/**
 * The configuration a run should use for this pair: the player's newest deck
 * when one exists AND still passes the rules, otherwise the generated one.
 *
 * The validity check is not paranoia. The saved file outlives the catalogue -
 * a card renamed or removed between versions would otherwise hand the duel a
 * deck with holes in it, which fails much later and much less clearly.
 */
DeckConfiguration configurationFor(MechRole primary, MechRole secondary);
/// The same, for one named deck.
DeckConfiguration configurationOf(const SavedDeck& deck);

} // namespace DeckStore
