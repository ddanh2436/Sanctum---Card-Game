#pragma once

#include <SFML/Graphics.hpp>
#include <vector>

/**
 * @brief The atmosphere around the end of a duel: rays, ash, cracks, a seal.
 *
 * This is a third layer beside the two that already exist, not a replacement
 * for either. EndScreen owns the timeline and the typography; PortraitRig
 * breaks the loser's portrait apart; this fills the space between them so the
 * moment reads as a climax rather than as one animation playing alone.
 *
 *   Victory   a gold wash, god rays turning out of the dead reactor, embers
 *             rising as the core is recovered, and a seal that spins down
 *             behind the verdict.
 *   Defeat    a black-red wash, the HUD glass fracturing out from the reactor,
 *             a vignette breathing on a slowing heartbeat, and ash falling.
 *
 * **No screen capture.** The obvious way to build this is to freeze the board
 * into an sf::RenderTexture and tint that, but the board is already drawn live
 * underneath and the duel is over, so nothing on it can change - a full-screen
 * render target would cost around eight megabytes to reproduce a picture that
 * is on the screen already. The wash goes straight over the live board instead.
 *
 * **The backdrop is optional.** Each outcome looks for a painting of its own -
 * assets/ui/victory_bg.png and defeat_bg.png, either extension, plus a couple of
 * alternate spellings - and draws it cover-fitted under the wash with a slow
 * push in. With no file there the effects play over the live board, which is a
 * complete picture on its own; the art is an upgrade, not a dependency.
 *
 * Everything is drawn in the 1280x720 design space and every particle is
 * batched into one draw call.
 */
class EndGameVFX {
public:
    enum class Outcome { Victory, Defeat };

    /// `focus` is the losing reactor: cracks radiate from it, rays turn about
    /// it. In design space.
    void begin(Outcome outcome, sf::Vector2f focus);
    void reset() { m_active = false; }
    void update(float dt);

    /// Over the board, under EndScreen's scrim: the wash, the rays, the cracks.
    void renderBelow(sf::RenderTarget& target);
    /// Over everything but the verdict text: particles and the seal.
    void renderAbove(sf::RenderTarget& target);

    bool active() const { return m_active; }

private:
    /// One backdrop per outcome, loaded the first time that outcome comes up
    /// and kept afterwards. `tried` is separate from `loaded` so a missing file
    /// is looked for once rather than on every duel.
    struct Backdrop {
        sf::Texture texture;
        bool tried = false;
        bool loaded = false;
    };

    struct Mote {
        sf::Vector2f position;
        sf::Vector2f velocity;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 1.0f;
        float size = 3.0f;
        float drift = 0.0f;   // horizontal sway rate
        float phase = 0.0f;
    };

    static constexpr int kMoteCap = 90;
    /// The seal lands with EndScreen's verdict, so the two arrive as one beat.
    static constexpr float kSealLand = 2.00f;

    Outcome m_outcome = Outcome::Defeat;
    sf::Vector2f m_focus{ 640.0f, 360.0f };
    bool m_active = false;
    float m_time = 0.0f;
    float m_spawnDebt = 0.0f;

    std::vector<Mote> m_motes;
    /// Built once when the sequence starts, then drawn with a growing vertex
    /// count so the fracture spreads rather than appearing whole.
    sf::VertexArray m_cracks{ sf::Lines };

    Backdrop m_backVictory;
    Backdrop m_backDefeat;

    Backdrop& backdrop();
    void drawBackdrop(sf::RenderTarget& target);
    void spawnMote();
    void buildCracks();
    void drawWash(sf::RenderTarget& target) const;
    void drawRays(sf::RenderTarget& target) const;
    void drawVignette(sf::RenderTarget& target) const;
    void drawSeal(sf::RenderTarget& target) const;
};
