#pragma once

#include "battle/Board.hpp"
#include <SFML/Graphics.hpp>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief The feedback layer for a duel: what just happened, and to whom.
 *
 * The rules engine resolves an action instantly. Without this, a card is simply
 * gone from the enemy hand and a frame is simply missing from the board, and
 * the player is left reading the log to find out why. Everything here exists to
 * put a beat of screen time on a decision that already happened.
 *
 * Nothing in here can change the game state. It is told what occurred and only
 * decides how it looks, which keeps the rules testable without a window.
 *
 * Drawing is split in two so effects land on the right side of the board:
 * renderBelow() paints beams and shockwaves under the frames, renderAbove()
 * paints debris, arrows and the played-card reveal over them.
 */
class CombatVFX {
public:
    void setFont(const sf::Font& font) { m_font = &font; }

    // ---- spawners -----------------------------------------------------------

    /// Metal and fire thrown out from a destroyed frame.
    void explosion(sf::Vector2f centre, sf::Color accent);
    /// A short burst where a blow lands, thrown along `dir`.
    void impact(sf::Vector2f at, sf::Vector2f dir, sf::Color accent);
    /// Deflection sparks: the hit was absorbed rather than felt.
    void sparks(sf::Vector2f at, sf::Color accent);
    /// An expanding ring - a spell resolving, a counter-protocol firing.
    void shockwave(sf::Vector2f centre, sf::Color colour, float maxRadius, float seconds = 0.62f);
    /// A straight energy lance between two points, for ranged fire and spells.
    void beam(sf::Vector2f from, sf::Vector2f to, sf::Color colour, float seconds = 0.42f);
    /// A curved slash arc across a target.
    void slash(sf::Vector2f at, sf::Vector2f dir, sf::Color colour);
    /// Dust thrown up where a frame lands.
    void smoke(sf::Vector2f at, int count);
    /// A rotating reticle over the frame an attack is aimed at.
    void crosshair(sf::Vector2f at, float seconds = 0.55f);

    /// Squash a freshly landed frame and let it spring back, so a deployment
    /// reads as a heavy thing hitting the deck.
    void slam(int unitId);
    /// Vertical scale for a frame, 1.0 unless it is mid-slam.
    float squashFor(int unitId) const;
    /// 1 down to 0 across the slam, for the one-beat flare on the stat badges.
    float badgePulseFor(int unitId) const;

    /// A hex landing zone that spins open where a frame is about to touch down.
    void dropMarker(sf::Vector2f at, sf::Color accent);

    /// Flip a face-down counter-protocol over in its own slot: the card
    /// narrows to nothing showing its back, then opens out showing its face.
    void trapFlip(sf::FloatRect box, const CardData& card, Side owner, sf::Color accent);

    /// A card locking into place: its border runs bright, then settles into the
    /// armed colour, with sparks chasing round the frame. Used when a counter-
    /// protocol is set, where there is no flip to watch and the only thing that
    /// changes is the card's status.
    void armFlare(sf::FloatRect box, sf::Color colour, float seconds = 1.30f);
    /// True while a flip is still turning, so the caller can hold the spotlight
    /// until the card has actually turned over.
    bool flipping() const { return !m_flips.empty(); }

    /// Wash a frame in colour for a moment: white for a hit it shrugged off,
    /// red for real damage. The board renderer paints it through overlayFor(),
    /// which keeps CardArt::drawUnit free of any notion of combat state.
    void flash(int unitId, sf::Color colour, float seconds = 0.26f);
    /// Shove a frame toward `direction` and let it slide back - the lunge that
    /// makes an attack read as an attack rather than a number changing.
    void lunge(int unitId, sf::Vector2f direction, float distance = 26.0f);

    /// A targeting arrow, the way KARDS draws one while you pick a target. Used
    /// live while the player drags, and replayed for the enemy's attacks so
    /// their choice of target is visible.
    void arrow(sf::Vector2f from, sf::Vector2f to, sf::Color colour, float seconds = 0.9f);
    /// Hold the arrow open indefinitely (the player is still dragging).
    void setLiveArrow(bool active, sf::Vector2f from = {}, sf::Vector2f to = {},
                      sf::Color colour = sf::Color::White, bool valid = true);

    // ---- queries used while drawing the board -------------------------------

    /// Colour to paint over a frame this instant. Alpha 0 means leave it alone.
    sf::Color overlayFor(int unitId) const;
    /// Positional offset for a frame, zero when it is standing still.
    sf::Vector2f offsetFor(int unitId) const;

    void update(float dt);
    void renderBelow(sf::RenderTarget& target);
    void renderAbove(sf::RenderTarget& target);
    void clear();

    size_t particleCount() const { return m_particles.size(); }

private:
    struct Particle {
        sf::Vector2f position;
        sf::Vector2f velocity;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 1.0f;
        float size = 3.0f;
        float gravity = 0.0f;
        float drag = 1.0f;
        bool fade = true;
    };

    struct Ring {
        sf::Vector2f centre;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 0.62f;
        float maxRadius = 90.0f;
    };

    struct Beam {
        sf::Vector2f from, to;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 0.42f;
        float width = 7.0f;
    };

    struct Arc {
        sf::Vector2f centre;
        float angle = 0.0f;      // direction the slash travels
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 0.32f;
        float radius = 46.0f;
    };

    struct Flash {
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 0.26f;
    };

    struct Lunge {
        sf::Vector2f direction;
        float distance = 26.0f;
        float life = 0.0f;
        float maxLife = 0.48f;
    };

    struct Crosshair {
        sf::Vector2f at;
        float life = 0.0f;
        float maxLife = 0.55f;
    };

    struct Slam {
        float life = 0.0f;
        float maxLife = 0.30f;
    };

    struct DropMarker {
        sf::Vector2f at;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 0.48f;
    };

    struct TrapFlip {
        sf::FloatRect box;
        CardData card;
        Side owner = Side::Player;
        sf::Color accent;
        float life = 0.0f;
        float maxLife = 0.44f;
    };

    struct ArmFlare {
        sf::FloatRect box;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 1.30f;
    };

    struct Arrow {
        sf::Vector2f from, to;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 0.9f;
    };

    std::vector<Particle> m_particles;
    std::vector<Ring> m_rings;
    std::vector<Beam> m_beams;
    std::vector<Arc> m_arcs;
    std::vector<Arrow> m_arrows;
    std::vector<Crosshair> m_crosshairs;
    std::vector<DropMarker> m_markers;
    std::vector<TrapFlip> m_flips;
    std::vector<ArmFlare> m_armFlares;
    std::unordered_map<int, Slam> m_slams;
    std::unordered_map<int, Flash> m_flashes;
    std::unordered_map<int, Lunge> m_lunges;

    bool m_liveArrow = false;
    bool m_liveArrowValid = true;
    sf::Vector2f m_liveFrom, m_liveTo;
    sf::Color m_liveColour = sf::Color::White;

    const sf::Font* m_font = nullptr;

    void drawArrow(sf::RenderTarget& target, sf::Vector2f from, sf::Vector2f to,
                   sf::Color colour, float alpha, bool valid) const;
};
