#pragma once

#include <SFML/Graphics.hpp>
#include <deque>

struct TrailPoint {
    sf::Vector2f position;
    float timeRemaining;
};

/**
 * @brief Quản lý các hiệu ứng đồ họa đặc biệt (SFML Game Feel):
 * 1. Golden Trail: Dùng sf::VertexArray(sf::TriangleStrip) vẽ vệt sáng hoàng kim khi kéo thẻ Smite
 * 2. Cathedral Bell & Holy Glow: Phát sáng hào quang Thánh Nữ khi đạt 10 Faith
 * 3. Screen Shake: Rung camera ngẫu nhiên trên sf::View trong 0.15s khi sát thương > 15
 */
class HolyVFX {
private:
    // Golden Trail
    std::deque<TrailPoint> m_trailPoints;
    const float m_trailPointLifetime = 0.25f;
    const float m_trailWidth = 14.0f;
    bool m_isTrailActive = false;

    // Cathedral Glow
    float m_saintessGlowTimer = 0.0f;
    bool m_saintessGlowActive = false;
    sf::Vector2f m_saintessAvatarPos = { 180.0f, 480.0f };

    // Screen Shake
    float m_shakeTimer = 0.0f;
    float m_shakeDuration = 0.15f;
    float m_shakeMagnitude = 10.0f;
    sf::Vector2f m_defaultViewCenter;
    sf::Vector2f m_shakeOffset;

public:
    HolyVFX(sf::Vector2f defaultCenter = { 640.0f, 360.0f });

    // Trail methods
    void setTrailActive(bool active);
    void addTrailPoint(sf::Vector2f point);
    void clearTrail();

    // Saintess Bloom Glow
    void triggerSaintessGlow();

    // Screen Shake
    void triggerScreenShake(float duration = 0.15f, float magnitude = 12.0f);

    /// Advance timers and recompute the shake offset.
    void update(float dt);
    /// Convenience overload that also drives an sf::View directly.
    void update(float dt, sf::View& view);
    /// Current camera displacement, zero when nothing is shaking.
    sf::Vector2f getShakeOffset() const { return m_shakeOffset; }
    void render(sf::RenderTarget& target);
    void renderSaintessGlow(sf::RenderTarget& target);

    void setDefaultCenter(sf::Vector2f center) { m_defaultViewCenter = center; }
    void setSaintessAvatarPos(sf::Vector2f pos) { m_saintessAvatarPos = pos; }
};
