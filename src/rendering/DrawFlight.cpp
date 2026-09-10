#include "rendering/DrawFlight.hpp"

#include <algorithm>
#include <cmath>

namespace {

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/// 0 before `from`, 1 after `to`, linear between.
float ramp(float t, float from, float to) {
    return to <= from ? 1.0f : clamp01((t - from) / (to - from));
}

/// Decelerating. The card should arrive rather than slam.
float easeOut(float t) {
    const float u = 1.0f - clamp01(t);
    return 1.0f - u * u * u;
}

sf::Vector2f bezier(sf::Vector2f p0, sf::Vector2f peak, sf::Vector2f p1, float t) {
    const float u = 1.0f - t;
    return u * u * p0 + 2.0f * u * t * peak + t * t * p1;
}

} // namespace

void DrawFlight::launch(const CardData& card, Side owner, int handIndex,
                        sf::Vector2f from, sf::Vector2f to, sf::Vector2f size,
                        float delay) {
    Flight flight;
    flight.card = card;
    flight.owner = owner;
    flight.handIndex = handIndex;
    flight.from = from;
    flight.to = to;
    flight.size = size;
    flight.time = -delay;

    // Above the *higher* of the two ends, so the arc always bows away from the
    // board rather than cutting across it. Screen Y grows downward, hence min.
    //
    // 150, not the 80 the arc is meant to clear: a quadratic Bezier only pulls
    // half way to its control point, so a control point 80 above the ends gives
    // an apex 40 above them - measured at 34 for the player's pile, which was
    // barely a bow at all. Doubling the control point buys the lift that was
    // actually wanted.
    flight.peak = { (from.x + to.x) * 0.5f, std::min(from.y, to.y) - 150.0f };

    m_cards.push_back(flight);
}

void DrawFlight::update(float dt) {
    for (Flight& flight : m_cards) {
        flight.time += dt;
        if (!flight.flipped && flight.time >= kPinch) {
            flight.flipped = true;
            // Only the player's draws are worth a sound. The enemy draws every
            // turn too, and six of these a turn from off-screen is noise.
            if (flight.owner == Side::Player) m_flip = true;
        }
    }
    m_cards.erase(std::remove_if(m_cards.begin(), m_cards.end(),
                                 [](const Flight& f) { return f.time >= kSettle; }),
                  m_cards.end());
}

std::vector<DrawFlight::Frame> DrawFlight::frames() const {
    std::vector<Frame> out;
    out.reserve(m_cards.size());

    for (const Flight& flight : m_cards) {
        if (flight.time < 0.0f) continue;   // still waiting its turn

        Frame frame;
        frame.card = &flight.card;
        frame.owner = flight.owner;

        // ---- position ------------------------------------------------------
        if (flight.time < kLiftEnd) {
            frame.centre = flight.from;
        } else {
            const float t = easeOut(ramp(flight.time, kLiftEnd, kFlyEnd));
            frame.centre = bezier(flight.from, flight.peak, flight.to, t);
        }

        // ---- size ----------------------------------------------------------
        // Half size on the pile, full size by the time it arrives: a card
        // drawn at hand size the whole way looks like it was already in the
        // hand and merely slid over.
        const float grow = 0.5f + 0.5f * ramp(flight.time, kLiftEnd, kFlyEnd);
        frame.size = flight.size * grow;

        // ---- the flip ------------------------------------------------------
        // One pinch, not a spin: SFML has no perspective, so a card turned by
        // rotating it would sweep through the board like a flat plate. Closing
        // the X axis to nothing and opening it again is the same silhouette a
        // real card gives and costs one multiply.
        if (flight.time >= kFlyEnd) {
            const float pinch = (flight.time < kPinch)
                ? 1.0f - ramp(flight.time, kFlyEnd, kPinch)
                : ramp(flight.time, kPinch, kSettle);
            frame.size.x *= std::max(0.02f, pinch);
        }
        frame.faceUp = flight.time >= kPinch;

        // Fading up off the pile only. Once it is moving it is fully opaque -
        // a translucent card in flight reads as a ghost rather than as a card.
        frame.alpha = ramp(flight.time, 0.0f, kLiftEnd * 0.8f);

        out.push_back(frame);
    }
    return out;
}

bool DrawFlight::hides(Side owner, int handIndex) const {
    for (const Flight& flight : m_cards) {
        if (flight.owner == owner && flight.handIndex == handIndex) return true;
    }
    return false;
}

bool DrawFlight::consumeFlip() {
    const bool was = m_flip;
    m_flip = false;
    return was;
}
