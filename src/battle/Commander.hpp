#pragma once

#include "battle/CardData.hpp"
#include <string>
#include <vector>

/**
 * @brief One duellist: reactor, energy curve, deck, hand and scrap yard.
 *
 * Energy follows the KARDS / Hearthstone curve - the cap rises by one each turn
 * to a ceiling, and refills at upkeep. `tempMana` covers effects that lend
 * energy for a single turn.
 *
 * A commander also carries the two doctrines of the Dual-Core Protocol. The
 * primary core decides the commander passive; the secondary is only a splash.
 * Overcharge is the Paladin resource: it persists across turns and is spent by
 * cards rather than refilled.
 */
class Commander {
public:
    static constexpr int kStartingHp = 30;
    static constexpr int kMaxManaCap = 10;
    static constexpr int kHandLimit = 8;
    static constexpr int kOpeningHand = 4;
    /// The core cannot bank more than this, so Paladin decks must spend.
    static constexpr int kMaxOvercharge = 12;

    Commander() = default;
    Commander(std::string name, MechRole primary, MechRole secondary,
              int hp = kStartingHp);

    // --- identity ---
    const std::string& getName() const { return m_name; }
    MechRole getPrimaryRole() const { return m_primaryRole; }
    MechRole getSecondaryRole() const { return m_secondaryRole; }
    /// True when this card is legal for the commander's two cores.
    bool ownsRole(MechRole role) const {
        return role == m_primaryRole || role == m_secondaryRole;
    }

    // --- life ---
    int getHp() const { return m_hp; }
    int getMaxHp() const { return m_maxHp; }
    bool isAlive() const { return m_hp > 0; }
    /// Returns the damage actually taken.
    int takeDamage(int amount);
    void heal(int amount);
    void setHp(int hp);

    // --- mana ---
    int getMana() const { return m_mana + m_tempMana; }
    int getManaCap() const { return m_manaCap; }
    bool canAfford(int cost) const { return getMana() >= cost; }
    bool spendMana(int cost);
    void addTempMana(int amount) { m_tempMana += amount; }
    /// Overclocked Core: how far above the normal ceiling this run reaches.
    void setManaCapBonus(int amount) { m_manaCapBonus = amount; }
    /// Thermal Recycler: energy handed to the NEXT upkeep. A refund earned on
    /// the enemy's turn is worthless if it lands in energy that is about to be
    /// cleared, so it waits.
    void bankMana(int amount) { m_bankedMana += amount; }
    /// Raise the cap by one (to the ceiling) and refill.
    void beginTurnMana();

    // --- overcharge core (Paladin) ---
    int getOvercharge() const { return m_overcharge; }
    void gainOvercharge(int amount);
    /// Spend up to `amount`; returns how much was actually drawn from the core.
    int spendOvercharge(int amount);
    /// Vent the whole core and report what it held.
    int dumpOvercharge();

    // --- cards ---
    std::vector<CardData>& getDeck() { return m_deck; }
    std::vector<CardData>& getHand() { return m_hand; }
    std::vector<CardData>& getCrypt() { return m_crypt; }
    const std::vector<CardData>& getDeck() const { return m_deck; }
    const std::vector<CardData>& getHand() const { return m_hand; }
    const std::vector<CardData>& getCrypt() const { return m_crypt; }

    void setDeck(std::vector<CardData> deck);
    void shuffleDeck();

    /// Draw one card. Returns false when the deck is empty (fatigue-free:
    /// an empty deck simply stops producing cards) or the hand is full.
    bool draw();
    int draw(int count);

    void discardFromHand(size_t index);
    void sendToCrypt(const CardData& card) { m_crypt.push_back(card); }

    /// Cards burned because the hand was already at its limit.
    int getBurnedCards() const { return m_burned; }

private:
    std::string m_name = "Commander";
    MechRole m_primaryRole = MechRole::Vanguard;
    MechRole m_secondaryRole = MechRole::Siege;

    int m_maxHp = kStartingHp;
    int m_hp = kStartingHp;

    int m_manaCap = 0;
    int m_mana = 0;
    int m_tempMana = 0;
    int m_manaCapBonus = 0;
    int m_bankedMana = 0;
    int m_overcharge = 0;

    std::vector<CardData> m_deck;
    std::vector<CardData> m_hand;
    std::vector<CardData> m_crypt;
    int m_burned = 0;

    friend class DuelEngine;
    void clearTempMana() { m_tempMana = 0; }
};
