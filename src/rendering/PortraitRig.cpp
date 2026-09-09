#include "rendering/PortraitRig.hpp"
#include "utils/ResourceManager.hpp"
#include "utils/Rng.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

/// The layer names the rig looks for, back to front. Order is the draw order,
/// so a cape sits behind the torso and a weapon in front of the arm.
const char* kLayerSuffixes[] = {
    "_cape", "_wingL", "_wingR", "_torso", "_armL", "_armR", "_head", "_weapon",
};

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

/// Centre of a region's opaque pixels. Rotating a part about the middle of its
/// own canvas would swing a head around the portrait's belt buckle; this finds
/// where the part actually is.
sf::Vector2f opaqueCentre(const sf::Image& image, sf::IntRect region) {
    const sf::Vector2u size = image.getSize();
    int minX = region.left + region.width, maxX = region.left;
    int minY = region.top + region.height, maxY = region.top;

    // Every fourth pixel is plenty to find a bounding box and costs a sixteenth
    // of the work on a 1024x1024 layer.
    for (int y = region.top; y < region.top + region.height; y += 4) {
        if (y < 0 || y >= static_cast<int>(size.y)) continue;
        for (int x = region.left; x < region.left + region.width; x += 4) {
            if (x < 0 || x >= static_cast<int>(size.x)) continue;
            if (image.getPixel(static_cast<unsigned>(x), static_cast<unsigned>(y)).a < 24) continue;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    }
    if (minX > maxX || minY > maxY) {   // fully transparent
        return { region.left + region.width / 2.0f, region.top + region.height / 2.0f };
    }
    return { (minX + maxX) / 2.0f, (minY + maxY) / 2.0f };
}

} // namespace

bool PortraitRig::addLayer(const std::string& path) {
    if (!ResourceManager::exists(path)) return false;

    const sf::Texture& tex = ResourceManager::get().getTexture(path);
    if (tex.getSize().x <= 1) return false;

    Part part;
    part.texture = &tex;
    part.rect = sf::IntRect(0, 0, static_cast<int>(tex.getSize().x),
                            static_cast<int>(tex.getSize().y));
    const sf::Image image = tex.copyToImage();
    part.pivot = opaqueCentre(image, part.rect);
    m_parts.push_back(part);

    m_sourceSize = { static_cast<float>(tex.getSize().x),
                     static_cast<float>(tex.getSize().y) };
    return true;
}

void PortraitRig::addWholeAsBands(const sf::Texture& texture, int bands) {
    const int w = static_cast<int>(texture.getSize().x);
    const int h = static_cast<int>(texture.getSize().y);
    m_sourceSize = { static_cast<float>(w), static_cast<float>(h) };

    const int band = std::max(1, h / bands);
    for (int i = 0; i < bands; ++i) {
        const int top = i * band;
        const int height = (i == bands - 1) ? h - top : band;
        if (height <= 0) break;

        Part part;
        part.texture = &texture;
        part.rect = sf::IntRect(0, top, w, height);
        // A band is solid across its width, so its own middle is the pivot.
        part.pivot = { w / 2.0f, top + height / 2.0f };
        m_parts.push_back(part);
    }
}

bool PortraitRig::load(const std::string& stem) {
    m_parts.clear();
    m_cutLayers = false;
    m_running = false;

    const std::size_t dot = stem.find_last_of('.');
    const std::string base = dot == std::string::npos ? stem : stem.substr(0, dot);

    for (const char* suffix : kLayerSuffixes) {
        if (addLayer(base + suffix + ".png")) m_cutLayers = true;
    }
    if (m_cutLayers) return true;

    // No cut layers: band the portrait itself.
    if (!ResourceManager::exists(stem)) return false;
    const sf::Texture& tex = ResourceManager::get().getTexture(stem);
    if (tex.getSize().x <= 1) return false;
    addWholeAsBands(tex, 9);
    return !m_parts.empty();
}

void PortraitRig::begin(sf::Vector2f centre, float height) {
    if (m_parts.empty()) return;

    m_centre = centre;
    m_scale = m_sourceSize.y > 0.0f ? height / m_sourceSize.y : 1.0f;
    m_time = 0.0f;
    m_running = true;

    const float mid = m_sourceSize.y / 2.0f;
    for (std::size_t i = 0; i < m_parts.size(); ++i) {
        Part& p = m_parts[i];
        // Pieces high on the portrait leave first and travel further, so the
        // figure comes apart from the top down rather than all at once.
        const float fromTop = clamp01((mid + (mid - p.pivot.y)) / m_sourceSize.y);

        const float sideways = (p.pivot.x - m_sourceSize.x / 2.0f) / m_sourceSize.x;
        p.velocity = { sideways * Rng::rangeF(190.0f, 330.0f) + Rng::rangeF(-70.0f, 70.0f),
                       Rng::rangeF(-250.0f, -90.0f) - fromTop * 90.0f };
        p.spin = Rng::rangeF(-1.9f, 1.9f);
        // Not before 0.55s: the scrim starts dropping at 0.50, and pieces flying
        // across a board that is still at full brightness read as clutter rather
        // than as a portrait coming apart.
        p.delay = 0.55f + (1.0f - fromTop) * 0.40f + Rng::rangeF(0.0f, 0.08f);
    }
}

void PortraitRig::update(float dt) {
    if (!m_running || dt <= 0.0f) return;
    m_time += dt;
    if (m_time > 4.0f) m_running = false;
}

void PortraitRig::render(sf::RenderTarget& target) {
    if (!m_running || m_parts.empty()) return;

    for (const Part& part : m_parts) {
        if (!part.texture) continue;

        sf::Sprite sprite(*part.texture, part.rect);
        // Origin is the part's own pivot expressed inside its sub-rectangle.
        sprite.setOrigin(part.pivot.x - static_cast<float>(part.rect.left),
                         part.pivot.y - static_cast<float>(part.rect.top));
        sprite.setScale(m_scale, m_scale);

        // Rest position: where this piece sits inside the assembled portrait.
        const sf::Vector2f rest = m_centre
            + sf::Vector2f(part.pivot.x - m_sourceSize.x / 2.0f,
                           part.pivot.y - m_sourceSize.y / 2.0f) * m_scale;

        sf::Vector2f offset(0.0f, 0.0f);
        float rotation = 0.0f;
        float alpha = 1.0f;
        sf::Color tint(255, 255, 255);

        if (m_time < part.delay) {
            // Still holding together, but failing: a rising judder.
            const float stress = m_time / std::max(0.01f, part.delay);
            const float shake = 1.0f + stress * 5.0f;
            offset = { Rng::rangeF(-shake, shake), Rng::rangeF(-shake, shake) };
            const sf::Uint8 heat = static_cast<sf::Uint8>(255 - 70 * stress);
            tint = sf::Color(255, heat, heat);
        } else {
            const float t = m_time - part.delay;
            offset = { part.velocity.x * t,
                       part.velocity.y * t + 520.0f * t * t };   // gravity
            rotation = part.spin * t * 90.0f;
            alpha = clamp01(1.0f - t / 1.60f);
        }
        // Fade the whole figure in rather than snapping it over the board the
        // instant the reactor fails.
        alpha *= clamp01(m_time / 0.22f);

        if (alpha <= 0.01f) continue;
        sprite.setPosition(rest + offset);
        sprite.setRotation(rotation);
        sprite.setColor(sf::Color(tint.r, tint.g, tint.b,
                                  static_cast<sf::Uint8>(alpha * 255.0f)));
        target.draw(sprite);
    }
}
