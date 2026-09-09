#include "rendering/EndScreen.hpp"
#include "utils/Rng.hpp"
#include "utils/Settings.hpp"
#include "utils/TextUtils.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 720.0f;

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

sf::Uint8 toAlpha(float v) {
    return static_cast<sf::Uint8>(clamp01(v) * 255.0f);
}

/// Accelerating: slow to leave, fast to arrive. The opposite of the usual
/// ease-out, and the whole reason the verdict reads as a slam rather than a
/// zoom - it has to be moving fastest at the moment it lands.
float easeIn(float t) { return t * t * t; }

void centre(sf::Text& text) {
    const sf::FloatRect b = text.getLocalBounds();
    text.setOrigin(b.left + b.width / 2.0f, b.top + b.height / 2.0f);
}

} // namespace

void EndScreen::begin(Outcome outcome, const std::string& headline,
                      const std::string& subtitle) {
    m_outcome = outcome;
    m_headline = headline;
    m_subtitle = subtitle;
    m_active = true;
    m_time = 0.0f;
    m_impactPending = true;
    m_impactFired = false;
    m_shake = { 0.0f, 0.0f };
}

void EndScreen::update(float dt) {
    if (!m_active) return;
    m_time += dt;

    const bool allowShake = Settings::get().screenShake;
    m_shake = { 0.0f, 0.0f };
    if (!allowShake) return;

    if (m_time < kGlitchEnd) {
        // Phase 1: the reactor is coming apart. Falls off over the phase so it
        // reads as a system failing rather than a permanent judder.
        const float strength = 11.0f * (1.0f - m_time / kGlitchEnd);
        m_shake = { Rng::rangeF(-strength, strength), Rng::rangeF(-strength, strength) };
    } else if (m_time >= kSlamEnd && m_time < kSlamEnd + 0.22f) {
        // Phase 3's landing: one hard punch that decays fast.
        const float strength = 16.0f * (1.0f - (m_time - kSlamEnd) / 0.22f);
        m_shake = { Rng::rangeF(-strength, strength), Rng::rangeF(-strength, strength) };
    }
}

bool EndScreen::consumeImpact() {
    if (!m_active || m_impactFired || m_time < kSlamEnd) return false;
    m_impactFired = true;
    m_impactPending = false;
    return true;
}

float EndScreen::uiReveal() const {
    if (!m_active || m_time < kSlamEnd) return 0.0f;
    return clamp01((m_time - kSlamEnd) / kUiRamp);
}

void EndScreen::renderScrim(sf::RenderTarget& target) {
    if (!m_active) return;

    const bool defeat = m_outcome == Outcome::Defeat;
    const sf::Color accent = defeat ? sf::Color(238, 62, 58) : sf::Color(240, 196, 92);

    // ---- Phase 1: red alert strobe ------------------------------------------
    if (m_time < kGlitchEnd) {
        // Three hard pulses across half a second. A smooth fade would read as a
        // filter; a square-ish strobe reads as an alarm.
        const float pulse = std::abs(std::sin(m_time * 18.0f));
        const float fade = 1.0f - m_time / kGlitchEnd;
        sf::RectangleShape alert({ kDesignW, kDesignH });
        alert.setFillColor(sf::Color(accent.r, accent.g, accent.b,
                                     toAlpha(pulse * fade * 0.42f)));
        target.draw(alert);
    }

    // ---- Phase 2: the board sinks -------------------------------------------
    // Starts under the strobe and keeps rising through the slam, so the verdict
    // never has to fight the board for contrast.
    const float dimT = clamp01((m_time - kGlitchEnd) / (kDimEnd - kGlitchEnd));
    if (dimT > 0.0f) {
        sf::RectangleShape scrim({ kDesignW, kDesignH });
        scrim.setFillColor(sf::Color(6, 4, 8, static_cast<sf::Uint8>(dimT * 196.0f)));
        target.draw(scrim);
    }

}

void EndScreen::render(sf::RenderTarget& target) {
    if (!m_active || !m_font) return;

    const bool defeat = m_outcome == Outcome::Defeat;
    const sf::Color accent = defeat ? sf::Color(238, 62, 58) : sf::Color(240, 196, 92);

    // ---- Phase 3: the verdict slams in --------------------------------------
    if (m_time < kDimEnd) return;

    const float slamT = clamp01((m_time - kDimEnd) / (kSlamEnd - kDimEnd));

    float scale;
    if (slamT < 1.0f) {
        scale = 1.0f + 2.0f * (1.0f - easeIn(slamT));
    } else {
        // A single sharp rebound on landing - metal, not rubber.
        const float since = m_time - kSlamEnd;
        scale = 1.0f + 0.11f * std::sin(since * 26.0f) * std::max(0.0f, 1.0f - since / 0.3f);
    }

    sf::Text verdict;
    verdict.setFont(*m_font);
    verdict.setString(m_headline);
    verdict.setCharacterSize(56);
    verdict.setStyle(sf::Text::Bold);
    verdict.setLetterSpacing(3.0f);
    centre(verdict);
    verdict.setPosition(kDesignW / 2.0f, kDesignH / 2.0f + 118.0f);
    verdict.setScale(scale, scale);

    // Fade in over the first third of the plunge; it is at full weight well
    // before it lands so the impact is the loudest moment, not the arrival.
    const float textAlpha = clamp01(slamT / 0.34f);
    verdict.setFillColor(sf::Color(accent.r, accent.g, accent.b, toAlpha(textAlpha)));
    verdict.setOutlineColor(sf::Color(0, 0, 0, toAlpha(textAlpha * 0.8f)));
    verdict.setOutlineThickness(3.0f);
    target.draw(verdict);

    // ---- Phase 3b: the rule and the subtitle, once it has landed ------------
    if (m_time < kSlamEnd) return;

    const float tailT = clamp01((m_time - kSlamEnd) / 0.4f);

    sf::RectangleShape rule({ 420.0f * tailT, 2.0f });
    rule.setOrigin(rule.getSize().x / 2.0f, 1.0f);
    rule.setPosition(kDesignW / 2.0f, kDesignH / 2.0f + 164.0f);
    rule.setFillColor(sf::Color(accent.r, accent.g, accent.b, toAlpha(tailT * 0.7f)));
    target.draw(rule);

    sf::Text sub;
    sub.setFont(*m_font);
    sub.setString(m_subtitle);
    sub.setCharacterSize(20);
    sub.setLetterSpacing(2.6f);
    centre(sub);
    sub.setPosition(kDesignW / 2.0f, kDesignH / 2.0f + 192.0f);
    sub.setFillColor(sf::Color(210, 204, 196, toAlpha(tailT * 0.88f)));
    target.draw(sub);
}
