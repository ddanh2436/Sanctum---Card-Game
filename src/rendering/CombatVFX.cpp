#include "rendering/CombatVFX.hpp"
#include "rendering/CardArt.hpp"
#include "utils/Rng.hpp"
#include "utils/TextUtils.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

float frand(float lo, float hi) { return Rng::rangeF(lo, hi); }

sf::Color withAlpha(sf::Color c, float t) {
    c.a = static_cast<sf::Uint8>(std::max(0.0f, std::min(1.0f, t)) * 255.0f);
    return c;
}

sf::Color mix(sf::Color a, sf::Color b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return sf::Color(static_cast<sf::Uint8>(a.r + (b.r - a.r) * t),
                     static_cast<sf::Uint8>(a.g + (b.g - a.g) * t),
                     static_cast<sf::Uint8>(a.b + (b.b - a.b) * t),
                     static_cast<sf::Uint8>(a.a + (b.a - a.a) * t));
}

sf::Vector2f normalise(sf::Vector2f v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0001f ? sf::Vector2f(v.x / len, v.y / len) : sf::Vector2f(0.0f, -1.0f);
}

/// Ease-out: fast at the start, settling at the end. Used for anything that
/// should feel like it was thrown rather than driven.
float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

} // namespace

// =============================================================================
// Spawners
// =============================================================================

void CombatVFX::explosion(sf::Vector2f centre, sf::Color accent) {
    // Two populations: hot sparks that die fast, and heavier debris that arcs
    // under gravity. One alone reads as a puff; together it reads as a machine
    // coming apart.
    for (int i = 0; i < 46; ++i) {
        const float angle = frand(0.0f, 2.0f * kPi);
        const float speed = frand(120.0f, 430.0f);

        Particle p;
        p.position = centre;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        p.maxLife = frand(0.42f, 0.9f);
        p.life = p.maxLife;
        p.size = frand(2.0f, 5.5f);
        p.drag = 0.90f;
        // Yellow core cooling through the doctrine accent into smoke.
        p.colour = mix(sf::Color(255, 226, 140), accent, frand(0.0f, 0.7f));
        m_particles.push_back(p);
    }

    for (int i = 0; i < 18; ++i) {
        const float angle = frand(-kPi, 0.0f);      // thrown upward
        const float speed = frand(90.0f, 260.0f);

        Particle p;
        p.position = centre;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        p.maxLife = frand(0.85f, 1.5f);
        p.life = p.maxLife;
        p.size = frand(3.0f, 7.0f);
        p.gravity = 900.0f;
        p.drag = 0.995f;
        p.colour = sf::Color(118, 124, 134);        // torn hull plate
        m_particles.push_back(p);
    }

    shockwave(centre, mix(accent, sf::Color(255, 236, 190), 0.5f), 110.0f, 0.55f);
}

void CombatVFX::impact(sf::Vector2f at, sf::Vector2f dir, sf::Color accent) {
    const sf::Vector2f d = normalise(dir);
    const float base = std::atan2(d.y, d.x);

    for (int i = 0; i < 16; ++i) {
        // A cone thrown along the blow, not a sphere - the direction of the hit
        // is half of what makes it readable.
        const float angle = base + frand(-0.7f, 0.7f);
        const float speed = frand(140.0f, 340.0f);

        Particle p;
        p.position = at;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        p.maxLife = frand(0.24f, 0.5f);
        p.life = p.maxLife;
        p.size = frand(1.6f, 3.6f);
        p.drag = 0.88f;
        p.colour = mix(sf::Color(255, 240, 210), accent, frand(0.2f, 0.9f));
        m_particles.push_back(p);
    }
    slash(at, d, accent);
}

void CombatVFX::sparks(sf::Vector2f at, sf::Color accent) {
    for (int i = 0; i < 14; ++i) {
        const float angle = frand(0.0f, 2.0f * kPi);
        const float speed = frand(70.0f, 190.0f);

        Particle p;
        p.position = at;
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed };
        p.maxLife = frand(0.3f, 0.6f);
        p.life = p.maxLife;
        p.size = frand(1.4f, 2.8f);
        p.gravity = 320.0f;
        p.drag = 0.93f;
        p.colour = mix(sf::Color(206, 236, 255), accent, frand(0.0f, 0.5f));
        m_particles.push_back(p);
    }
}

void CombatVFX::shockwave(sf::Vector2f centre, sf::Color colour, float maxRadius, float seconds) {
    Ring ring;
    ring.centre = centre;
    ring.colour = colour;
    ring.maxRadius = maxRadius;
    ring.maxLife = seconds;
    ring.life = seconds;
    m_rings.push_back(ring);
}

void CombatVFX::beam(sf::Vector2f from, sf::Vector2f to, sf::Color colour, float seconds) {
    Beam b;
    b.from = from;
    b.to = to;
    b.colour = colour;
    b.maxLife = seconds;
    b.life = seconds;
    m_beams.push_back(b);
}

void CombatVFX::slash(sf::Vector2f at, sf::Vector2f dir, sf::Color colour) {
    const sf::Vector2f d = normalise(dir);
    Arc arc;
    arc.centre = at;
    arc.angle = std::atan2(d.y, d.x);
    arc.colour = colour;
    arc.life = arc.maxLife;
    m_arcs.push_back(arc);
}

void CombatVFX::smoke(sf::Vector2f at, int count) {
    // Grey dust kicked sideways and up, falling back under gravity. Slower and
    // heavier than sparks, which is what separates "something landed here" from
    // "something exploded here".
    for (int i = 0; i < count; ++i) {
        const float angle = frand(-kPi, 0.0f);
        const float speed = frand(40.0f, 150.0f);

        Particle p;
        p.position = at + sf::Vector2f(frand(-26.0f, 26.0f), frand(6.0f, 22.0f));
        p.velocity = { std::cos(angle) * speed, std::sin(angle) * speed * 0.45f };
        p.maxLife = frand(0.5f, 0.95f);
        p.life = p.maxLife;
        p.size = frand(4.0f, 9.0f);
        p.gravity = 190.0f;
        p.drag = 0.93f;
        const sf::Uint8 grey = static_cast<sf::Uint8>(frand(120.0f, 190.0f));
        p.colour = sf::Color(grey, grey, static_cast<sf::Uint8>(grey + 8));
        m_particles.push_back(p);
    }
}

void CombatVFX::crosshair(sf::Vector2f at, float seconds) {
    Crosshair c;
    c.at = at;
    c.maxLife = seconds;
    c.life = seconds;
    m_crosshairs.push_back(c);
}

void CombatVFX::slam(int unitId) {
    Slam s;
    s.life = s.maxLife;
    m_slams[unitId] = s;
}

float CombatVFX::squashFor(int unitId) const {
    auto it = m_slams.find(unitId);
    if (it == m_slams.end()) return 1.0f;

    const float elapsed = 1.0f - it->second.life / it->second.maxLife;
    // Compress hard on contact, spring back past 1.0, settle. The overshoot is
    // what makes it read as weight rather than a resize.
    if (elapsed < 0.30f) return 1.0f - 0.15f * (elapsed / 0.30f);
    const float back = (elapsed - 0.30f) / 0.70f;
    return 0.85f + 0.15f * easeOut(back) + 0.06f * std::sin(back * kPi);
}

float CombatVFX::badgePulseFor(int unitId) const {
    auto it = m_slams.find(unitId);
    if (it == m_slams.end()) return 0.0f;
    return it->second.life / it->second.maxLife;
}

void CombatVFX::dropMarker(sf::Vector2f at, sf::Color accent) {
    DropMarker m;
    m.at = at;
    m.colour = accent;
    m.life = m.maxLife;
    m_markers.push_back(m);
}

void CombatVFX::trapFlip(sf::FloatRect box, const CardData& card, Side owner,
                         sf::Color accent) {
    TrapFlip f;
    f.box = box;
    f.card = card;
    f.owner = owner;
    f.accent = accent;
    f.life = f.maxLife;
    m_flips.push_back(f);
}

void CombatVFX::armFlare(sf::FloatRect box, sf::Color colour, float seconds) {
    ArmFlare f;
    f.box = box;
    f.colour = colour;
    f.maxLife = seconds;
    f.life = seconds;
    m_armFlares.push_back(f);

    // Sparks seeded ALONG the border rather than from the middle: the thing that
    // just changed is the frame, so that is where the light should come from.
    const float perimeter = 2.0f * (box.width + box.height);
    const int count = 40;
    for (int i = 0; i < count; ++i) {
        float walk = frand(0.0f, perimeter);
        sf::Vector2f at;
        sf::Vector2f outward;
        if (walk < box.width) {
            at = { box.left + walk, box.top };
            outward = { 0.0f, -1.0f };
        } else if ((walk -= box.width) < box.height) {
            at = { box.left + box.width, box.top + walk };
            outward = { 1.0f, 0.0f };
        } else if ((walk -= box.height) < box.width) {
            at = { box.left + box.width - walk, box.top + box.height };
            outward = { 0.0f, 1.0f };
        } else {
            walk -= box.width;
            at = { box.left, box.top + box.height - walk };
            outward = { -1.0f, 0.0f };
        }

        const float speed = frand(24.0f, 70.0f);
        Particle p;
        p.position = at;
        p.velocity = { outward.x * speed + frand(-26.0f, 26.0f),
                       outward.y * speed + frand(-26.0f, 26.0f) };
        // Spread the lifetimes wide so sparks keep appearing to leave the frame
        // for as long as the border is still hot, rather than all at once.
        p.maxLife = frand(0.30f, 1.15f);
        p.life = p.maxLife;
        p.size = frand(1.1f, 2.8f);
        p.gravity = 40.0f;
        p.drag = 0.90f;
        p.colour = mix(sf::Color(255, 246, 214), colour, frand(0.0f, 0.65f));
        m_particles.push_back(p);
    }
}

void CombatVFX::flash(int unitId, sf::Color colour, float seconds) {
    Flash f;
    f.colour = colour;
    f.maxLife = seconds;
    f.life = seconds;
    m_flashes[unitId] = f;
}

void CombatVFX::lunge(int unitId, sf::Vector2f direction, float distance) {
    Lunge l;
    l.direction = normalise(direction);
    l.distance = distance;
    l.life = l.maxLife;
    m_lunges[unitId] = l;
}

void CombatVFX::arrow(sf::Vector2f from, sf::Vector2f to, sf::Color colour, float seconds) {
    Arrow a;
    a.from = from;
    a.to = to;
    a.colour = colour;
    a.maxLife = seconds;
    a.life = seconds;
    m_arrows.push_back(a);
}

void CombatVFX::setLiveArrow(bool active, sf::Vector2f from, sf::Vector2f to,
                             sf::Color colour, bool valid) {
    m_liveArrow = active;
    m_liveFrom = from;
    m_liveTo = to;
    m_liveColour = colour;
    m_liveArrowValid = valid;
}


// =============================================================================
// Queries
// =============================================================================

sf::Color CombatVFX::overlayFor(int unitId) const {
    auto it = m_flashes.find(unitId);
    if (it == m_flashes.end()) return sf::Color(0, 0, 0, 0);

    const float t = it->second.life / it->second.maxLife;
    sf::Color c = it->second.colour;
    // Snap on, fall off - a flash that ramps up both ways reads as a glow
    // rather than an impact.
    c.a = static_cast<sf::Uint8>(std::min(1.0f, t * 1.25f) * c.a);
    return c;
}

sf::Vector2f CombatVFX::offsetFor(int unitId) const {
    auto it = m_lunges.find(unitId);
    if (it == m_lunges.end()) return { 0.0f, 0.0f };

    const Lunge& l = it->second;
    const float elapsed = 1.0f - l.life / l.maxLife;   // 0 at the start

    // Out fast over the first third, back over the rest. The pause between the
    // two is what sells the contact.
    float reach;
    if (elapsed < 0.34f) {
        reach = easeOut(elapsed / 0.34f);
    } else {
        const float back = (elapsed - 0.34f) / 0.66f;
        reach = 1.0f - easeOut(back);
    }
    return { l.direction.x * l.distance * reach, l.direction.y * l.distance * reach };
}

// =============================================================================
// Update
// =============================================================================

void CombatVFX::update(float dt) {
    if (dt <= 0.0f) return;

    for (size_t i = 0; i < m_particles.size();) {
        Particle& p = m_particles[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            p = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        p.velocity.y += p.gravity * dt;
        // Drag is applied per frame rather than per second; at the frame rates
        // this game runs the difference is not visible and this is cheaper.
        p.velocity *= p.drag;
        p.position += p.velocity * dt;
        ++i;
    }

    auto tick = [dt](auto& container) {
        for (size_t i = 0; i < container.size();) {
            container[i].life -= dt;
            if (container[i].life <= 0.0f) {
                container[i] = container.back();
                container.pop_back();
            } else {
                ++i;
            }
        }
    };
    tick(m_rings);
    tick(m_beams);
    tick(m_arcs);
    tick(m_arrows);
    tick(m_crosshairs);
    tick(m_markers);
    tick(m_flips);
    tick(m_armFlares);

    for (auto it = m_flashes.begin(); it != m_flashes.end();) {
        it->second.life -= dt;
        if (it->second.life <= 0.0f) it = m_flashes.erase(it); else ++it;
    }
    for (auto it = m_slams.begin(); it != m_slams.end();) {
        it->second.life -= dt;
        if (it->second.life <= 0.0f) it = m_slams.erase(it); else ++it;
    }
    for (auto it = m_lunges.begin(); it != m_lunges.end();) {
        it->second.life -= dt;
        if (it->second.life <= 0.0f) it = m_lunges.erase(it); else ++it;
    }

}

void CombatVFX::clear() {
    m_particles.clear();
    m_rings.clear();
    m_beams.clear();
    m_arcs.clear();
    m_arrows.clear();
    m_crosshairs.clear();
    m_markers.clear();
    m_flips.clear();
    m_armFlares.clear();
    m_slams.clear();
    m_flashes.clear();
    m_lunges.clear();
    m_liveArrow = false;
}

// =============================================================================
// Rendering
// =============================================================================

void CombatVFX::renderBelow(sf::RenderTarget& target) {
    // Beams first: a shot passes behind the frames it connects.
    for (const Beam& b : m_beams) {
        const float t = b.life / b.maxLife;
        const sf::Vector2f d = normalise(b.to - b.from);
        const sf::Vector2f n(-d.y, d.x);
        const float half = b.width * 0.5f * t;

        sf::VertexArray strip(sf::TriangleStrip, 4);
        strip[0].position = b.from + n * half;
        strip[1].position = b.from - n * half;
        strip[2].position = b.to + n * half;
        strip[3].position = b.to - n * half;

        const sf::Color hot = withAlpha(mix(b.colour, sf::Color::White, 0.55f), t);
        const sf::Color cool = withAlpha(b.colour, t * 0.6f);
        strip[0].color = hot;
        strip[1].color = hot;
        strip[2].color = cool;
        strip[3].color = cool;
        target.draw(strip);
    }

    // Hex landing zone: two counter-rotating rings opening at the slot.
    for (const DropMarker& m : m_markers) {
        const float t = m.life / m.maxLife;
        const float open = easeOut(1.0f - t);
        for (int ring = 0; ring < 2; ++ring) {
            const float radius = (30.0f + ring * 16.0f) * open;
            if (radius < 1.0f) continue;
            const float spin = (1.0f - t) * (ring == 0 ? 1.6f : -1.1f);

            sf::VertexArray hex(sf::LineStrip, 7);
            for (int i = 0; i <= 6; ++i) {
                const float a = spin + static_cast<float>(i) * kPi / 3.0f;
                hex[i].position = m.at + sf::Vector2f(std::cos(a), std::sin(a)) * radius;
                hex[i].color = withAlpha(m.colour, t * (ring == 0 ? 0.9f : 0.5f));
            }
            target.draw(hex);
        }
    }

    for (const Ring& r : m_rings) {
        const float t = r.life / r.maxLife;
        const float radius = r.maxRadius * easeOut(1.0f - t);
        if (radius < 1.0f) continue;

        sf::CircleShape ring(radius, 42);
        ring.setOrigin(radius, radius);
        ring.setPosition(r.centre);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(std::max(1.0f, 5.0f * t));
        ring.setOutlineColor(withAlpha(r.colour, t * 0.85f));
        target.draw(ring);
    }
}

void CombatVFX::renderAbove(sf::RenderTarget& target) {
    // Slash arcs: a wedge swept across the point of contact.
    for (const Arc& a : m_arcs) {
        const float t = a.life / a.maxLife;
        const int steps = 14;
        const float spread = 1.5f;   // radians the arc covers

        sf::VertexArray strip(sf::TriangleStrip, (steps + 1) * 2);
        for (int i = 0; i <= steps; ++i) {
            const float f = static_cast<float>(i) / steps;
            const float angle = a.angle - spread * 0.5f + spread * f + kPi * 0.5f;
            const float inner = a.radius * (0.45f + 0.25f * (1.0f - t));
            const float outer = a.radius * (1.0f + 0.35f * (1.0f - t));
            const sf::Vector2f dir(std::cos(angle), std::sin(angle));

            // Taper the ends so it reads as a swipe, not a band.
            const float taper = std::sin(f * kPi);
            const sf::Color c = withAlpha(mix(a.colour, sf::Color::White, 0.5f), t * taper);

            strip[i * 2 + 0].position = a.centre + dir * inner;
            strip[i * 2 + 1].position = a.centre + dir * outer;
            strip[i * 2 + 0].color = c;
            strip[i * 2 + 1].color = withAlpha(a.colour, t * taper * 0.35f);
        }
        target.draw(strip);
    }

    // Particles, batched into one draw call.
    if (!m_particles.empty()) {
        sf::VertexArray quads(sf::Quads, m_particles.size() * 4);
        for (size_t i = 0; i < m_particles.size(); ++i) {
            const Particle& p = m_particles[i];
            const float t = p.life / p.maxLife;
            const float s = p.size * (p.fade ? (0.35f + 0.65f * t) : 1.0f);

            quads[i * 4 + 0].position = p.position + sf::Vector2f(-s, -s);
            quads[i * 4 + 1].position = p.position + sf::Vector2f(s, -s);
            quads[i * 4 + 2].position = p.position + sf::Vector2f(s, s);
            quads[i * 4 + 3].position = p.position + sf::Vector2f(-s, s);

            const sf::Color c = withAlpha(p.colour, p.fade ? t : 1.0f);
            for (int j = 0; j < 4; ++j) quads[i * 4 + j].color = c;
        }
        target.draw(quads);
    }

    // Target lock: two rotating brackets closing on the frame under attack.
    for (const Crosshair& c : m_crosshairs) {
        const float t = c.life / c.maxLife;
        const float radius = 26.0f + 20.0f * t;          // closes as it settles
        const float spin = (1.0f - t) * 2.4f;
        const sf::Color ink = withAlpha(sf::Color(255, 96, 84), std::min(1.0f, t * 2.0f));

        for (int q = 0; q < 4; ++q) {
            const float base = spin + static_cast<float>(q) * kPi * 0.5f;
            sf::VertexArray bracket(sf::LineStrip, 3);
            for (int k = 0; k < 3; ++k) {
                const float a = base + static_cast<float>(k) * 0.30f;
                bracket[k].position = c.at + sf::Vector2f(std::cos(a), std::sin(a)) * radius;
                bracket[k].color = ink;
            }
            target.draw(bracket);
        }

        sf::CircleShape dot(2.5f, 8);
        dot.setOrigin(2.5f, 2.5f);
        dot.setPosition(c.at);
        dot.setFillColor(ink);
        target.draw(dot);
    }

    // A card locking in: the border runs hot, then eases down to the armed
    // colour. Drawn as three nested outlines so the glow has depth without a
    // shader, and the innermost one is the colour the card keeps afterwards.
    for (const ArmFlare& f : m_armFlares) {
        const float t = 1.0f - f.life / f.maxLife;              // 0 -> 1

        // Hold at full heat for the first fifth, then burn off. A pure ease-out
        // spent most of its brightness inside the first two frames, which is
        // long enough for the code to be correct and far too short to see.
        const float hold = 0.20f;
        float heat;
        if (t <= hold) {
            heat = 1.0f;
        } else {
            const float u = (t - hold) / (1.0f - hold);
            heat = (1.0f - u) * (1.0f - u);                     // in-quad decay
        }
        // A slow shimmer on top, so the frame reads as live rather than lit.
        const float shimmer = 0.82f + 0.18f * std::sin(t * 26.0f);

        for (int layer = 0; layer < 3; ++layer) {
            const float grow = 2.0f + layer * 3.0f + heat * (10.0f + layer * 7.0f);
            sf::RectangleShape edge({ f.box.width + grow * 2.0f, f.box.height + grow * 2.0f });
            edge.setPosition(f.box.left - grow, f.box.top - grow);
            edge.setFillColor(sf::Color::Transparent);
            edge.setOutlineThickness(layer == 0 ? 2.4f : 1.4f);

            // Layer 0 is the card's own new border colour; the outer two are the
            // white-hot bloom that burns off.
            const sf::Color base = layer == 0 ? f.colour : mix(sf::Color(255, 250, 226), f.colour, 0.45f);
            const float alpha = layer == 0 ? (0.35f + 0.65f * heat)
                                           : heat * shimmer * (0.72f - layer * 0.16f);
            edge.setOutlineColor(sf::Color(base.r, base.g, base.b,
                                           static_cast<sf::Uint8>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
            target.draw(edge);
        }
    }

    // A counter-protocol turning over in its slot. The card narrows to a line
    // showing its back, then opens out showing its face - the same read as a
    // physical card being turned, which is what the moment is imitating.
    for (const TrapFlip& f : m_flips) {
        if (!m_font) break;
        const float t = 1.0f - f.life / f.maxLife;          // 0 -> 1
        const bool faceUp = t >= 0.5f;
        const float squeeze = faceUp ? (t - 0.5f) * 2.0f : 1.0f - t * 2.0f;
        const float width = std::max(1.0f, f.box.width * squeeze);
        const sf::Vector2f centre(f.box.left + f.box.width / 2.0f,
                                  f.box.top + f.box.height / 2.0f);

        if (faceUp) {
            // The trap slot is far shorter than a card, so the face is drawn at
            // card proportions and centred on the slot rather than stretched
            // into it.
            const float h = f.box.height * 2.6f;
            CardArt::drawCard(target, *m_font, f.card, centre,
                     { width * 2.6f, h }, 0.0f, true, true);
        } else {
            // The same painted back the card was showing in its slot a moment
            // ago, narrowing as it turns.
            CardArt::drawCardBack(target,
                                  { centre.x - width / 2.0f, f.box.top, width, f.box.height },
                                  f.owner, f.accent);
        }

        // A rim of light along the turning edge sells the rotation.
        sf::RectangleShape edge({ 2.5f, f.box.height * (faceUp ? 2.6f : 1.0f) });
        edge.setOrigin(1.25f, edge.getSize().y / 2.0f);
        edge.setPosition(centre.x + width * (faceUp ? 1.3f : 0.5f), centre.y);
        edge.setFillColor(withAlpha(sf::Color(255, 245, 220), 1.0f - std::abs(t - 0.5f) * 2.0f));
        target.draw(edge);
    }

    // Replayed targeting arrows, then the one the player is currently dragging.
    for (const Arrow& a : m_arrows) {
        drawArrow(target, a.from, a.to, a.colour, a.life / a.maxLife, true);
    }
    if (m_liveArrow) {
        drawArrow(target, m_liveFrom, m_liveTo, m_liveColour, 1.0f, m_liveArrowValid);
    }

}

void CombatVFX::drawArrow(sf::RenderTarget& target, sf::Vector2f from, sf::Vector2f to,
                          sf::Color colour, float alpha, bool valid) const {
    const sf::Vector2f delta = to - from;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length < 12.0f) return;

    // A quadratic bezier bowed away from the straight line. A straight arrow
    // over a busy board disappears into the row dividers; a curved one does not.
    const sf::Vector2f mid = from + delta * 0.5f;
    const sf::Vector2f perp = normalise({ -delta.y, delta.x });
    const sf::Vector2f control = mid + perp * std::min(70.0f, length * 0.22f);

    const sf::Color body = valid ? colour : sf::Color(150, 90, 90);
    const int segments = 26;
    const float headRoom = 26.0f;   // the tail stops short so the head is clean

    sf::VertexArray ribbon(sf::TriangleStrip, (segments + 1) * 2);
    sf::Vector2f last = from;
    for (int i = 0; i <= segments; ++i) {
        const float f = static_cast<float>(i) / segments;
        const float inv = 1.0f - f;
        const sf::Vector2f point = from * (inv * inv)
                                 + control * (2.0f * inv * f)
                                 + to * (f * f);

        sf::Vector2f dir = normalise(point - last);
        if (i == 0) dir = normalise(control - from);
        last = point;

        const sf::Vector2f n(-dir.y, dir.x);
        const float width = (2.0f + 6.0f * f);    // tapers from the source outward
        const sf::Color c = withAlpha(body, alpha * (0.25f + 0.75f * f));

        ribbon[i * 2 + 0].position = point + n * width;
        ribbon[i * 2 + 1].position = point - n * width;
        ribbon[i * 2 + 0].color = c;
        ribbon[i * 2 + 1].color = c;
    }
    target.draw(ribbon);

    // Head, pointed along the curve's final tangent.
    const sf::Vector2f tangent = normalise(to - control);
    const sf::Vector2f n(-tangent.y, tangent.x);
    const sf::Vector2f tip = to + tangent * 6.0f;

    sf::VertexArray head(sf::Triangles, 3);
    head[0].position = tip;
    head[1].position = to - tangent * headRoom + n * 15.0f;
    head[2].position = to - tangent * headRoom - n * 15.0f;
    const sf::Color headColour = withAlpha(mix(body, sf::Color::White, 0.35f), alpha);
    for (int i = 0; i < 3; ++i) head[i].color = headColour;
    target.draw(head);

    // A ring at the source so it is obvious which frame is doing the attacking.
    sf::CircleShape anchor(7.0f, 16);
    anchor.setOrigin(7.0f, 7.0f);
    anchor.setPosition(from);
    anchor.setFillColor(sf::Color::Transparent);
    anchor.setOutlineThickness(2.0f);
    anchor.setOutlineColor(withAlpha(body, alpha * 0.9f));
    target.draw(anchor);
}
