#pragma once

#include "battle/Board.hpp"
#include "battle/Commander.hpp"
#include <array>
#include <string>
#include <vector>

/// Something the rules engine did, for the battle log and the animation layer.
struct DuelEvent {
    enum class Type {
        Log,
        TurnStarted,
        CardDrawn,
        UnitSummoned,
        UnitAdvanced,
        SpellCast,
        TrapSet,
        TrapFlipped,
        AttackDeclared,
        UnitDamaged,
        UnitHealed,
        UnitDestroyed,
        CommanderDamaged,
        CommanderHealed,
        DuelEnded
    };

    Type type = Type::Log;
    Side side = Side::Player;
    int instanceId = -1;   // unit the event is about, when relevant
    int targetId = -1;
    int amount = 0;
    std::string text;      // human-readable line for the battle log
    /// Catalogue id of the card behind the event, when there is one. The
    /// presentation layer needs it to show what was played: by the time a
    /// spell resolves the card is already in the scrap yard, and a name string
    /// is not enough to draw it.
    std::string cardId;
};

/// Why an action was rejected, so the UI can say something useful.
enum class ActionResult {
    Ok,
    NotYourTurn,
    NoSuchCard,
    NotEnoughMana,
    RowFull,
    TrapZoneFull,
    NeedsTribute,
    InvalidTribute,
    UnitCannotAct,
    IllegalTarget,
    DuelOver
};

const char* toString(ActionResult result);

/**
 * @brief The complete rules engine for a Sanctum duel. No SFML, no rendering.
 *
 * Everything the game does to the board goes through here, which keeps the
 * rules in one testable place and lets the AI run the exact same code paths a
 * human does rather than a parallel "AI can cheat" implementation.
 */
/// Everything needed to seat one duellist. Grouped into a struct because the
/// Dual-Core Protocol added two roles per side and a flat argument list of
/// eight parameters is a bug waiting to happen.
struct DuelistSetup {
    std::vector<CardData> deck;
    MechRole primary = MechRole::Vanguard;
    MechRole secondary = MechRole::Siege;
    int hp = Commander::kStartingHp;
    std::string name = "Commander";
};

class DuelEngine {
public:
    static constexpr int kMoveCost = 1;   // operation cost to advance a unit
    static constexpr int kSetTrapCost = 1;
    /// Reactor strain per empty draw, escalating each turn it happens.
    static constexpr int kFatigueStep = 1;
    /// Overcharge Core: what it earns each upkeep, what a plasma strike draws,
    /// and what that draw buys.
    static constexpr int kOverchargePerUpkeep = 2;
    /// How many lanes either side a melee frame can swing into.
    static constexpr int kMeleeReach = 1;
    static constexpr int kOverchargePerStrike = 1;
    static constexpr int kOverchargeStrikeBonus = 2;

    /// Cipher Tribunal: what one resolved counter-protocol is worth.
    static constexpr int kTribunalDraw = 1;
    static constexpr int kTribunalBurn = 2;

    void startDuel(DuelistSetup player, DuelistSetup opponent);

    // ---------------- queries ----------------
    Board& board() { return m_board; }
    const Board& board() const { return m_board; }
    Commander& commander(Side side) { return m_commanders[index(side)]; }
    const Commander& commander(Side side) const { return m_commanders[index(side)]; }

    Side activeSide() const { return m_active; }
    int turnNumber() const { return m_turn; }
    bool isOver() const { return m_over; }
    Side winner() const { return m_winner; }

    /// Cost after the tribute discount, given how many units are offered.
    int summonCost(const CardData& card, int tributeCount) const;
    /// Every legal attack target for a unit (unit ids; -1 means the commander).
    std::vector<int> legalTargets(int attackerId) const;
    bool canAdvance(int unitId) const;
    /// True when a Guard stands immediately beside this position.
    bool isScreened(const UnitLocation& where) const;
    /// True when neither row of `side` holds anything in lane `slot`.
    bool laneIsOpen(Side side, int slot) const;
    /// Energy this side pays to advance right now (Thruster / Dragoon make it free).
    int advanceCost(int unitId) const;

    /// Inquisitor's Cold Read: this side can see the enemy's face-down cards.
    bool seesEnemyTraps(Side viewer) const;
    /// Energy to arm a counter-protocol (Inquisitor arms for free).
    int trapCost(Side side) const;
    /// The commander passive granted by this side's primary core.
    MechRole passiveRole(Side side) const { return commander(side).getPrimaryRole(); }

    // ---------------- actions ----------------
    ActionResult summonFromHand(Side side, size_t handIndex, BoardLine line, int slot,
                                const std::vector<int>& tributeIds = {});
    ActionResult castSpell(Side side, size_t handIndex, int targetId = -1);
    ActionResult setTrap(Side side, size_t handIndex);
    ActionResult advanceUnit(Side side, int unitId);
    ActionResult declareAttack(Side side, int attackerId, int targetId);
    void endTurn();

    // ---------------- events ----------------
    const std::vector<DuelEvent>& events() const { return m_events; }
    std::vector<DuelEvent> drainEvents();

private:
    Board m_board;
    std::array<Commander, 2> m_commanders;
    Side m_active = Side::Player;
    Side m_winner = Side::Player;
    int m_turn = 0;
    bool m_over = false;
    int m_nextInstanceId = 1;

    // Per-turn passive budgets, reset at each upkeep.
    std::array<bool, 2> m_freeAdvanceUsed { false, false };   // Dragoon
    std::array<bool, 2> m_salvageUsed { false, false };       // Valkyrie
    std::array<int, 2> m_unitsLostThisTurn { 0, 0 };          // drives Scrap Protocol
    /// Reactor strain: escalating damage once a salvage line runs dry.
    std::array<int, 2> m_fatigue { 0, 0 };

    std::vector<DuelEvent> m_events;

    // --- turn flow ---
    void beginTurn(Side side);
    void runTurnStartEffects(Side side);
    void runTurnEndEffects(Side side);

    // --- helpers ---
    // --- commander passives ---
    /// Extra damage this side's ranged frames deal (Siege: Fire Support).
    int rangedBonus(Side side) const;
    /// Damage this unit shrugs off before armour (Reactive / Vanguard: Aegis).
    int platingOf(const Unit& unit) const;

    void emit(DuelEvent::Type type, Side side, const std::string& text,
              int instanceId = -1, int amount = 0, int targetId = -1,
              const std::string& cardId = std::string());
    void log(const std::string& text) { emit(DuelEvent::Type::Log, m_active, text); }

    /**
     * Draw `count` cards and announce each one separately.
     *
     * Every draw in the game goes through here so that a CardDrawn event names
     * the card it is about. The events used to be one per *effect* and carried
     * no id at all, which was enough for a log line and nothing else - the
     * presentation layer could not tell how many cards moved, let alone which,
     * so it could not animate them leaving the pile.
     *
     * `text` is put on the first event only; the rest are silent, so a five
     * card opening hand is five animations and one log line.
     */
    int drawFor(Side side, int count, const std::string& text = std::string());

    std::unique_ptr<Unit> makeUnit(const CardData& card, Side owner);
    void recomputeAuras();
    void damageUnit(Unit& unit, int amount, bool plasma, Unit* source, Side attackerSide);
    /// A Splash attack's collateral onto the slots flanking the target.
    void splashNeighbours(const UnitLocation& centre, int amount, Side attackerSide);
    void healUnit(Unit& unit, int amount);
    void damageCommander(Side side, int amount, const std::string& reason);
    void resolveDeaths(Unit* killer, Side killerSide);
    void runAbilities(Unit& unit, AbilityTrigger trigger, Unit* other = nullptr);
    void checkGameOver();
    Side sideOf(const Unit& unit) const;

    // --- traps ---
    bool fireTraps(Side defender, TrapTrigger trigger, Unit* actor, int* outNegated = nullptr);
    void resolveTrap(TrapCard& trap, Side owner, Unit* actor, int* outNegated);
    void destroyAllTraps(int* destroyedCount);
    /// Deus Ex-Machina: steal an enemy frontline frame when a trap of ours fires.
    void seizeEnemyUnit(Side owner);
    /// Inquisitor doctrine payoff, applied wherever a counter-protocol resolves.
    void applyTribunalPayoff(Side owner);

    // --- spells ---
    void resolveSpell(Side side, const CardData& card, int targetId);
};
