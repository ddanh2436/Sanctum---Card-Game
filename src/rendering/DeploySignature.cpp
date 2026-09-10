#include "rendering/DeploySignature.hpp"

#include "rendering/CardArt.hpp"

namespace DeploySignature {

Shake play(CombatVFX& vfx, const CardData& card, sf::Vector2f at, int unitId) {
    switch (card.role) {
    case MechRole::Vanguard:
        // Armour and shields: the deck splits under the weight. One decisive
        // vertical jolt, not a rumble - a Vanguard plants itself.
        //
        // Pale steel, and a 150px reach. The first pass used a dark grey that
        // was darker than the battlefield art itself, at a 78px reach - which,
        // against a 112px unit tile drawn on top of it, put nearly the whole
        // fissure behind the very card that caused it.
        vfx.groundCracks(at, sf::Color(176, 186, 204), 8, 150.0f, 0.55f);
        vfx.smoke(at, 16);
        vfx.shockwave(at, sf::Color(150, 158, 172), 82.0f, 0.44f);
        return { 0.12f, 4.0f };

    case MechRole::Paladin:
        // Arclight: a plasma drop. The shaft arrives first and the frame is
        // already standing in it, which is why the column is drawn under the
        // rows rather than over them.
        vfx.lightColumn(at, sf::Color(255, 226, 150), 78.0f, 0.30f);
        vfx.flash(unitId, sf::Color(255, 255, 208, 210), 0.34f);
        vfx.shockwave(at, sf::Color(150, 220, 240), 96.0f, 0.50f);
        vfx.sparks(at, sf::Color(255, 236, 180));
        return {};

    case MechRole::Dragoon:
        // Real dragons and jet wash: fire thrown flat along the deck, so the
        // ring reads as a shock front rather than as a fireball going up.
        vfx.emberRing(at, sf::Color(255, 132, 48), 22);
        vfx.shockwave(at, sf::Color(255, 150, 70), 128.0f, 0.52f);
        vfx.impact(at, { 0.0f, -1.0f }, sf::Color(255, 168, 84));
        vfx.smoke(at, 10);
        return { 0.18f, 5.0f };

    case MechRole::Inquisitor:
        // Overseer: an EMP wash and static crawling round the cell. Two rings
        // at different rates, so it reads as a pulse rather than as one ripple.
        vfx.shockwave(at, sf::Color(186, 128, 236), 118.0f, 0.60f);
        vfx.shockwave(at, sf::Color(140, 100, 210), 74.0f, 0.38f);
        vfx.boltRing(at, sf::Color(198, 148, 255), 8, 58.0f, 0.36f);
        vfx.flash(unitId, sf::Color(196, 150, 255, 150), 0.30f);
        return {};

    case MechRole::Siege:
        // Fortress and golem: the heaviest thing in the game arriving. Slow
        // dust, and a shake longer than anything else in the game throws.
        vfx.smoke(at, 26);
        vfx.groundCracks(at, sf::Color(214, 184, 138), 7, 180.0f, 0.70f);
        vfx.shockwave(at, sf::Color(168, 146, 112), 110.0f, 0.66f);
        return { 0.35f, 6.0f };

    case MechRole::Valkyrie:
        // Weightless: no crack, no dust, nothing thrown outward, and the only
        // landing in the game that asks for no shake at all. It settles.
        vfx.feathers(at, sf::Color(150, 232, 214), 10);
        vfx.shockwave(at, sf::Color(150, 232, 214), 70.0f, 0.72f);
        vfx.flash(unitId, sf::Color(200, 255, 240, 120), 0.42f);
        return {};
    }

    // No default label above, so a doctrine added later fails to compile rather
    // than silently landing with nothing.
    vfx.shockwave(at, CardArt::accentFor(card), 96.0f, 0.62f);
    vfx.smoke(at, 14);
    return {};
}

} // namespace DeploySignature
