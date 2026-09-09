#include "rendering/HolyVFX.hpp"
#include "utils/Rng.hpp"
#include <algorithm>
#include <cmath>

HolyVFX::HolyVFX(sf::Vector2f defaultCenter)
    : m_defaultViewCenter(defaultCenter) {}

void HolyVFX::setTrailActive(bool active) {
    m_isTrailActive = active;
    if (!active) {
        clearTrail();
    }
}

void HolyVFX::addTrailPoint(sf::Vector2f point) {
    if (!m_trailPoints.empty()) {
        sf::Vector2f last = m_trailPoints.back().position;
        float distSq = (point.x - last.x) * (point.x - last.x) + (point.y - last.y) * (point.y - last.y);
        if (distSq < 16.0f) {
            return; // Tránh thêm điểm quá dày
        }
    }
    m_trailPoints.push_back({ point, m_trailPointLifetime });
}

void HolyVFX::clearTrail() {
    m_trailPoints.clear();
}

void HolyVFX::triggerSaintessGlow() {
    m_saintessGlowActive = true;
    m_saintessGlowTimer = 1.5f;
}

void HolyVFX::triggerScreenShake(float duration, float magnitude) {
    m_shakeDuration = duration;
    m_shakeTimer = duration;
    m_shakeMagnitude = magnitude;
}

void HolyVFX::update(float dt) {
    // 1. Golden sword trail fades out
    for (auto it = m_trailPoints.begin(); it != m_trailPoints.end(); ) {
        it->timeRemaining -= dt;
        if (it->timeRemaining <= 0.0f) {
            it = m_trailPoints.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Saintess halo
    if (m_saintessGlowActive) {
        m_saintessGlowTimer -= dt;
        if (m_saintessGlowTimer <= 0.0f) {
            m_saintessGlowActive = false;
        }
    }

    // 3. Screen shake, published as an offset so the caller keeps its own view
    if (m_shakeTimer > 0.0f) {
        m_shakeTimer -= dt;
        if (m_shakeTimer <= 0.0f) {
            m_shakeOffset = { 0.0f, 0.0f };
        } else {
            const float progress = m_shakeTimer / m_shakeDuration;
            const float magnitude = m_shakeMagnitude * progress;
            m_shakeOffset = { Rng::rangeF(-1.0f, 1.0f) * magnitude,
                              Rng::rangeF(-1.0f, 1.0f) * magnitude };
        }
    } else {
        m_shakeOffset = { 0.0f, 0.0f };
    }
}

void HolyVFX::update(float dt, sf::View& view) {
    update(dt);
    view.setCenter(m_defaultViewCenter + m_shakeOffset);
}

void HolyVFX::render(sf::RenderTarget& target) {
    // Vẽ vệt kiếm vàng hoàng kim bằng sf::TriangleStrip
    if (m_trailPoints.size() >= 2) {
        sf::VertexArray strip(sf::TriangleStrip);

        for (size_t i = 0; i < m_trailPoints.size(); ++i) {
            float progress = static_cast<float>(i) / static_cast<float>(m_trailPoints.size() - 1);
            float alphaRatio = m_trailPoints[i].timeRemaining / m_trailPointLifetime;
            sf::Uint8 alpha = static_cast<sf::Uint8>(std::clamp(alphaRatio * 255.0f, 0.0f, 255.0f));

            sf::Vector2f dir(0.0f, 0.0f);
            if (i + 1 < m_trailPoints.size()) {
                dir = m_trailPoints[i + 1].position - m_trailPoints[i].position;
            } else if (i > 0) {
                dir = m_trailPoints[i].position - m_trailPoints[i - 1].position;
            }

            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            sf::Vector2f normal(0.0f, 1.0f);
            if (len > 0.001f) {
                normal = sf::Vector2f(-dir.y / len, dir.x / len);
            }

            float halfW = (m_trailWidth * progress + 2.0f);
            sf::Vector2f p1 = m_trailPoints[i].position + normal * halfW;
            sf::Vector2f p2 = m_trailPoints[i].position - normal * halfW;

            sf::Color goldenColor(255, 220, 60, alpha);
            strip.append(sf::Vertex(p1, goldenColor));
            strip.append(sf::Vertex(p2, goldenColor));
        }

        target.draw(strip, sf::BlendAdd);
    }
}

void HolyVFX::renderSaintessGlow(sf::RenderTarget& target) {
    if (!m_saintessGlowActive) return;

    // Hào quang vàng kim bừng sáng xung quanh Thánh Nữ
    float pulse = std::sin(m_saintessGlowTimer * 8.0f) * 0.2f + 0.8f;
    float radius = 55.0f * pulse;

    sf::CircleShape halo(radius);
    halo.setOrigin(radius, radius);
    halo.setPosition(m_saintessAvatarPos);

    sf::Uint8 alpha = static_cast<sf::Uint8>(std::clamp((m_saintessGlowTimer / 1.5f) * 200.0f, 0.0f, 200.0f));
    halo.setFillColor(sf::Color(255, 235, 120, alpha / 3));
    halo.setOutlineThickness(4.0f);
    halo.setOutlineColor(sf::Color(255, 240, 160, alpha));

    target.draw(halo, sf::BlendAdd);
}
