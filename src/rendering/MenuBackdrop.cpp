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
                              sf::Vector2f offset, float extraScale) const {
    const float tw = static_cast<float>(texture.getSize().x);
    const float th = static_cast<float>(texture.getSize().y);
    if (tw < 1.0f || th < 1.0f) return;

    // Cover the design box plus the bleed on all four sides. No setTextureRect
    // here on purpose: cropping the rect would leave the shader's texture
    // coordinates spanning the whole source image rather than the visible part,
    // and the wind ripple would key off the wrong x.
    const float scale = std::max((kDesignW + kBleed * 2.0f) / tw,
                                 (kDesignH + kBleed * 2.0f) / th) * extraScale;
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
        // sky barely moves; the character moves several times as far.
        placeCover(sprite, *m_sky, { m_smoothed.x * -4.0f, m_smoothed.y * -3.0f }, 1.0f);
        target.draw(sprite, wind);

        placeCover(sprite, *m_character,
                   { m_smoothed.x * -11.0f, m_smoothed.y * -7.0f + breathe * 2.0f },
                   1.0f + breathe * 0.006f);
        target.draw(sprite, wind);
    } else if (m_flat) {
        // One plane cannot have parallax, so it gets a drift instead: the whole
        // painting leans away from the cursor. Less convincing than two layers,
        // but honest motion rather than a still frame, and the wind ripple does
        // the rest of the work on the hair and ribbons.
        placeCover(sprite, *m_flat,
                   { m_smoothed.x * -7.0f, m_smoothed.y * -5.0f + breathe * 1.5f },
                   1.0f + breathe * 0.004f);
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
