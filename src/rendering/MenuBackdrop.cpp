#include "rendering/MenuBackdrop.hpp"

#include "utils/ResourceManager.hpp"
#include "utils/Rng.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 720.0f;

/// Loaded only if it is really there; a 1x1 placeholder means "missing".
const sf::Texture* loadIfPresent(const std::string& path) {
    if (!ResourceManager::exists(path)) return nullptr;
    const sf::Texture& tex = ResourceManager::get().getTexture(path);
    return tex.getSize().x > 1 ? &tex : nullptr;
}

} // namespace

void MenuBackdrop::load() {
    m_flat = loadIfPresent("assets/ui/menu_bg.png");
    m_sky = loadIfPresent("assets/ui/menu_bg_sky.png");
    m_character = loadIfPresent("assets/ui/menu_character.png");

    // The wind ripple is a nicety, not a requirement: an old driver, a software
    // renderer or a missing .frag all end up here and the menu simply runs
    // without it rather than failing to draw.
    m_windReady = false;
    if (sf::Shader::isAvailable() && ResourceManager::exists("assets/shaders/wind.frag")) {
        m_windReady = m_wind.loadFromFile("assets/shaders/wind.frag", sf::Shader::Fragment);
        if (m_windReady) m_wind.setUniform("texture", sf::Shader::CurrentTexture);
    }

    m_embers.resize(kEmberCount);
    for (Ember& ember : m_embers) resetEmber(ember, true);
}

void MenuBackdrop::resetEmber(Ember& ember, bool anywhere) {
    // They travel down and to the left, following the sweep of the hair and the
    // ribbons in the painting, so they read as caught in the same wind.
    if (anywhere) {
        ember.position = { Rng::rangeF(-40.0f, kDesignW + 40.0f),
                           Rng::rangeF(-40.0f, kDesignH) };
    } else {
        ember.position = { Rng::rangeF(kDesignW * 0.35f, kDesignW + 80.0f),
                           Rng::rangeF(-60.0f, -10.0f) };
    }
    ember.velocity = { Rng::rangeF(-96.0f, -38.0f), Rng::rangeF(26.0f, 74.0f) };
    ember.swingPhase = Rng::rangeF(0.0f, 6.283f);
    ember.swingRate = Rng::rangeF(1.1f, 2.6f);
    ember.size = Rng::rangeF(2.4f, 6.2f);
    ember.rotation = Rng::rangeF(0.0f, 360.0f);
    ember.spin = Rng::rangeF(-95.0f, 95.0f);
    ember.colour = sf::Color(static_cast<sf::Uint8>(Rng::range(198, 246)),
                             static_cast<sf::Uint8>(Rng::range(28, 74)),
                             static_cast<sf::Uint8>(Rng::range(44, 82)),
                             static_cast<sf::Uint8>(Rng::range(120, 220)));
}

void MenuBackdrop::setCursor(sf::Vector2f designPos) {
    m_cursor = { std::clamp(designPos.x / kDesignW * 2.0f - 1.0f, -1.0f, 1.0f),
                 std::clamp(designPos.y / kDesignH * 2.0f - 1.0f, -1.0f, 1.0f) };
}

void MenuBackdrop::update(float dt) {
    m_time += dt;

    // Ease toward the cursor instead of snapping to it. Following the pointer
    // exactly makes the parallax feel welded to the mouse rather than like a
    // camera with weight, and it jitters on every small movement.
    const float follow = std::min(1.0f, dt * 4.5f);
    m_smoothed += (m_cursor - m_smoothed) * follow;

    for (Ember& ember : m_embers) {
        ember.swingPhase += dt * ember.swingRate;
        const float swing = std::sin(ember.swingPhase) * 34.0f;
        ember.position.x += (ember.velocity.x + swing) * dt;
        ember.position.y += ember.velocity.y * dt;
        ember.rotation += ember.spin * dt;

        if (ember.position.y > kDesignH + 40.0f || ember.position.x < -60.0f) {
            resetEmber(ember, false);
        }
    }
}

void MenuBackdrop::placeCover(sf::Sprite& sprite, const sf::Texture& texture,
                              sf::Vector2f offset, float extraScale, float bleed) const {
    const float tw = static_cast<float>(texture.getSize().x);
    const float th = static_cast<float>(texture.getSize().y);
    if (tw < 1.0f || th < 1.0f) return;

    // Cover the design box plus the bleed on all four sides. No setTextureRect
    // here on purpose: cropping the rect would leave the shader's texture
    // coordinates spanning the whole source image rather than the visible part,
    // and the wind ripple would key off the wrong x.
    const float scale = std::max((kDesignW + bleed * 2.0f) / tw,
                                 (kDesignH + bleed * 2.0f) / th) * extraScale;
    sprite.setTexture(texture, true);
    sprite.setOrigin(tw / 2.0f, th / 2.0f);
    sprite.setScale(scale, scale);
    sprite.setPosition(kDesignW / 2.0f + offset.x, kDesignH / 2.0f + offset.y);
}

void MenuBackdrop::render(sf::RenderTarget& target) {
    if (!hasArt()) return;

    if (m_windReady) m_wind.setUniform("time", m_time);
    const sf::Shader* wind = m_windReady ? &m_wind : nullptr;

    // A slow swell, so the frame is never perfectly still even with the cursor
    // parked. 0.6% - large enough to notice out of the corner of the eye, small
    // enough that nothing visibly grows.
    const float breathe = std::sin(m_time * 1.7f);

    sf::Sprite sprite;
    if (layered()) {
        // Two planes at different rates is what actually reads as depth. The
        // sky barely moves; the character moves several times as far. Both the
        // drift and the breathing are safe here because each acts on its own
        // plane - the moon does not lurch when the figure leans.
        // The sky is drawn WITHOUT the wind, on purpose. Rippling it moved the
        // clouds and the moon's rim as much as it moved the hair - measured at
        // 12.6 against the hair's 11.6 - which is the same "the whole picture is
        // moving" fault as before, just wearing the background's clothes. A
        // backdrop should hold still; only the figure is caught in the wind.
        placeCover(sprite, *m_sky, { m_smoothed.x * -4.0f, m_smoothed.y * -3.0f },
                   1.0f, kBleedLayered);
        target.draw(sprite);

        placeCover(sprite, *m_character,
                   { m_smoothed.x * -11.0f, m_smoothed.y * -7.0f + breathe * 2.0f },
                   1.0f + breathe * 0.006f, kBleedLayered);
        target.draw(sprite, wind);
    } else if (m_flat) {
        // Drawn dead still, on purpose.
        //
        // With one layer there is nothing to move RELATIVE to anything else: a
        // parallax drift slides the moon, the skyline and the figure together,
        // and a breathing scale swells all three at once. That is not depth, it
        // is the whole picture sliding, and it looks like it. The first version
        // did exactly that and the painting visibly wandered.
        //
        // So the flat path transforms nothing and leaves every pixel where the
        // artist put it. The shader is the only thing that moves, and it is
        // confined to the hair and the ribbons; the embers are separate objects
        // drifting over the top. Both come back the moment the two cut layers
        // appear, because then they have separate planes to act on.
        (void)breathe;
        placeCover(sprite, *m_flat, { 0.0f, 0.0f }, 1.0f, kBleedFlat);
        target.draw(sprite, wind);
    }

    // Embers last, over everything, in one batched draw.
    sf::VertexArray petals(sf::Quads, m_embers.size() * 4);
    for (std::size_t i = 0; i < m_embers.size(); ++i) {
        const Ember& ember = m_embers[i];
        const float rad = ember.rotation * 3.14159265f / 180.0f;
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        const float w = ember.size * 0.55f;
        const float h = ember.size;

        // A leaf rather than a square: two long points and two short flanks.
        // Rotated properly, which is what stops a drifting field of these from
        // reading as falling confetti.
        const sf::Vector2f shape[4] = {
            {  0.0f,      -h },
            {  w,         -h * 0.15f },
            {  w * 0.25f,  h },
            { -w * 0.85f,  h * 0.35f },
        };
        for (int k = 0; k < 4; ++k) {
            const sf::Vector2f p = shape[k];
            petals[i * 4 + k].position = ember.position
                                       + sf::Vector2f(p.x * c - p.y * s, p.x * s + p.y * c);
            petals[i * 4 + k].color = ember.colour;
        }
    }
    target.draw(petals);
}
