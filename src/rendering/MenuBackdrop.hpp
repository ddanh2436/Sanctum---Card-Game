#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

/**
 * @brief The menu artwork, animated in code rather than played as a film.
 *
 * A video would cost more memory than the rest of the game together and could
 * not be re-timed, so the motion is three cheap effects layered over the stills:
 * a parallax drift that follows the cursor, a breathing pulse, and drifting
 * embers. One draw call carries every ember.
 *
 * **It adapts to whatever art exists.** Drop the two cut layers in and it runs
 * true two-plane parallax; with only the flat painting it drifts the whole
 * image and leans on the wind shader instead. Nothing has to be configured:
 *
 *   assets/ui/menu_bg_sky.png     sky, moon, skyline - the character removed
 *   assets/ui/menu_character.png  the character alone, transparent elsewhere
 *   assets/ui/menu_bg.jpg         the flat painting, used when those are absent
 *   assets/shaders/wind.frag      optional; skipped if missing or unsupported
 *
 * The whole thing is drawn in the 1280x720 design space, so the caller must
 * hand it cursor positions already mapped through the view - a raw pixel
 * position would drift the wrong way under letterboxing.
 */
class MenuBackdrop {
public:
    /// Load whatever art is present. Safe to call again; it reloads.
    void load();

    /// `designPos` must already be in 1280x720 space.
    void setCursor(sf::Vector2f designPos);
    void update(float dt);
    void render(sf::RenderTarget& target);

    /// False when not even the flat painting is on disk, so the caller can fall
    /// back to a plain fill rather than drawing nothing.
    bool hasArt() const { return m_flat || m_sky; }
    /// True only when both cut layers were found.
    bool layered() const { return m_sky && m_character; }
    bool windActive() const { return m_windReady; }

private:
    struct Ember {
        sf::Vector2f position;
        sf::Vector2f velocity;
        float swingPhase = 0.0f;
        float swingRate = 1.0f;
        float size = 4.0f;
        float rotation = 0.0f;
        float spin = 0.0f;
        sf::Color colour;
    };

    // Design-space margin the art is oversized by, so a parallax drift never
    // pulls an edge into frame.
    static constexpr float kBleed = 26.0f;
    static constexpr int kEmberCount = 34;

    const sf::Texture* m_flat = nullptr;
    const sf::Texture* m_sky = nullptr;
    const sf::Texture* m_character = nullptr;

    sf::Shader m_wind;
    bool m_windReady = false;

    std::vector<Ember> m_embers;
    float m_time = 0.0f;
    sf::Vector2f m_cursor;    // -1..1 on each axis
    sf::Vector2f m_smoothed;  // eased toward m_cursor

    void resetEmber(Ember& ember, bool anywhere);
    /// Cover 1280x720 plus kBleed on every side, scaled about the centre.
    void placeCover(sf::Sprite& sprite, const sf::Texture& texture,
                    sf::Vector2f offset, float extraScale) const;
};
