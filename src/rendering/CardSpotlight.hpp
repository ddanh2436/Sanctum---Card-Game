#pragma once

#include "battle/Board.hpp"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

/**
 * @brief Holds the duel for a beat and shows the card that is resolving.
 *
 * The rules engine resolves an operation instantly, so without this a spell is
 * a number changing and a counter-protocol is a frame dying for no stated
 * reason. This puts the card on screen, large enough to read, for long enough
 * to read it.
 *
 * It deliberately does NOT fire on every deployment. An earlier version did,
 * and a panel between the player and the board every time the enemy played a
 * one-cost drone is an interruption, not information. It is reserved for the
 * three moments the board genuinely cannot explain on its own: an operation
 * resolving, a counter-protocol flipping, and a titan landing.
 *
 * The player's cards come in from the left, the enemy's from the right, so
 * "who did this" is answered by the direction of travel before the text is
 * read.
 */
class CardSpotlight {
public:
    enum class Kind { Operation, Counter, Titan };

    void setFont(const sf::Font& font) { m_font = &font; }

    /// Put a card on screen. `scrapAt` is where its remains drift to when the
    /// spotlight closes - the owner's scrap pile for a spell or trap.
    void show(const CardData& card, Side side, Kind kind, sf::Vector2f scrapAt);

    void update(float dt);
    void render(sf::RenderTarget& target);
    void clear();

    /// True while the duel should hold. The AI checks this before acting.
    bool busy() const { return m_state != State::Idle; }

private:
    enum class State { Idle, SlidingIn, Holding, SlidingOut };

    struct Mote {
        sf::Vector2f position;
        sf::Vector2f velocity;
        sf::Color colour;
        float life = 0.0f;
        float maxLife = 1.0f;
        float size = 2.0f;
    };

    const sf::Font* m_font = nullptr;

    State m_state = State::Idle;
    float m_timer = 0.0f;

    CardData m_card;
    Side m_side = Side::Player;
    Kind m_kind = Kind::Operation;
    sf::Vector2f m_scrapAt;

    sf::Vector2f m_from;
    sf::Vector2f m_to;
    sf::Vector2f m_pos;

    /// Digital motes the card breaks into on the way out.
    std::vector<Mote> m_motes;
    bool m_dissolved = false;

    void spawnDissolve();
    sf::Color accent() const;
    const char* bannerText() const;

    static constexpr float kSlideIn = 0.24f;
    static constexpr float kHold = 1.10f;
    static constexpr float kSlideOut = 0.34f;
    static constexpr float kScrimAlpha = 118.0f;
};
