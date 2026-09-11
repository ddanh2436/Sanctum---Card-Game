#pragma once

#include "battle/CardData.hpp"
#include "run/DeckBuilder.hpp"

#include <string>
#include <vector>

/**
 * @brief Decks the player built by hand, saved beside the executable.
 *
 * The game has always generated a deck for you the moment you picked two
 * cores. That is the right default - it means a run is one click away and a
 * new player never faces an empty list - but it also means the catalogue's
 * seventy-four cards were decided for you every time.
 *
 * A saved deck belongs to a PAIR of cores, not to a slot: Vanguard/Siege and
 * Siege/Vanguard are different decks, and picking a pair you have never edited
 * still hands you the generated one. So nothing is ever taken away by this
 * file existing, and deleting it puts the game exactly back where it was.
 *
 * Storage is one JSON file holding every pair the player has touched. It is
 * player data, so it sits next to settings.json rather than in assets/.
 */
namespace DeckStore {

/// A hand-built deck for one ordered pair of cores.
struct SavedDeck {
    MechRole primary = MechRole::Vanguard;
    MechRole secondary = MechRole::Siege;
    /// Catalogue ids, one entry per copy.
    std::vector<std::string> cardIds;
};

/// Read the file. Safe to call more than once; later calls are no-ops.
void load(const std::string& path = "custom_decks.json");
/// Write every saved deck back out.
void save(const std::string& path = "custom_decks.json");

/// The deck saved for this pair, or nullptr when the player never edited it.
const SavedDeck* find(MechRole primary, MechRole secondary);
/// Replace (or create) the deck for this pair and write the file.
void store(MechRole primary, MechRole secondary, const std::vector<CardData>& cards);
/// Forget this pair, so the generated deck takes over again.
void clear(MechRole primary, MechRole secondary);

/**
 * The configuration a run should use for this pair: the player's own deck when
 * one exists AND still passes the rules, otherwise the generated one.
 *
 * The validity check is not paranoia. The saved file outlives the catalogue -
 * a card renamed or removed between versions would otherwise hand the duel a
 * deck with holes in it, which fails much later and much less clearly.
 */
DeckConfiguration configurationFor(MechRole primary, MechRole secondary);

} // namespace DeckStore
