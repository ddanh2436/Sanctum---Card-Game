#include "rendering/FloatingText.hpp"
#include "utils/Rng.hpp"
#include <algorithm>
#include <cmath>

void FloatingTextSystem::spawn(const std::string& content, sf::Vector2f pos, sf::Color color,
                               unsigned int charSize, float lifetime) {
    if (!m_font || content.empty()) return;

    FloatingLabel label;
    label.text.setFont(*m_font);
    label.text.setString(content);
    label.text.setCharacterSize(charSize);
    label.text.setStyle(sf::Text::Bold);
    label.text.setFillColor(color);
    label.text.setOutlineColor(sf::Color(0, 0, 0, 200));
    label.text.setOutlineThickness(2.0f);

    sf::FloatRect b = label.text.getLocalBounds();
    label.text.setOrigin(b.left + b.width / 2.0f, b.top + b.height / 2.0f);

    // Several labels spawning on the same spot (e.g. "-16 HP" and "BLOCK 8")
    // would sit on top of each other and become unreadable. Lift each new one
    // above every live label already near that point.
    float stackOffset = 0.0f;
    for (const auto& existing : m_labels) {
        const sf::Vector2f p = existing.text.getPosition();
        if (std::abs(p.x - pos.x) < 90.0f && std::abs(p.y - pos.y) < 34.0f) {
            stackOffset -= 30.0f;
        }
    }

    // Lệch ngang ngẫu nhiên để nhiều con số không chồng khít lên nhau
    label.text.setPosition(pos.x + Rng::rangeF(-14.0f, 14.0f), pos.y + stackOffset);
    label.velocity = { Rng::rangeF(-12.0f, 12.0f), -58.0f };
    label.life = lifetime;
    label.maxLife = lifetime;

    m_labels.push_back(std::move(label));
}

void FloatingTextSystem::spawnDamage(int amount, sf::Vector2f pos) {
    if (amount <= 0) return;
    spawn("-" + std::to_string(amount), pos, sf::Color(255, 225, 110), 26);
}

void FloatingTextSystem::spawnPlayerHit(int amount, sf::Vector2f pos) {
    if (amount <= 0) return;
    spawn("-" + std::to_string(amount) + " HP", pos, sf::Color(255, 90, 90), 24);
}

void FloatingTextSystem::spawnBlocked(int amount, sf::Vector2f pos) {
    if (amount <= 0) return;
    spawn("BLOCK " + std::to_string(amount), pos, sf::Color(120, 190, 255), 20);
}

void FloatingTextSystem::spawnHeal(int amount, sf::Vector2f pos) {
    if (amount <= 0) return;
    spawn("+" + std::to_string(amount) + " HP", pos, sf::Color(140, 240, 160), 22);
}

void FloatingTextSystem::spawnBuff(const std::string& label, sf::Vector2f pos) {
    spawn(label, pos, sf::Color(235, 235, 255), 18);
}

void FloatingTextSystem::update(float dt) {
    for (auto& label : m_labels) {
        label.life -= dt;
        label.text.move(label.velocity * dt);
        label.velocity.y += 34.0f * dt; // Hơi rơi chậm lại ở cuối

        // Mờ dần trong 45% thời gian cuối
        float ratio = label.life / label.maxLife;
        float alphaRatio = std::clamp(ratio / 0.45f, 0.0f, 1.0f);
        sf::Uint8 alpha = static_cast<sf::Uint8>(alphaRatio * 255.0f);

        sf::Color fill = label.text.getFillColor();
        fill.a = alpha;
        label.text.setFillColor(fill);

        sf::Color outline = label.text.getOutlineColor();
        outline.a = static_cast<sf::Uint8>(alphaRatio * 200.0f);
        label.text.setOutlineColor(outline);
    }

    m_labels.erase(
        std::remove_if(m_labels.begin(), m_labels.end(),
                       [](const FloatingLabel& l) { return l.life <= 0.0f; }),
        m_labels.end());
}

void FloatingTextSystem::render(sf::RenderTarget& target) {
    for (const auto& label : m_labels) {
        target.draw(label.text);
    }
}
