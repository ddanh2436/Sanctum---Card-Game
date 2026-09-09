#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

/**
 * @brief Chữ nổi bay lên khi gây sát thương / hồi máu / chặn đòn.
 *
 * Không có phản hồi dạng này người chơi không biết chuyện gì vừa xảy ra
 * trong lượt quái - máu chỉ đơn giản là tụt xuống.
 */
struct FloatingLabel {
    sf::Text text;
    sf::Vector2f velocity;
    float life = 1.0f;
    float maxLife = 1.0f;
};

class FloatingTextSystem {
private:
    std::vector<FloatingLabel> m_labels;
    const sf::Font* m_font = nullptr;

public:
    void setFont(const sf::Font& font) { m_font = &font; }

    /// Chữ tùy ý bay lên tại vị trí pos
    void spawn(const std::string& content, sf::Vector2f pos, sf::Color color,
               unsigned int charSize = 22, float lifetime = 1.5f);

    // Các mẫu dựng sẵn theo tone màu của game
    void spawnDamage(int amount, sf::Vector2f pos);   // Vàng thánh quang
    void spawnPlayerHit(int amount, sf::Vector2f pos);// Đỏ máu
    void spawnBlocked(int amount, sf::Vector2f pos);  // Xanh thép
    void spawnHeal(int amount, sf::Vector2f pos);     // Xanh lá dịu
    void spawnBuff(const std::string& label, sf::Vector2f pos); // Trắng bạc

    void update(float dt);
    void render(sf::RenderTarget& target);
    void clear() { m_labels.clear(); }

    size_t getActiveCount() const { return m_labels.size(); }
};
