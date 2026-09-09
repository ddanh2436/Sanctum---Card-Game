#pragma once

#include "battle/Board.hpp"
#include "battle/CardData.hpp"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

/**
 * @brief Draws cards and board units. Shared by the hand, the battlefield and
 *        the reward screen so one card looks the same everywhere it appears.
 */
namespace CardArt {

/// Doctrine accent colour used for frames, gems and name plates.
sf::Color accentFor(const CardData& card);
sf::Color panelFor(const CardData& card);

/// Path of the frame texture that suits this card.
std::string frameFor(const CardData& card);

/// Short keyword line, e.g. "TAUNT  VANGUARD".
std::string keywordLine(const CardData& card);

/// One rules term and what it means, for the inspect panel's glossary.
struct GlossaryEntry {
    std::string term;
    std::string text;
};

/// Every rule that applies to this card: its keywords, its tier's tribute
/// rules, and what its category does. This is the only place in the game that
/// teaches the rules, so it covers everything the card actually depends on.
std::vector<GlossaryEntry> glossaryFor(const CardData& card);

/**
 * @brief The effects currently sitting on a deployed frame.
 *
 * Separate from glossaryFor, which describes what a card *is*. This describes
 * what has been done to it: the plating a spell just granted, the EMP burn it
 * is carrying, the field an ally is projecting over it. Without this a player
 * watches a stat change and has no way to find out which card caused it.
 *
 * Returns an empty list for a frame nothing is affecting.
 */
std::vector<GlossaryEntry> statusesFor(const Unit& unit);

/**
 * @brief A hand / reward card. `size` is the full card size; text scales with it.
 *
 * `withBadges` draws the cost gem and the attack/health circles on the card
 * itself. At hand size that is the right call - the numbers have to be readable
 * without opening anything. The inspector turns it off, because at 320x448
 * there is room to put the numbers somewhere they do not sit on the artwork.
 */
void drawCard(sf::RenderTarget& target, const sf::Font& font, const CardData& card,
              sf::Vector2f centre, sf::Vector2f size, float rotation = 0.0f,
              bool playable = true, bool highlighted = false, bool withBadges = true);

/// A unit standing on the board.
void drawUnit(sf::RenderTarget& target, const sf::Font& font, const Unit& unit,
              sf::FloatRect bounds, bool selectable, bool selected, bool targeted);

/**
 * @brief A face-down card.
 *
 * Every hidden card in the game goes through here - draw piles, the enemy's
 * hand, a set counter-protocol, the back half of a trap flip - so they all look
 * like the same object.
 *
 * Each side has its own back, which is the point: a face-down card on the
 * board tells you whose it is without telling you what it is. `accent` and the
 * procedural fallback only matter when the artwork is missing.
 */
void drawCardBack(sf::RenderTarget& target, sf::FloatRect bounds, Side owner,
                  sf::Color accent, float alpha = 1.0f);

/**
 * @brief A commander portrait in a square frame.
 *
 * Cover-fits, but biased toward the top of the source rather than its middle:
 * avatars are usually full-length figures, and a centred square crop of one is
 * a picture of a belt buckle. `bias` is where the crop sits vertically, 0 at
 * the top of the image and 1 at the bottom.
 */
void drawAvatar(sf::RenderTarget& target, const std::string& path,
                sf::FloatRect bounds, sf::Color accent, float bias = 0.18f);

/// An empty board slot.
void drawEmptySlot(sf::RenderTarget& target, sf::FloatRect bounds, bool highlighted,
                   sf::Color accent);

/// A trap slot: face-down back, or the revealed card for your own traps.
/// The border colour that marks a card as an armed counter-protocol.
sf::Color armedBorder();

void drawTrapSlot(sf::RenderTarget& target, const sf::Font& font, const TrapCard* trap,
                  sf::FloatRect bounds, Side owner, bool reveal, bool highlighted);

} // namespace CardArt
