#include "battle/Commander.hpp"
#include "utils/Rng.hpp"
#include <algorithm>

Commander::Commander(std::string name, MechRole primary, MechRole secondary, int hp)
    : m_name(std::move(name)), m_primaryRole(primary), m_secondaryRole(secondary),
      m_maxHp(hp), m_hp(hp) {}

void Commander::gainOvercharge(int amount) {
    if (amount <= 0) return;
    m_overcharge = std::min(kMaxOvercharge, m_overcharge + amount);
}

int Commander::spendOvercharge(int amount) {
    const int spent = std::max(0, std::min(amount, m_overcharge));
    m_overcharge -= spent;
    return spent;
}

int Commander::dumpOvercharge() {
    const int held = m_overcharge;
    m_overcharge = 0;
    return held;
}

int Commander::takeDamage(int amount) {
    if (amount <= 0) return 0;
    const int dealt = std::min(amount, m_hp);
    m_hp -= dealt;
    return dealt;
}

void Commander::heal(int amount) {
    if (amount <= 0 || m_hp <= 0) return;
    m_hp = std::min(m_maxHp, m_hp + amount);
}

void Commander::setHp(int hp) {
    m_hp = std::clamp(hp, 0, m_maxHp);
}

bool Commander::spendMana(int cost) {
    if (cost < 0 || getMana() < cost) return false;

    // Burn the borrowed mana first; it evaporates at end of turn anyway.
    const int fromTemp = std::min(m_tempMana, cost);
    m_tempMana -= fromTemp;
    m_mana -= (cost - fromTemp);
    return true;
}

void Commander::beginTurnMana() {
    m_manaCap = std::min(kMaxManaCap, m_manaCap + 1);
    m_mana = m_manaCap;
    m_tempMana = 0;
}

void Commander::setDeck(std::vector<CardData> deck) {
    m_deck = std::move(deck);
    m_hand.clear();
    m_crypt.clear();
    m_burned = 0;
}

void Commander::shuffleDeck() {
    std::shuffle(m_deck.begin(), m_deck.end(), Rng::engine());
}

bool Commander::draw() {
    if (m_deck.empty()) return false;

    CardData card = m_deck.back();
    m_deck.pop_back();

    // Over the hand limit the card is burned straight to the crypt rather than
    // silently vanishing, so the crypt stays an honest record of the game.
    if (static_cast<int>(m_hand.size()) >= kHandLimit) {
        m_crypt.push_back(card);
        ++m_burned;
        return false;
    }

    m_hand.push_back(std::move(card));
    return true;
}

int Commander::draw(int count) {
    int drawn = 0;
    for (int i = 0; i < count; ++i) {
        if (draw()) ++drawn;
    }
    return drawn;
}

void Commander::discardFromHand(size_t index) {
    if (index >= m_hand.size()) return;
    m_crypt.push_back(m_hand[index]);
    m_hand.erase(m_hand.begin() + static_cast<std::ptrdiff_t>(index));
}
