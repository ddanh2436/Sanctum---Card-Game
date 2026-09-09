#include "rendering/CardSpotlight.hpp"
#include "rendering/CardArt.hpp"
#include "utils/Rng.hpp"
#include "utils/TextUtils.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 720.0f;
const sf::Vector2f kCardSize(168.0f, 232.0f);

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
sf::Uint8 toAlpha(float v) { return static_cast<sf::Uint8>(clamp01(v) * 255.0f); }

/// Decelerating: arrives quickly, settles without overshoot. A card sliding in
/// to be read should stop dead, not bounce.
float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

sf::Color withAlpha(sf::Color c, float a) {
    return sf::Color(c.r, c.g, c.b, static_cast<sf::Uint8>(c.a * clamp01(a)));
}

} // namespace

void CardSpotlight::show(const CardData& card, Side side, Kind kind, sf::Vector2f scrapAt) {
    m_card = card;
    m_side = side;
    m_kind = kind;
    m_scrapAt = scrapAt;

    // The player's own cards enter from their side of the table, the enemy's
    // from theirs. Direction of travel says whose it is before the text does.
    const bool mine = side == Side::Player;
    const float restX = mine ? 318.0f : kDesignW - 318.0f;
    m_to = { restX, kDesignH / 2.0f - 16.0f };
    m_from = { mine ? -kCardSize.x : kDesignW + kCardSize.x, m_to.y };
    m_pos = m_from;

    m_state = State::SlidingIn;
    m_timer = 0.0f;
    m_motes.clear();
    m_dissolved = false;
}

void CardSpotlight::clear() {
    m_state = State::Idle;
    m_motes.clear();
}

sf::Color CardSpotlight::accent() const {
    switch (m_kind) {
    case Kind::Counter: return sf::Color(255, 132, 48);   // orange alarm
    case Kind::Titan:   return sf::Color(240, 196, 92);   // gold
    case Kind::Operation:
    default:            return sf::Color(60, 220, 255);   // neon cyan
    }
}

const char* CardSpotlight::bannerText() const {
    const bool mine = m_side == Side::Player;
    switch (m_kind) {
    case Kind::Counter: return mine ? "COUNTER-PROTOCOL ACTIVATED"
                                    : "ENEMY COUNTER-PROTOCOL";
    case Kind::Titan:   return mine ? "TITAN DEPLOYED" : "ENEMY TITAN DEPLOYED";
    case Kind::Operation:
    default:            return mine ? "OPERATION EXECUTED" : "ENEMY OPERATION";
    }
}

void CardSpotlight::spawnDissolve() {
    // A spell or trap has no board presence to fly to, so it breaks into motes
    // that drift to the scrap pile. A titan keeps its card - it is about to
    // exist on the board instead.
    if (m_kind == Kind::Titan) return;

    const sf::Color tint = accent();
    for (int i = 0; i < 70; ++i) {
        Mote m;
        m.position = { m_pos.x + Rng::rangeF(-kCardSize.x / 2.0f, kCardSize.x / 2.0f),
                       m_pos.y + Rng::rangeF(-kCardSize.y / 2.0f, kCardSize.y / 2.0f) };
        // Aimed at the scrap pile, with enough spread that it reads as debris
        // rather than a queue.
        sf::Vector2f toScrap = m_scrapAt - m.position;
        const float len = std::sqrt(toScrap.x * toScrap.x + toScrap.y * toScrap.y);
        if (len > 0.01f) toScrap /= len;
        const float speed = Rng::rangeF(140.0f, 420.0f);
        m.velocity = toScrap * speed
                   + sf::Vector2f(Rng::rangeF(-70.0f, 70.0f), Rng::rangeF(-70.0f, 70.0f));
        m.maxLife = Rng::rangeF(0.28f, 0.6f);
        m.life = m.maxLife;
        m.size = Rng::rangeF(1.6f, 3.4f);
        m.colour = i % 3 == 0 ? sf::Color(255, 255, 255) : tint;
        m_motes.push_back(m);
    }
}

void CardSpotlight::update(float dt) {
    if (dt <= 0.0f) return;

    for (size_t i = 0; i < m_motes.size();) {
        Mote& m = m_motes[i];
        m.life -= dt;
        if (m.life <= 0.0f) {
            m = m_motes.back();
            m_motes.pop_back();
            continue;
        }
        m.position += m.velocity * dt;
        m.velocity *= 0.94f;
        ++i;
    }

    if (m_state == State::Idle) return;
    m_timer += dt;

    switch (m_state) {
    case State::SlidingIn: {
        const float t = clamp01(m_timer / kSlideIn);
        m_pos.x = m_from.x + (m_to.x - m_from.x) * easeOut(t);
        if (t >= 1.0f) {
            m_pos = m_to;
            m_state = State::Holding;
            m_timer = 0.0f;
        }
        break;
    }
    case State::Holding:
        if (m_timer >= kHold) {
            m_state = State::SlidingOut;
            m_timer = 0.0f;
            if (!m_dissolved) {
                m_dissolved = true;
                spawnDissolve();
            }
        }
        break;
    case State::SlidingOut:
        // A titan lifts away toward the board; everything else has already
        // come apart, so the card itself just fades.
        m_pos.y -= 260.0f * dt;
        if (m_timer >= kSlideOut) m_state = State::Idle;
        break;
    case State::Idle:
        break;
    }
}

void CardSpotlight::render(sf::RenderTarget& target) {
    if (!m_font) return;

    // Motes outlive the card, so they are drawn even once the state is Idle.
    if (m_state != State::Idle) {
        float scrim = 1.0f;
        if (m_state == State::SlidingIn)       scrim = clamp01(m_timer / kSlideIn);
        else if (m_state == State::SlidingOut) scrim = 1.0f - clamp01(m_timer / kSlideOut);

        sf::RectangleShape dim({ kDesignW, kDesignH });
        dim.setFillColor(sf::Color(4, 6, 10, static_cast<sf::Uint8>(kScrimAlpha * scrim)));
        target.draw(dim);

        // A counter fires out of turn, off a card the victim has never seen.
        // Two hard pulses of alarm light before the card arrives is the only
        // warning the moment gets, and it is what separates "I was countered"
        // from "my frame died and I do not know why".
        if (m_kind == Kind::Counter && m_state == State::SlidingIn) {
            const float alarm = std::abs(std::sin(m_timer * 22.0f))
                              * (1.0f - clamp01(m_timer / kSlideIn));
            sf::RectangleShape flash({ kDesignW, kDesignH });
            flash.setFillColor(sf::Color(255, 96, 40, toAlpha(alarm * 0.34f)));
            target.draw(flash);
        }

        const float fade = m_state == State::SlidingOut
                         ? 1.0f - clamp01(m_timer / kSlideOut)
                         : 1.0f;
        const sf::Color tint = accent();

        // A glow plate behind the card, so it lifts off the dimmed board.
        sf::RectangleShape halo({ kCardSize.x + 26.0f, kCardSize.y + 26.0f });
        halo.setOrigin(halo.getSize() / 2.0f);
        halo.setPosition(m_pos);
        halo.setFillColor(sf::Color(tint.r / 6, tint.g / 6, tint.b / 6,
                                    static_cast<sf::Uint8>(190 * fade)));
        halo.setOutlineThickness(2.0f);
        halo.setOutlineColor(withAlpha(tint, fade));
        target.draw(halo);

        CardArt::drawCard(target, *m_font, m_card, m_pos, kCardSize, 0.0f, true, true);

        // Hologram scanline: a bright band sweeping top to bottom, on a loop.
        // It is what makes the card read as a readout rather than a photo.
        const float sweep = std::fmod(m_timer * 1.4f, 1.0f);
        const float bandY = m_pos.y - kCardSize.y / 2.0f + kCardSize.y * sweep;
        sf::VertexArray band(sf::TriangleStrip, 4);
        const float halfW = kCardSize.x / 2.0f;
        const sf::Color hot = withAlpha(sf::Color(tint.r, tint.g, tint.b, 150), fade);
        const sf::Color cool = withAlpha(sf::Color(tint.r, tint.g, tint.b, 0), fade);
        band[0] = sf::Vertex({ m_pos.x - halfW, bandY - 9.0f }, cool);
        band[1] = sf::Vertex({ m_pos.x + halfW, bandY - 9.0f }, cool);
        band[2] = sf::Vertex({ m_pos.x - halfW, bandY + 2.0f }, hot);
        band[3] = sf::Vertex({ m_pos.x + halfW, bandY + 2.0f }, hot);
        target.draw(band);

        // Sub-banner under the card: what class of thing just happened.
        sf::Text banner;
        banner.setFont(*m_font);
        banner.setString(bannerText());
        banner.setCharacterSize(15);
        banner.setStyle(sf::Text::Bold);
        banner.setLetterSpacing(2.4f);
        banner.setFillColor(withAlpha(tint, fade));
        banner.setOutlineColor(sf::Color(0, 0, 0, toAlpha(fade * 0.85f)));
        banner.setOutlineThickness(2.0f);
        TextUtils::centerBoth(banner);
        banner.setPosition(m_pos.x, m_pos.y + kCardSize.y / 2.0f + 26.0f);

        const sf::FloatRect box = banner.getGlobalBounds();
        sf::RectangleShape plate({ box.width + 28.0f, box.height + 16.0f });
        plate.setOrigin(plate.getSize() / 2.0f);
        plate.setPosition(banner.getPosition());
        plate.setFillColor(sf::Color(8, 10, 14, static_cast<sf::Uint8>(232 * fade)));
        plate.setOutlineThickness(1.0f);
        plate.setOutlineColor(withAlpha(sf::Color(tint.r, tint.g, tint.b, 170), fade));
        target.draw(plate);
        target.draw(banner);
    }

    if (!m_motes.empty()) {
        sf::VertexArray quads(sf::Quads, m_motes.size() * 4);
        for (size_t i = 0; i < m_motes.size(); ++i) {
            const Mote& m = m_motes[i];
            const float t = m.life / m.maxLife;
            const float s = m.size * (0.4f + 0.6f * t);
            quads[i * 4 + 0].position = m.position + sf::Vector2f(-s, -s);
            quads[i * 4 + 1].position = m.position + sf::Vector2f(s, -s);
            quads[i * 4 + 2].position = m.position + sf::Vector2f(s, s);
            quads[i * 4 + 3].position = m.position + sf::Vector2f(-s, s);
            const sf::Color c = withAlpha(m.colour, t);
            for (int j = 0; j < 4; ++j) quads[i * 4 + j].color = c;
        }
        target.draw(quads);
    }
}
