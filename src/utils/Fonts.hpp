#pragma once

#include <SFML/Graphics/Font.hpp>

/**
 * @brief The game's two faces, and the rule for which text gets which.
 *
 * The whole game used to be set in one face, and that face was Georgia, chosen
 * for the fantasy build this game grew out of. Two things were wrong with it,
 * both measured by rendering the real strings at the real sizes rather than by
 * eye:
 *
 *   - **Georgia has no Vietnamese.** Every accented vowel came out as an empty
 *     box, so the intro captions were literally unreadable.
 *   - **Georgia's numerals are text figures.** "Bastion-01" rendered as
 *     "Bastion-o1", and the attack and health badges - the numbers a player
 *     reads more often than anything else on screen - were set in digits that
 *     drop below the baseline and vary in height.
 *
 * On top of that, a serif at 8-12px on a dark ground is the hardest thing to
 * read a UI can ask for: the strokes are thin to begin with, light-on-dark
 * makes them look thinner still, and the serifs themselves close up the
 * counters. Most of this game's text is at those sizes.
 *
 * So there are two faces now, split by job rather than by screen:
 *
 *   display()  Constantia. Titles, card names, the verdict, commander names -
 *              text that is large, read once, and carries the game's character.
 *              It is a screen serif with full Vietnamese coverage, which is
 *              what Georgia was supposed to be.
 *   ui()       Segoe UI Semibold. Rules text, labels, every number. Semibold
 *              rather than regular on purpose: this game paints warm off-white
 *              on near-black almost everywhere, and a regular weight thins out
 *              against that ground.
 *
 * Both are Windows system faces, so nothing ships and nothing is licensed. A
 * .ttf dropped into assets/fonts wins over the system face, so the look can be
 * changed without touching code.
 */
namespace Fonts {

/// Load both faces. Returns false only if no face at all could be found.
bool load();

/// Large text that carries the game's character.
const sf::Font& display();
/// Anything small, and every number.
const sf::Font& ui();

} // namespace Fonts
