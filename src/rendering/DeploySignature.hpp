#pragma once

#include "battle/CardData.hpp"
#include "rendering/CombatVFX.hpp"

#include <SFML/System/Vector2.hpp>

/**
 * @brief What a frame does to the ground when it arrives, per doctrine.
 *
 * Every deployment used to play the same shockwave, dust and flash whichever
 * doctrine it belonged to, so a Vanguard walker and a Valkyrie landed
 * identically. Each doctrine now has a recipe of its own.
 *
 * The recipes are built from CombatVFX primitives rather than from six separate
 * particle systems, which is what keeps them looking like the same game: tuning
 * one primitive tunes every doctrine that uses it, and a new doctrine costs a
 * recipe rather than new drawing code.
 *
 * This lives outside DuelState so it can be rendered into an offscreen target
 * and checked. Catching a half-second landing by driving the real game meant
 * banking four turns of energy, guessing which card in hand was an affordable
 * unit, and then hoping a screenshot fell inside the window - three attempts
 * running it did not.
 */
namespace DeploySignature {

/// A camera shake the recipe asks for. Returned rather than applied, so the
/// caller owns the "screen shake" setting and the test can assert on it.
struct Shake {
    float seconds = 0.0f;
    float amplitude = 0.0f;
    bool wanted() const { return seconds > 0.0f && amplitude > 0.0f; }
};

/// Fire `card`'s doctrine landing at `at`. `unitId` is the frame that just
/// landed, for the effects that wash the frame itself.
Shake play(CombatVFX& vfx, const CardData& card, sf::Vector2f at, int unitId);

} // namespace DeploySignature
