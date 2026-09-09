#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

/**
 * @brief Animates a commander portrait by moving its pieces, not by playing a
 *        film strip.
 *
 * A frame-by-frame sequence would need a dozen consistent drawings per second
 * of motion, and would cost a hundred megabytes of texture for an effect seen
 * once a duel. This takes one illustration cut into a handful of layers and
 * animates each layer's transform, which is how most 2D games move a character.
 *
 * Parts are looked up as `<stem>_head.png`, `_torso.png`, `_armL.png`,
 * `_armR.png`, `_wingL.png`, `_wingR.png`, `_cape.png`. Every part is expected
 * to be the *same canvas size* as the whole portrait with everything else
 * transparent - that is what "export layers" gives you out of Photoshop or
 * GIMP, and it means no offsets have to be recorded anywhere.
 *
 * When no parts exist it falls back to slicing the single portrait into
 * horizontal bands. That is not a stand-in for the real thing, but on a mech
 * commander a banded break-up reads as a display losing signal, so the effect
 * works before any art is cut and improves the moment some is.
 */
class PortraitRig {
public:
    /// Load `stem`'s layers, or fall back to banding `stem` itself.
    /// Returns false when even the base portrait is missing.
    bool load(const std::string& stem);

    /// `height` is the portrait's drawn height in design-space pixels.
    void begin(sf::Vector2f centre, float height);
    void reset() { m_running = false; }

    void update(float dt);
    void render(sf::RenderTarget& target);

    bool running() const { return m_running; }
    bool loaded() const { return !m_parts.empty(); }
    /// True when the pieces came from real cut layers rather than banding.
    bool hasCutLayers() const { return m_cutLayers; }

private:
    struct Part {
        const sf::Texture* texture = nullptr;
        sf::IntRect rect;            // sub-rectangle of the source
        sf::Vector2f pivot;          // centre of the part's opaque pixels, in source pixels

        // Motion, assigned per mode when the animation starts.
        sf::Vector2f velocity;
        float spin = 0.0f;
        float delay = 0.0f;
    };

    std::vector<Part> m_parts;
    sf::Vector2f m_sourceSize;
    sf::Vector2f m_centre;
    float m_scale = 1.0f;
    float m_time = 0.0f;
    bool m_running = false;
    bool m_cutLayers = false;

    void addWholeAsBands(const sf::Texture& texture, int bands);
    bool addLayer(const std::string& path);
};
