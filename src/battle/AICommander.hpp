#pragma once

#include "battle/DuelEngine.hpp"

/**
 * @brief The Eclipse Commander's brain.
 *
 * It plays through the public DuelEngine API - the same calls a human makes -
 * so it can never do anything the rules forbid. `takeTurn` runs the whole turn;
 * `step` performs one action at a time so the UI can pace the animation.
 */
class AICommander {
public:
    explicit AICommander(Side side = Side::Opponent) : m_side(side) {}

    /// Perform one action. Returns false when the AI has nothing left to do.
    bool step(DuelEngine& duel);

    /// Run the whole turn, then end it. Used by the headless simulator.
    void takeTurn(DuelEngine& duel, int actionBudget = 40);

private:
    Side m_side;

    bool tryPlayCard(DuelEngine& duel);
    bool tryAdvance(DuelEngine& duel);
    bool tryAttack(DuelEngine& duel);

    /// How much the AI wants this card right now, or a negative score to skip.
    int scoreCard(const DuelEngine& duel, const CardData& card, int cost) const;
    /// Pick tributes for a summon: cheapest and least useful units first.
    std::vector<int> chooseTributes(const DuelEngine& duel, const CardData& card, int needed) const;
    /// Rate an attack; higher is better.
    int scoreAttack(const DuelEngine& duel, const Unit& attacker, int targetId) const;

    /// How many living allies a Guard in this position is screening. Killing a
    /// Guard is worth what it is protecting, not a flat bonus.
    int guardCoverage(const DuelEngine& duel, const Unit& guard) const;
    /// Where to put this card, rather than the first hole in the row.
    int bestSlotFor(const DuelEngine& duel, const CardData& card, BoardLine line) const;
    /// What arming this counter-protocol is worth, on the same scale as a unit.
    int scoreTrap(const DuelEngine& duel, const CardData& card) const;
};
