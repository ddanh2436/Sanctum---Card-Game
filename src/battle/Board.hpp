#pragma once

#include "battle/CardData.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

enum class Side { Player = 0, Opponent = 1 };

inline Side other(Side s) { return s == Side::Player ? Side::Opponent : Side::Player; }
inline int index(Side s) { return static_cast<int>(s); }

/**
 * @brief A summoned card on the board.
 *
 * Stats are kept as base + permanent + aura rather than one mutable number, so
 * an aura can be recomputed from scratch every time the board changes without
 * losing permanent modifiers (kill-growth, a Grid Snare's weakening) or
 * resurrecting damage that was already dealt.
 */
struct Unit {
    CardData data;

    /// Who controls it. Stored explicitly because both sides can field the same
    /// doctrine, and because Inquisitor effects hijack units across the line.
    Side owner = Side::Player;

    int permAttack = 0;   // permanent modifiers, may be negative
    int permHealth = 0;
    int auraAttack = 0;   // rebuilt by DuelEngine::recomputeAuras()
    int auraHealth = 0;
    int damage = 0;       // accumulated damage; healing removes it

    int armour = 0;        // temporary, expires at your next upkeep
    int stunTurns = 0;     // cannot attack or advance while positive
    /// Blinding Corona: for this many of its own turns the frame swings for
    /// `blindPenalty` less and cannot connect with a healthy target at all.
    int blindTurns = 0;
    int blindPenalty = 0;
    bool shorted = false;  // EMP burn: loses 1 health at the start of each of your turns
    bool wardOff = false;  // cannot be destroyed by spells or traps (aura granted)

    /// Fired from the support row this round, so enemy melee can reach it
    /// until its next upkeep. Artillery gives away its position.
    bool exposed = false;
    /// Entrench: cleared at upkeep, set the moment the frame advances. A frame
    /// that has broken cover is no longer dug in.
    bool advancedThisTurn = false;

    bool exhausted = true;        // deployment lag
    int attacksThisTurn = 0;
    int extraAttacks = 0;         // granted by ExtraAttackPerTurn
    int auraRangedBonus = 0;      // rebuilt by DuelEngine::recomputeAuras()

    int instanceId = 0;

    int attack() const { return std::max(0, data.attack + permAttack + auraAttack); }
    int maxHealth() const { return std::max(1, data.health + permHealth + auraHealth); }
    int health() const { return maxHealth() - damage; }
    bool isAlive() const { return health() > 0; }
    int attacksAllowed() const { return 1 + extraAttacks; }
    bool canAct() const {
        return isAlive() && !exhausted && stunTurns == 0
            && attacksThisTurn < attacksAllowed();
    }
    bool hasKeyword(Keyword::Mask k) const { return data.hasKeyword(k); }
};

/// A trap sitting in a trap zone, face-down until its condition fires.
struct TrapCard {
    CardData data;
    bool faceDown = true;
    int instanceId = 0;
};

/// Where a unit sits on the board.
struct UnitLocation {
    Side side = Side::Player;
    BoardLine line = BoardLine::Frontline;
    int slot = -1;
    bool valid() const { return slot >= 0; }
};

/**
 * @brief The two-sided battlefield: a frontline and a support row per side,
 *        plus a three-slot trap zone.
 *
 * Slots are positional because position is part of the game: the frontline
 * screens the support row, and a full frontline denies attacks on the commander.
 */
class Board {
public:
    static constexpr int kLineSlots = 4;
    static constexpr int kTrapSlots = 3;

    using Line = std::array<std::unique_ptr<Unit>, kLineSlots>;

    Line& line(Side side, BoardLine which);
    const Line& line(Side side, BoardLine which) const;

    std::vector<TrapCard>& traps(Side side) { return m_traps[index(side)]; }
    const std::vector<TrapCard>& traps(Side side) const { return m_traps[index(side)]; }

    int freeSlot(Side side, BoardLine which) const;
    bool hasRoom(Side side, BoardLine which) const { return freeSlot(side, which) >= 0; }
    bool trapZoneFull(Side side) const {
        return static_cast<int>(m_traps[index(side)].size()) >= kTrapSlots;
    }

    Unit* at(Side side, BoardLine which, int slot);
    const Unit* at(Side side, BoardLine which, int slot) const;

    /// Place a unit in a specific slot. Fails if the slot is taken.
    bool place(Side side, BoardLine which, int slot, std::unique_ptr<Unit> unit);
    /// Lift a unit out of its slot without destroying it (used when advancing).
    std::unique_ptr<Unit> take(Side side, BoardLine which, int slot);

    std::vector<Unit*> units(Side side);
    std::vector<const Unit*> units(Side side) const;
    std::vector<Unit*> unitsIn(Side side, BoardLine which);
    std::vector<const Unit*> unitsIn(Side side, BoardLine which) const;
    std::vector<Unit*> allUnits();

    Unit* findById(int instanceId);
    const Unit* findById(int instanceId) const;
    UnitLocation locate(int instanceId) const;

    bool lineEmpty(Side side, BoardLine which) const;
    bool sideEmpty(Side side) const;
    int unitCount(Side side) const;

    /// Remove every unit at zero health and hand the corpses back so the caller
    /// can run deathrattles and file them in the crypt.
    std::vector<Unit> collectDead();

    void clear();

private:
    std::array<Line, 2> m_frontline;
    std::array<Line, 2> m_support;
    std::array<std::vector<TrapCard>, 2> m_traps;
};
