#pragma once

#include "battle/CardData.hpp"
#include <string>
#include <vector>

/**
 * @brief Loads the card catalogue from assets/data/cards.json.
 *
 * The catalogue is the single source of truth for every card in the game;
 * decks are then assembled from it by role (see run/DeckBuilder.hpp). A missing
 * or malformed file logs a warning and falls back to a built-in catalogue so
 * the game still starts.
 */
namespace DataLoader {

/// The full catalogue, all six doctrines.
const std::vector<CardData>& catalogue();

/// Force a reload (tests use this after writing a temporary file).
const std::vector<CardData>& loadCatalogue(const std::string& path = "assets/data/cards.json");

/// Built-in catalogue used when the JSON cannot be read.
std::vector<CardData> builtinCatalogue();

const CardData* findCard(const std::string& id);

/**
 * @brief Cards a run may offer as a reward for these two cores.
 *
 * Anything from the primary role, plus the secondary role's non-titan cards -
 * exactly the pool the Dual-Core Protocol lets you play.
 */
std::vector<CardData> rewardPool(MechRole primary, MechRole secondary);

/// Expand a list of ids into a shuffled deck.
std::vector<CardData> buildDeck(const std::vector<std::string>& ids);

} // namespace DataLoader
