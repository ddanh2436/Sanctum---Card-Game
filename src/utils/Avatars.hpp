#pragma once

#include "battle/CardData.hpp"
#include <string>
#include <vector>

/**
 * @brief The commander portraits a player can pick between.
 *
 * The pool is whatever sits in assets/avatars/ - drop a file in and it appears
 * in the picker, no code change and no manifest to keep in step. A file named
 * after a doctrine (paladin.jpg, vanguard.png, ...) is that doctrine's default,
 * so a player who never opens the picker still gets a face that suits the deck
 * they built.
 *
 * The player's explicit choice lives in settings.json as a bare filename. An
 * empty setting means "follow my primary core", which is also what a choice
 * falls back to if that file is later deleted.
 */
namespace Avatars {

/// Every image in assets/avatars/, sorted by filename. Scanned once.
const std::vector<std::string>& pool();
/// Re-read the directory. Only needed if files change while the game runs.
void refresh();

/// Path the player's avatar should load from, given their primary core.
std::string forPlayer(MechRole primary);

/// The filename without directory or extension, for use as a label.
std::string label(const std::string& path);

/// Store a choice. Pass an empty string to go back to the role default.
void choose(const std::string& path);
/// The current explicit choice, or empty when following the role.
std::string chosen();

} // namespace Avatars
