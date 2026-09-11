#pragma once

#include <cstdint>
#include <string>
#include <vector>

// =============================================================================
// Card catalogue for Sanctum: Mecha-Chivalry.
//
// Numbers live in assets/data/cards.json; behaviour lives in DuelEngine.
// A card describes WHAT it does through these enums and values, and the engine
// decides HOW. Balance stays tunable without a rebuild, and there is no
// half-built scripting language to maintain.
// =============================================================================

/// The six combat doctrines. A deck is built from exactly two of them.
enum class MechRole {
    Vanguard,    // reactive armour, taunt, holds the frontline
    Paladin,     // overcharge core, burst plasma
    Valkyrie,    // nanite repair, salvage from the scrap yard
    Dragoon,     // thrusters, blitz, strikes past the line
    Siege,       // fixed artillery, ranged and splash
    Inquisitor   // counter-protocols, EMP, electronic warfare
};

constexpr int kMechRoleCount = 6;

/// The two rows each side fields. The frontline screens the support row.
enum class BoardLine { Frontline, Support };

enum class CardCategory { Unit, Spell, Trap };

/// Summoning tier. Tier2 may tribute one friendly unit, Tier3 must tribute two.
enum class CardTier { Tier1, Tier2, Tier3 };

/// Static combat keywords, combined as flags.
namespace Keyword {
using Mask = std::uint32_t;
inline constexpr Mask None     = 0;
inline constexpr Mask Taunt    = 1u << 0; // enemies must engage this first
inline constexpr Mask Rush     = 1u << 1; // may attack the turn it deploys
inline constexpr Mask Ranged   = 1u << 2; // fires from any row, takes no return fire
inline constexpr Mask Reactive = 1u << 3; // reactive armour: 1 less damage per hit
inline constexpr Mask Overkill = 1u << 4; // excess damage spills into the reactor
inline constexpr Mask Plasma   = 1u << 5; // burns through absorption plating
inline constexpr Mask EMP      = 1u << 6; // its hits short out the target for a turn
inline constexpr Mask Aerial   = 1u << 7; // thrusters over the line into the support row
inline constexpr Mask Splash   = 1u << 8; // also hits the units flanking the target
inline constexpr Mask Thruster = 1u << 9; // advancing costs no energy
inline constexpr Mask Intercept = 1u << 10; // shoots down Aerial crossing its lanes
}

/// When an ability fires.
enum class AbilityTrigger {
    None,
    OnDeploy,     // resolves the moment the unit hits the board
    OnTurnEnd,    // at the end of its controller's turn
    OnTurnStart,  // at the start of its controller's turn
    OnDestroyed,  // salvage / detonation
    OnKill,       // after it destroys an enemy unit
    OnDamaged,    // after it survives a hit
    Aura          // continuous while it is on the board
};

/// What an ability does. `value` / `value2` carry the numbers from JSON.
enum class AbilityKind {
    None,
    RepairWoundedAlly,      // value HP to the most damaged ally
    RepairSelfEachTurn,     // value HP to itself
    DamageEnemyFrontline,   // value damage to every enemy frontline unit
    DamageEnemyBackline,    // value damage split across the enemy support row
    DamageKiller,           // detonates into whatever destroyed it
    DrawCards,              // value cards
    RepairReactor,          // value HP to your own commander
    GainOvercharge,         // value overcharge points
    SpendOverchargeToStrike,// spend value overcharge for a splash attack
    DumpOverchargeOnDeploy, // release the whole core across the enemy board
    AuraBuffFrontline,      // +value attack / +value2 health to allied frontline
    AuraArmourFrontline,    // value absorption to allied frontline each upkeep
    AuraRangedBonus,        // +value damage to allied ranged attacks
    GainStatsOnKill,        // +value attack, repair value2
    ExtraAttackPerTurn,     // may attack value extra times each turn
    RetreatAfterKill,       // falls back to the support row after a kill
    ReviveBestFromScrap,    // returns the strongest wreck to the board
    SpawnScrapDroneOnAllyLoss, // replaces a destroyed neighbour with a 1/1
    RevealEnemyTrap,        // peek at one set card
    SeizeUnitOnTrapFlip,    // hijack an enemy unit whenever a trap of yours fires
    BuffPerArmedCounter,    // +value attack for each counter-protocol you have armed
    ReflectToRow            // returns damage to the attacker's whole row
};

struct Ability {
    AbilityTrigger trigger = AbilityTrigger::None;
    AbilityKind kind = AbilityKind::None;
    int value = 0;
    int value2 = 0;
};

/// Spell resolution, chosen by kind.
enum class SpellKind {
    None,
    ArmourAllAllies,        // value absorption to every friendly unit
    DestroyHighAttackEnemy, // destroy one enemy unit with attack >= value
    WipeLowHealthUnits,     // destroy every unit on the board with health <= value
    DrawThenRepairIfLosses, // draw value; repair value2 if you lost a unit this turn
    OverchargeSurge,        // value overcharge, this turn only
    DamageEnemyFrontline    // value damage to every enemy frontline unit
};

/// Conditions that can flip a face-down counter-protocol.
enum class TrapTrigger {
    None,
    OnEnemyAttackReactor,   // an enemy declares an attack on your commander
    OnEnemySpellCast,       // the enemy plays a spell
    OnEnemyHighTierDeploy,  // the enemy deploys a Tier 2 or Tier 3 unit
    OnEnemyAdvance,         // an enemy unit moves up into the frontline
    OnAllyTargetedByRemoval,// a spell tries to destroy one of your units
    OnOwnTitanDestroyed,    // one of your own Tier 3 units is destroyed
    OnAllyDestroyed,        // any frame of yours is scrapped
    OnEnemyUnitAttack,      // an enemy declares an attack on one of your frames
    OnEnemyAerialAttack     // an enemy flier commits to a strike
};

enum class TrapKind {
    None,
    BlockAndCrushWeak,      // stop the attack, destroy the attacker if health <= value
    NegateSpellAndBurn,     // cancel the spell, burn the caster for its cost
    OverloadSummon,         // the deployed unit takes half its own attack
    WeakenAdvancingUnit,    // the advancing unit permanently loses value / value2
    RecallTargetToHand,
    VentOverchargeAtReactor,// block `value`, then dump the whole core at their reactor
    BlindAttacker,          // the attacker loses `value` attack and misses healthy targets
    ReassembleDyingAlly,    // the frame is rebuilt at 1 health in the support row
    LeechAndMend,           // drain `value` from the actor, mend every hurt ally by `value`
    MinefieldSplash,        // `value` to the actor, `value2` to each frame beside it
    ShootDownFlier,         // scrap a flier at or under `value` health, else break the strike     // cancel the removal and return the unit to hand
    DetonateForTitanAttack  // damage equal to the dead titan's attack, split
};

/// One entry in the card catalogue.
struct CardData {
    std::string id;
    std::string name;
    MechRole role = MechRole::Vanguard;
    CardCategory category = CardCategory::Unit;
    CardTier tier = CardTier::Tier1;

    int manaCost = 1;          // energy
    int attack = 0;
    int health = 0;
    int overchargeCost = 0;    // Paladin cards that draw on the core

    Keyword::Mask keywords = Keyword::None;
    std::vector<Ability> abilities;

    SpellKind spell = SpellKind::None;
    int spellValue = 0;
    int spellValue2 = 0;

    TrapTrigger trapTrigger = TrapTrigger::None;
    TrapKind trapKind = TrapKind::None;
    int trapValue = 0;
    int trapValue2 = 0;

    int deckCount = 1;         // copies available when this role is in your deck
    std::string description;
    std::string textureFile;

    bool hasKeyword(Keyword::Mask k) const { return (keywords & k) != 0; }

    /// Tributes required to summon at the discounted price (0, 1 or 2).
    int tributeRequirement() const {
        switch (tier) {
        case CardTier::Tier2: return 1;
        case CardTier::Tier3: return 2;
        default:              return 0;
        }
    }

    /// Energy saved per unit offered in tribute.
    static constexpr int kTributeDiscount = 2;
};

/// The data token: cards.json, avatar filenames, printed records. Stable.
const char* toString(MechRole role);
/// What the player reads. Use this everywhere a human sees the doctrine.
const char* displayName(MechRole role);
const char* roleTitle(MechRole role);       // e.g. "Iron Shield Knights"
const char* rolePassiveName(MechRole role); // e.g. "Aegis Plating"
const char* rolePassiveText(MechRole role);
bool parseMechRole(const std::string& text, MechRole& out);
