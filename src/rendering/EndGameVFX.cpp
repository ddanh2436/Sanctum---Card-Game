#include "rendering/EndGameVFX.hpp"

#include "utils/Rng.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr float kW = 1280.0f;
constexpr float kH = 720.0f;
constexpr float kPi = 3.14159265f;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/// 0 before `from`, 1 after `to`, linear between.
float ramp(float t, float from, float to) {
    return to <= from ? 1.0f : clamp01((t - from) / (to - from));
}

sf::Uint8 a8(float v) { return static_cast<sf::Uint8>(clamp01(v) * 255.0f); }

} // namespace

void EndGameVFX::begin(Outcome outcome, sf::Vector2f focus) {
    m_outcome = outcome;
    m_focus = focus;
    m_active = true;
    m_time = 0.0f;
    m_spawnDebt = 0.0f;
    m_motes.clear();
    m_motes.reserve(kMoteCap);
    m_cracks.clear();
    if (m_outcome == Outcome::Defeat) buildCracks();
}

void EndGameVFX::buildCracks() {
    // Branching, not straight spokes. A crack that runs dead straight from the
    // centre to the edge reads as a drawn line; one that kinks every so often
    // and throws off a shorter splinter reads as glass giving way.
    const int spokes = 16;
    for (int i = 0; i < spokes; ++i) {
        const float base = (i / static_cast<float>(spokes)) * 2.0f * kPi
                         + Rng::rangeF(-0.16f, 0.16f);

        sf::Vector2f cursor = m_focus;
        float angle = base;
        for (int step = 0; step < 6; ++step) {
            const float len = Rng::rangeF(46.0f, 120.0f);
            const sf::Vector2f next = cursor
                + sf::Vector2f(std::cos(angle) * len, std::sin(angle) * len);

            // Hot at the impact, cooling as it travels out.
            const float near = 1.0f - step / 6.0f;
            const sf::Color hot(255, static_cast<sf::Uint8>(90 + 120 * near),
                                static_cast<sf::Uint8>(80 + 90 * near),
                                a8(0.35f + 0.45f * near));
            const sf::Color cold(226, 58, 54, a8(0.20f + 0.35f * near));
            m_cracks.append(sf::Vertex(cursor, hot));
            m_cracks.append(sf::Vertex(next, cold));

            // A splinter off the joint, every other step or so.
            if (Rng::chance(45)) {
                const float sa = angle + Rng::rangeF(-1.1f, 1.1f);
                const float sl = len * Rng::rangeF(0.25f, 0.6f);
                m_cracks.append(sf::Vertex(next, cold));
                m_cracks.append(sf::Vertex(
                    next + sf::Vector2f(std::cos(sa) * sl, std::sin(sa) * sl),
                    sf::Color(226, 58, 54, 40)));
            }

            cursor = next;
            angle += Rng::rangeF(-0.42f, 0.42f);
            if (cursor.x < -80.0f || cursor.x > kW + 80.0f ||
                cursor.y < -80.0f || cursor.y > kH + 80.0f) {
                break;
            }
        }
    }
}

void EndGameVFX::spawnMote() {
    if (static_cast<int>(m_motes.size()) >= kMoteCap) return;

    Mote mote;
    mote.phase = Rng::rangeF(0.0f, 2.0f * kPi);
    mote.drift = Rng::rangeF(0.8f, 2.4f);

    if (m_outcome == Outcome::Victory) {
        // Rising, because the core is being recovered rather than lost. Gold
        // going up is the whole read of the effect; the same particle falling
        // would say the opposite thing.
        mote.position = { Rng::rangeF(0.0f, kW), kH + Rng::rangeF(4.0f, 40.0f) };
        mote.velocity = { Rng::rangeF(-16.0f, 16.0f), Rng::rangeF(-96.0f, -44.0f) };
        mote.colour = sf::Color(255,
                                static_cast<sf::Uint8>(Rng::range(196, 236)),
                                static_cast<sf::Uint8>(Rng::range(96, 168)), 230);
        mote.size = Rng::rangeF(1.6f, 3.6f);
        mote.maxLife = Rng::rangeF(1.8f, 3.4f);
    } else {
        mote.position = { Rng::rangeF(0.0f, kW), Rng::rangeF(-40.0f, -6.0f) };
        mote.velocity = { Rng::rangeF(-22.0f, 10.0f), Rng::rangeF(34.0f, 86.0f) };
        // Mostly dark ash with the occasional live cinder still burning.
        const bool cinder = Rng::chance(28);
        mote.colour = cinder
            ? sf::Color(255, static_cast<sf::Uint8>(Rng::range(70, 130)), 40, 235)
            : sf::Color(static_cast<sf::Uint8>(Rng::range(58, 96)),
                        static_cast<sf::Uint8>(Rng::range(48, 72)),
                        static_cast<sf::Uint8>(Rng::range(48, 68)), 205);
        mote.size = cinder ? Rng::rangeF(1.4f, 2.8f) : Rng::rangeF(2.0f, 4.6f);
        mote.maxLife = Rng::rangeF(2.2f, 4.0f);
    }
    mote.life = mote.maxLife;
    m_motes.push_back(mote);
}

void EndGameVFX::update(float dt) {
    if (!m_active || dt <= 0.0f) return;
    m_time += dt;

    // Held back for the first beat: EndScreen's glitch phase is already busy,
    // and starting the weather on top of it makes the opening unreadable.
    if (m_time > 0.35f) {
        m_spawnDebt += dt * 34.0f;
        while (m_spawnDebt >= 1.0f) {
            m_spawnDebt -= 1.0f;
            spawnMote();
        }
    }

    for (std::size_t i = 0; i < m_motes.size();) {
        Mote& mote = m_motes[i];
        mote.life -= dt;
        if (mote.life <= 0.0f) {
            mote = m_motes.back();
            m_motes.pop_back();
            continue;
        }
        mote.phase += dt * mote.drift;
        mote.position.x += (mote.velocity.x + std::sin(mote.phase) * 16.0f) * dt;
        mote.position.y += mote.velocity.y * dt;
        ++i;
    }
}

EndGameVFX::Backdrop& EndGameVFX::backdrop() {
    Backdrop& slot = (m_outcome == Outcome::Victory) ? m_backVictory : m_backDefeat;
    if (slot.tried) return slot;
    slot.tried = true;

    // Several spellings, because the file arrives from the author rather than
    // from this code and guessing wrong should not mean silence. First hit wins.
    const std::vector<std::string> stems =
        (m_outcome == Outcome::Victory)
            ? std::vector<std::string>{ "victory_bg", "victory", "endgame_victory" }
            : std::vector<std::string>{ "defeat_bg", "defeat", "endgame_defeat" };
    static const char* kExts[] = { ".png", ".jpg", ".jpeg", ".webp" };

    for (const std::string& stem : stems) {
        for (const char* ext : kExts) {
            if (slot.texture.loadFromFile("assets/ui/" + stem + ext)) {
                slot.texture.setSmooth(true);
                slot.loaded = true;
                return slot;
            }
        }
    }
    return slot;
}

void EndGameVFX::drawBackdrop(sf::RenderTarget& target) {
    Backdrop& slot = backdrop();
    if (!slot.loaded) return;

    const sf::Vector2u size = slot.texture.getSize();
    if (size.x == 0 || size.y == 0) return;

    // Cover-fit, then a slow push in over the whole sequence. setTextureRect is
    // deliberately not used - scaling a full sprite keeps the crop centred no
    // matter what aspect the author's painting happens to be.
    const float push = 1.0f + 0.04f * clamp01(m_time / 6.0f);
    const float scale = std::max(kW / size.x, kH / size.y) * push;

    sf::Sprite sprite(slot.texture);
    sprite.setOrigin(size.x * 0.5f, size.y * 0.5f);
    sprite.setPosition(kW * 0.5f, kH * 0.5f);
    sprite.setScale(scale, scale);
    // Fades in with the wash rather than cutting, so the board it replaces is
    // still readable through the first half-second.
    sprite.setColor(sf::Color(255, 255, 255, a8(ramp(m_time, 0.05f, 0.90f))));
    target.draw(sprite);
}

void EndGameVFX::drawWash(sf::RenderTarget& target) const {
    // A flat wash rather than a captured-and-tinted frame: the board underneath
    // is live and already correct, so this only has to change its colour.
    const float in = ramp(m_time, 0.10f, 1.10f);
    sf::VertexArray wash(sf::TriangleStrip, 4);

    if (m_outcome == Outcome::Victory) {
        const sf::Color top(58, 44, 12, a8(in * 0.62f));
        const sf::Color bottom(14, 28, 34, a8(in * 0.50f));
        wash[0] = sf::Vertex({ 0.0f, 0.0f }, top);
        wash[1] = sf::Vertex({ kW, 0.0f }, top);
        wash[2] = sf::Vertex({ 0.0f, kH }, bottom);
        wash[3] = sf::Vertex({ kW, kH }, bottom);
    } else {
        const sf::Color top(30, 4, 6, a8(in * 0.70f));
        const sf::Color bottom(8, 2, 4, a8(in * 0.80f));
        wash[0] = sf::Vertex({ 0.0f, 0.0f }, top);
        wash[1] = sf::Vertex({ kW, 0.0f }, top);
        wash[2] = sf::Vertex({ 0.0f, kH }, bottom);
        wash[3] = sf::Vertex({ kW, kH }, bottom);
    }
    target.draw(wash);
}

void EndGameVFX::drawRays(sf::RenderTarget& target) const {
    const float in = ramp(m_time, 0.30f, 1.40f);
    if (in <= 0.01f) return;

    // Fourteen wedges turning slowly out of the dead reactor. Drawn as
    // triangles reaching well past the corners so no length check is needed -
    // the screen crops them.
    const int rays = 14;
    const float reach = 1500.0f;
    const float spin = m_time * 0.22f;

    sf::VertexArray fan(sf::Triangles, rays * 3);
    for (int i = 0; i < rays; ++i) {
        const float mid = spin + (i / static_cast<float>(rays)) * 2.0f * kPi;
        // Alternating widths, so the fan does not read as a regular star.
        const float half = (i % 2 == 0) ? 0.055f : 0.030f;
        const float pulse = 0.62f + 0.38f * std::sin(m_time * 1.6f + i * 0.7f);

        const sf::Color hub(255, 226, 150, a8(in * 0.30f * pulse));
        const sf::Color tip(255, 210, 120, 0);

        fan[i * 3 + 0] = sf::Vertex(m_focus, hub);
        fan[i * 3 + 1] = sf::Vertex(
            m_focus + sf::Vector2f(std::cos(mid - half) * reach, std::sin(mid - half) * reach), tip);
        fan[i * 3 + 2] = sf::Vertex(
            m_focus + sf::Vector2f(std::cos(mid + half) * reach, std::sin(mid + half) * reach), tip);
    }
    target.draw(fan, sf::BlendAdd);
}

void EndGameVFX::drawVignette(sf::RenderTarget& target) const {
    const float in = ramp(m_time, 0.20f, 1.00f);
    if (in <= 0.01f) return;

    // A heartbeat that slows as it goes, so the screen reads as something
    // failing rather than something flashing on a fixed timer.
    const float rate = std::max(1.05f, 3.0f - m_time * 0.30f);
    const float beat = 0.5f + 0.5f * std::sin(m_time * rate * 2.0f * kPi * 0.5f);
    const float strength = in * (0.55f + 0.45f * beat);

    // Four edge bands, each fading inward. Cheaper and sharper than a radial
    // texture, and it frames the verdict without touching the middle.
    const float depth = 190.0f;
    const sf::Color edge(96, 6, 10, a8(strength * 0.86f));
    const sf::Color gone(96, 6, 10, 0);

    struct Band { sf::Vector2f a, b, c, d; };
    const Band bands[4] = {
        { { 0.0f, 0.0f }, { kW, 0.0f }, { 0.0f, depth }, { kW, depth } },
        { { 0.0f, kH }, { kW, kH }, { 0.0f, kH - depth }, { kW, kH - depth } },
        { { 0.0f, 0.0f }, { 0.0f, kH }, { depth, 0.0f }, { depth, kH } },
        { { kW, 0.0f }, { kW, kH }, { kW - depth, 0.0f }, { kW - depth, kH } },
    };
    for (const Band& band : bands) {
        sf::VertexArray strip(sf::TriangleStrip, 4);
        strip[0] = sf::Vertex(band.a, edge);
        strip[1] = sf::Vertex(band.b, edge);
        strip[2] = sf::Vertex(band.c, gone);
        strip[3] = sf::Vertex(band.d, gone);
        target.draw(strip);
    }
}

void EndGameVFX::drawSeal(sf::RenderTarget& target) const {
    // Spins in over the last third of a second and stops dead on the same frame
    // EndScreen's verdict lands, so the two are one impact and not two.
    const float t = ramp(m_time, kSealLand - 0.34f, kSealLand);
    if (t <= 0.0f) return;
    const float settle = ramp(m_time, kSealLand, kSealLand + 1.10f);

    const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    const float scale = 2.30f - 1.30f * ease;
    const float turn = (1.0f - ease) * 150.0f;
    const float alpha = ease * (1.0f - settle * 0.55f);

    const sf::Color ink = m_outcome == Outcome::Victory
        ? sf::Color(255, 214, 132) : sf::Color(232, 74, 66);
    const sf::Vector2f centre(640.0f, 336.0f);

    // Two rings and a ring of ticks: a seal, without needing a texture for it.
    for (int r = 0; r < 2; ++r) {
        const float radius = (r == 0 ? 150.0f : 178.0f) * scale;
        sf::CircleShape ring(radius, 72);
        ring.setOrigin(radius, radius);
        ring.setPosition(centre);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(r == 0 ? 2.4f : 1.2f);
        ring.setOutlineColor(sf::Color(ink.r, ink.g, ink.b,
                                       a8(alpha * (r == 0 ? 0.70f : 0.38f))));
        target.draw(ring);
    }

    const int ticks = 24;
    sf::VertexArray marks(sf::Lines, ticks * 2);
    for (int i = 0; i < ticks; ++i) {
        const float ang = turn * kPi / 180.0f + (i / static_cast<float>(ticks)) * 2.0f * kPi;
        const float inner = 154.0f * scale;
        const float outer = (i % 4 == 0 ? 174.0f : 164.0f) * scale;
        const sf::Color c(ink.r, ink.g, ink.b, a8(alpha * 0.55f));
        marks[i * 2 + 0] = sf::Vertex(
            centre + sf::Vector2f(std::cos(ang) * inner, std::sin(ang) * inner), c);
        marks[i * 2 + 1] = sf::Vertex(
            centre + sf::Vector2f(std::cos(ang) * outer, std::sin(ang) * outer), c);
    }
    target.draw(marks);
}

void EndGameVFX::renderBelow(sf::RenderTarget& target) {
    if (!m_active) return;

    drawBackdrop(target);
    drawWash(target);
    if (m_outcome == Outcome::Victory) {
        drawRays(target);
    } else {
        // The fracture spreads outward instead of appearing whole: the vertex
        // count grows with the clock, and the spokes were built centre-first so
        // growing the count grows every crack from the impact point.
        const float spread = ramp(m_time, 0.05f, 0.85f);
        const std::size_t shown =
            static_cast<std::size_t>(m_cracks.getVertexCount() * spread) / 2 * 2;
        if (shown >= 2) {
            sf::VertexArray grown(sf::Lines, shown);
            for (std::size_t i = 0; i < shown; ++i) grown[i] = m_cracks[i];
            target.draw(grown);
        }
    }
}

void EndGameVFX::renderAbove(sf::RenderTarget& target) {
    if (!m_active) return;

    if (m_outcome == Outcome::Defeat) drawVignette(target);

    if (!m_motes.empty()) {
        sf::VertexArray quads(sf::Quads, m_motes.size() * 4);
        for (std::size_t i = 0; i < m_motes.size(); ++i) {
            const Mote& mote = m_motes[i];
            // Fade in at birth as well as out at death, so nothing pops into
            // existence at the screen edge.
            const float age = 1.0f - mote.life / mote.maxLife;
            const float fade = std::min(clamp01(age / 0.12f),
                                        clamp01(mote.life / (mote.maxLife * 0.35f)));
            const sf::Color c(mote.colour.r, mote.colour.g, mote.colour.b,
                              static_cast<sf::Uint8>(mote.colour.a * clamp01(fade)));
            const float s = mote.size;
            quads[i * 4 + 0] = sf::Vertex(mote.position + sf::Vector2f(-s, -s), c);
            quads[i * 4 + 1] = sf::Vertex(mote.position + sf::Vector2f(s, -s), c);
            quads[i * 4 + 2] = sf::Vertex(mote.position + sf::Vector2f(s, s), c);
            quads[i * 4 + 3] = sf::Vertex(mote.position + sf::Vector2f(-s, s), c);
        }
        target.draw(quads, m_outcome == Outcome::Victory ? sf::BlendAdd : sf::BlendAlpha);
    }

    drawSeal(target);
}
