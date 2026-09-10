#pragma once

#include "battle/CardData.hpp"
#include "battle/Board.hpp"

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

/**
 * @brief Cards flying out of a draw pile and into a hand.
 *
 * A card used to appear in the hand the frame the engine put it there, which
 * answered "what did I draw" but never "where did it come from" - the deck
 * stack in the corner sat inert all duel while the hand silently grew. Each
 * draw is now a short flight from that stack, so the pile is visibly the thing
 * being spent.
 *
 * Three beats, in seconds from launch:
 *
 *   0.00 - 0.15  the back lifts off the pile, small, fading up
 *   0.15 - 0.45  a quadratic Bezier arc to the hand slot, growing to full size
 *   0.45 - 0.60  the X axis pinches to nothing, the face is swapped in, and it
 *                opens back out at rest in the hand
 *
 * The arc's control point sits 80px above the higher of the two endpoints, so
 * the card reads as being pulled up and off the stack rather than sliding along
 * the floor.
 *
 * This class owns geometry and a clock, nothing else: `frames()` hands back
 * what to draw this instant and the caller draws it with the same CardArt calls
 * it uses everywhere else. Keeping the drawing out means a flying card and a
 * held card can never drift apart in appearance.
 *
 * While a flight is in the air the hand slot it is heading for must stay empty,
 * or the card is on screen twice. `hides()` answers that.
 */
class DrawFlight {
public:
    /// One card, as it should be drawn right now.
    struct Frame {
        const CardData* card = nullptr;
        Side owner = Side::Player;
        sf::Vector2f centre;
        /// Already pinched on X for the flip; height is the true height.
        sf::Vector2f size;
        float alpha = 1.0f;
        bool faceUp = false;
    };

    /**
     * Send a card from `from` to `to`.
     *
     * `handIndex` is the slot it is heading for, so the caller can leave that
     * slot empty until it lands. `delay` staggers a batch: five cards drawn on
     * the same frame leaving together read as one blob, so an opening hand
     * fans out over half a second instead.
     */
    void launch(const CardData& card, Side owner, int handIndex,
                sf::Vector2f from, sf::Vector2f to, sf::Vector2f size,
                float delay = 0.0f);

    void update(float dt);
    /// Everything currently in the air, oldest first.
    std::vector<Frame> frames() const;

    /// True while a flight is still heading for this slot. Draw nothing there.
    bool hides(Side owner, int handIndex) const;
    /// True on the frame a card's face is revealed, so the caller can play the
    /// flip sound. Reading it clears it.
    bool consumeFlip();

    bool busy() const { return !m_cards.empty(); }
    void clear() { m_cards.clear(); m_flip = false; }

private:
    struct Flight {
        CardData card;
        Side owner = Side::Player;
        int handIndex = 0;
        sf::Vector2f from, peak, to;
        sf::Vector2f size;
        float time = 0.0f;      // negative while still waiting on its delay
        bool flipped = false;   // has this one already reported its flip
    };

    // The script, in seconds. Changing these changes the feel of every draw.
    static constexpr float kLiftEnd  = 0.15f;
    static constexpr float kFlyEnd   = 0.45f;
    static constexpr float kPinch    = 0.52f;   // the card is edge-on here
    static constexpr float kSettle   = 0.60f;

    std::vector<Flight> m_cards;
    bool m_flip = false;
};
