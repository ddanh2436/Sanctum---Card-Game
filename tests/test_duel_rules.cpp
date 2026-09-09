// Rules coverage for the duel engine. No window, no SFML surface needed.

#include "TestAssert.hpp"
#include "battle/DuelEngine.hpp"
#include "run/DeckBuilder.hpp"
#include "utils/DataLoader.hpp"
#include "utils/Rng.hpp"
#include <iostream>

namespace {

/// Inquisitor is the neutral role for combat tests: its Cold Read passive only
/// changes what a player can see, so it never perturbs a damage calculation.
/// Any test that cares about a passive names the role it wants explicitly.
constexpr MechRole kNeutral = MechRole::Inquisitor;
constexpr MechRole kNeutral2 = MechRole::Valkyrie;

CardData unitCard(const char* id, int cost, int atk, int hp,
                  Keyword::Mask keywords = Keyword::None,
                  CardTier tier = CardTier::Tier1,
                  MechRole role = kNeutral) {
    CardData card;
    card.id = id;
    card.name = id;
    card.role = role;
    card.category = CardCategory::Unit;
    card.tier = tier;
    card.manaCost = cost;
    card.attack = atk;
    card.health = hp;
    card.keywords = keywords;
    return card;
}

CardData spellCard(const char* id, int cost, SpellKind kind, int v1 = 0, int v2 = 0) {
    CardData card;
    card.id = id;
    card.name = id;
    card.category = CardCategory::Spell;
    card.manaCost = cost;
    card.spell = kind;
    card.spellValue = v1;
    card.spellValue2 = v2;
    return card;
}

CardData trapCard(const char* id, TrapTrigger trigger, TrapKind kind, int v1 = 0, int v2 = 0) {
    CardData card;
    card.id = id;
    card.name = id;
    card.category = CardCategory::Trap;
    card.manaCost = 1;
    card.trapTrigger = trigger;
    card.trapKind = kind;
    card.trapValue = v1;
    card.trapValue2 = v2;
    return card;
}

/// A deck of filler so draws never fail during a test.
std::vector<CardData> filler(size_t count, const CardData& card) {
    return std::vector<CardData>(count, card);
}

DuelistSetup seat(std::vector<CardData> deck, MechRole primary = kNeutral,
                  MechRole secondary = MechRole::Siege,
                  int hp = Commander::kStartingHp) {
    DuelistSetup setup;
    setup.deck = std::move(deck);
    setup.primary = primary;
    setup.secondary = secondary;
    setup.hp = hp;
    setup.name = toString(primary);
    return setup;
}

/// Start a plain duel where neither passive is in play.
void startNeutral(DuelEngine& duel, const CardData& mine, const CardData& theirs) {
    duel.startDuel(seat(filler(40, mine), kNeutral, MechRole::Siege),
                   seat(filler(40, theirs), kNeutral2, MechRole::Siege));
}

/// Put an exact card at hand index 0. Decks are shuffled at the start of a
/// duel, so a test must never assume what the opening draw produced.
void giveCard(DuelEngine& duel, Side side, const CardData& card) {
    auto& hand = duel.commander(side).getHand();
    hand.insert(hand.begin(), card);
}

/// Advance to the player's turn with at least `cap` energy.
void reachEnergy(DuelEngine& duel, Side side, int cap) {
    for (int guard = 0; guard < 40; ++guard) {
        if (duel.activeSide() == side && duel.commander(side).getManaCap() >= cap) return;
        duel.endTurn();
    }
}

} // namespace

// -----------------------------------------------------------------------------

void test_energy_curve_and_opening_hand() {
    DuelEngine duel;
    CardData grunt = unitCard("grunt", 1, 1, 1);
    startNeutral(duel, grunt, grunt);

    // Opening hand of 4, plus the turn-one draw.
    CHECK(duel.commander(Side::Player).getHand().size() == Commander::kOpeningHand + 1);
    CHECK(duel.commander(Side::Player).getManaCap() == 1);
    CHECK(duel.activeSide() == Side::Player);

    duel.endTurn();                                    // opponent turn 1
    CHECK(duel.commander(Side::Opponent).getManaCap() == 1);
    duel.endTurn();                                    // player turn 2
    CHECK(duel.commander(Side::Player).getManaCap() == 2);

    for (int i = 0; i < 30; ++i) duel.endTurn();
    CHECK(duel.commander(Side::Player).getManaCap() == Commander::kMaxManaCap);

    std::cout << "[PASS] test_energy_curve_and_opening_hand\n";
}

void test_tribute_reduces_deploy_cost() {
    DuelEngine duel;
    CardData grunt = unitCard("grunt", 1, 1, 1);
    CardData titan = unitCard("titan", 8, 8, 8, Keyword::None, CardTier::Tier3);

    CHECK(duel.summonCost(titan, 0) == 8);
    CHECK(duel.summonCost(titan, 1) == 6);
    CHECK(duel.summonCost(titan, 2) == 4);

    CardData mid = unitCard("mid", 5, 5, 5, Keyword::None, CardTier::Tier2);
    CHECK(duel.summonCost(mid, 1) == 3);
    CHECK(duel.summonCost(mid, 2) == 3);   // never discounts beyond the requirement

    // And it actually works on the board.
    startNeutral(duel, grunt, grunt);
    giveCard(duel, Side::Player, grunt);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);

    reachEnergy(duel, Side::Player, 4);
    giveCard(duel, Side::Player, titan);
    const int gruntId = duel.board().units(Side::Player).front()->instanceId;

    // One tribute brings a Tier 3 to 6 energy, still out of reach at 4.
    CHECK(duel.commander(Side::Player).getManaCap() == 4);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1, { gruntId })
          == ActionResult::NotEnoughMana);

    reachEnergy(duel, Side::Player, 6);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1, { gruntId })
          == ActionResult::Ok);
    CHECK(duel.board().findById(gruntId) == nullptr);   // the tribute was consumed

    std::cout << "[PASS] test_tribute_reduces_deploy_cost\n";
}

void test_frontline_screens_the_reactor() {
    DuelEngine duel;
    CardData wall = unitCard("wall", 1, 0, 5);
    CardData attacker = unitCard("attacker", 1, 3, 3, Keyword::Rush);
    startNeutral(duel, wall, attacker);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();

    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int foeId = duel.board().units(Side::Opponent).front()->instanceId;
    const auto targets = duel.legalTargets(foeId);
    CHECK(std::find(targets.begin(), targets.end(), -1) == targets.end());
    CHECK(duel.declareAttack(Side::Opponent, foeId, -1) == ActionResult::IllegalTarget);

    std::cout << "[PASS] test_frontline_screens_the_reactor\n";
}

void test_melee_must_clear_the_whole_board_for_the_reactor() {
    DuelEngine duel;
    CardData backline = unitCard("backline", 1, 1, 2);
    CardData attacker = unitCard("attacker", 1, 4, 4, Keyword::Rush);
    startNeutral(duel, backline, attacker);

    // Defender keeps one frame, in the support row: the frontline is open but
    // the board is not clear.
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);

    const int foeId = duel.board().units(Side::Opponent).front()->instanceId;
    const int mineId = duel.board().units(Side::Player).front()->instanceId;

    // Melee cannot reach past a frame that is still standing, wherever it is.
    auto targets = duel.legalTargets(foeId);
    CHECK(std::find(targets.begin(), targets.end(), -1) == targets.end());
    CHECK(duel.declareAttack(Side::Opponent, foeId, -1) == ActionResult::IllegalTarget);

    // The support frame is what it can reach, since its own lane is clear.
    CHECK(std::find(targets.begin(), targets.end(), mineId) != targets.end());
    CHECK(duel.declareAttack(Side::Opponent, foeId, mineId) == ActionResult::Ok);

    // Board clear, and now the reactor is open.
    CHECK(duel.board().sideEmpty(Side::Player));
    duel.endTurn();
    duel.endTurn();
    targets = duel.legalTargets(foeId);
    CHECK(std::find(targets.begin(), targets.end(), -1) != targets.end());

    const int hpBefore = duel.commander(Side::Player).getHp();
    CHECK(duel.declareAttack(Side::Opponent, foeId, -1) == ActionResult::Ok);
    CHECK(duel.commander(Side::Player).getHp() == hpBefore - 4);

    std::cout << "[PASS] test_melee_must_clear_the_whole_board_for_the_reactor\n";
}

void test_ranged_shells_the_reactor_through_a_full_board() {
    DuelEngine duel;
    CardData wall = unitCard("wall", 1, 1, 6, Keyword::Taunt);
    CardData gun = unitCard("gun", 1, 3, 3, Keyword::Ranged | Keyword::Rush);
    startNeutral(duel, wall, gun);

    // Defender puts up a Guard; artillery ranges over it anyway.
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Support, 0) == ActionResult::Ok);

    const int gunId = duel.board().units(Side::Opponent).front()->instanceId;
    const auto targets = duel.legalTargets(gunId);
    CHECK(std::find(targets.begin(), targets.end(), -1) != targets.end());

    const int hpBefore = duel.commander(Side::Player).getHp();
    CHECK(duel.declareAttack(Side::Opponent, gunId, -1) == ActionResult::Ok);
    CHECK(duel.commander(Side::Player).getHp() < hpBefore);

    // Firing from the back gives the position away until its next upkeep.
    CHECK(duel.board().findById(gunId)->exposed);

    std::cout << "[PASS] test_ranged_shells_the_reactor_through_a_full_board\n";
}

void test_melee_reaches_one_lane_either_side() {
    DuelEngine duel;
    CardData body = unitCard("body", 1, 1, 4);
    CardData attacker = unitCard("attacker", 1, 3, 3, Keyword::Rush);
    startNeutral(duel, body, attacker);

    // Defenders in lanes 0 and 3 - opposite ends of the row.
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    reachEnergy(duel, Side::Player, 2);
    giveCard(duel, Side::Player, body);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 3) == ActionResult::Ok);
    const int nearId = duel.board().at(Side::Player, BoardLine::Frontline, 0)->instanceId;
    const int farId = duel.board().at(Side::Player, BoardLine::Frontline, 3)->instanceId;
    duel.endTurn();

    // Attacker lands in lane 1: it reaches lane 0, never lane 3.
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 1) == ActionResult::Ok);
    const int foeId = duel.board().at(Side::Opponent, BoardLine::Frontline, 1)->instanceId;

    const auto targets = duel.legalTargets(foeId);
    CHECK(std::find(targets.begin(), targets.end(), nearId) != targets.end());
    CHECK(std::find(targets.begin(), targets.end(), farId) == targets.end());
    CHECK(duel.declareAttack(Side::Opponent, foeId, farId) == ActionResult::IllegalTarget);
    CHECK(duel.declareAttack(Side::Opponent, foeId, nearId) == ActionResult::Ok);

    std::cout << "[PASS] test_melee_reaches_one_lane_either_side\n";
}

void test_guard_screens_only_its_neighbours() {
    DuelEngine duel;
    CardData guard = unitCard("guard", 1, 1, 6, Keyword::Taunt);
    CardData plain = unitCard("plain", 1, 1, 3);
    CardData sniper = unitCard("sniper", 1, 3, 3, Keyword::Ranged | Keyword::Rush);
    startNeutral(duel, plain, sniper);

    // Lane 0 Guard, lane 1 beside it, lane 3 well clear of it.
    giveCard(duel, Side::Player, guard);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    reachEnergy(duel, Side::Player, 3);
    giveCard(duel, Side::Player, plain);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1) == ActionResult::Ok);
    giveCard(duel, Side::Player, plain);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 3) == ActionResult::Ok);

    const int guardId = duel.board().at(Side::Player, BoardLine::Frontline, 0)->instanceId;
    const int besideId = duel.board().at(Side::Player, BoardLine::Frontline, 1)->instanceId;
    const int awayId = duel.board().at(Side::Player, BoardLine::Frontline, 3)->instanceId;
    duel.endTurn();

    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Support, 0) == ActionResult::Ok);
    const int foeId = duel.board().units(Side::Opponent).front()->instanceId;
    const auto targets = duel.legalTargets(foeId);

    // The Guard itself is always fair game - otherwise a wall of them would
    // lock the board with nothing to shoot.
    CHECK(std::find(targets.begin(), targets.end(), guardId) != targets.end());
    // Its neighbour is covered.
    CHECK(std::find(targets.begin(), targets.end(), besideId) == targets.end());
    // Two lanes away is not.
    CHECK(std::find(targets.begin(), targets.end(), awayId) != targets.end());

    CHECK(duel.declareAttack(Side::Opponent, foeId, besideId) == ActionResult::IllegalTarget);

    std::cout << "[PASS] test_guard_screens_only_its_neighbours\n";
}


void test_ranged_reaches_support_and_dodges_return_fire() {
    DuelEngine duel;
    CardData gun = unitCard("gun", 1, 3, 2, Keyword::Ranged);
    CardData backline = unitCard("backline", 1, 5, 4);
    startNeutral(duel, gun, backline);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Support, 0) == ActionResult::Ok);
    duel.endTurn();

    const int gunId = duel.board().units(Side::Player).front()->instanceId;
    const int targetId = duel.board().units(Side::Opponent).front()->instanceId;

    // Ranged sees the support row even from the back.
    const auto targets = duel.legalTargets(gunId);
    CHECK(std::find(targets.begin(), targets.end(), targetId) != targets.end());

    CHECK(duel.declareAttack(Side::Player, gunId, targetId) == ActionResult::Ok);
    const Unit* gunUnit = duel.board().findById(gunId);
    CHECK(gunUnit != nullptr);
    CHECK(gunUnit->damage == 0);   // no return fire despite the 5-attack defender

    std::cout << "[PASS] test_ranged_reaches_support_and_dodges_return_fire\n";
}

void test_aerial_strikes_past_the_line_but_guard_still_holds() {
    DuelEngine duel;
    CardData flyer = unitCard("flyer", 1, 4, 3, Keyword::Aerial | Keyword::Rush);
    CardData support = unitCard("support", 1, 1, 6);
    startNeutral(duel, support, flyer);

    // Player fields a body in each row - the frontline one has no Guard.
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    reachEnergy(duel, Side::Player, 2);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    duel.endTurn();

    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int flyerId = duel.board().units(Side::Opponent).front()->instanceId;
    const Unit* rear = duel.board().unitsIn(Side::Player, BoardLine::Support).front();
    const int rearId = rear->instanceId;

    // The support row is reachable over an unguarded frontline.
    const auto targets = duel.legalTargets(flyerId);
    CHECK(std::find(targets.begin(), targets.end(), rearId) != targets.end());

    CHECK(duel.declareAttack(Side::Opponent, flyerId, rearId) == ActionResult::Ok);
    CHECK(duel.board().findById(rearId)->damage == 4);
    CHECK(duel.board().findById(flyerId)->damage == 0);   // no return fire

    std::cout << "[PASS] test_aerial_strikes_past_the_line_but_guard_still_holds\n";
}

void test_reactive_plating_and_return_fire() {
    DuelEngine duel;
    CardData plated = unitCard("plated", 1, 2, 5, Keyword::Reactive);
    CardData striker = unitCard("striker", 1, 3, 5, Keyword::Rush);
    startNeutral(duel, plated, striker);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);

    const int defId = duel.board().units(Side::Player).front()->instanceId;
    const int atkId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, atkId, defId) == ActionResult::Ok);

    // 3 damage minus 1 for reactive plating, and the defender hits back for 2.
    CHECK(duel.board().findById(defId)->damage == 2);
    CHECK(duel.board().findById(atkId)->damage == 2);

    std::cout << "[PASS] test_reactive_plating_and_return_fire\n";
}

void test_vanguard_passive_stacks_with_reactive_on_the_frontline_only() {
    DuelEngine duel;
    CardData plated = unitCard("plated", 1, 0, 9, Keyword::Reactive);
    CardData striker = unitCard("striker", 1, 5, 9, Keyword::Rush | Keyword::Aerial);

    // The player commands a Vanguard core; the enemy is neutral.
    duel.startDuel(seat(filler(40, plated), MechRole::Vanguard, MechRole::Siege),
                   seat(filler(40, striker), kNeutral2, MechRole::Siege));

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    reachEnergy(duel, Side::Player, 2);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    duel.endTurn();

    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int flyerId = duel.board().units(Side::Opponent).front()->instanceId;
    const int frontId = duel.board().unitsIn(Side::Player, BoardLine::Frontline).front()->instanceId;
    const int rearId = duel.board().unitsIn(Side::Player, BoardLine::Support).front()->instanceId;

    // Support row: reactive only, so 5 - 1 = 4.
    CHECK(duel.declareAttack(Side::Opponent, flyerId, rearId) == ActionResult::Ok);
    CHECK(duel.board().findById(rearId)->damage == 4);

    duel.endTurn();
    duel.endTurn();

    // Frontline: reactive plus Aegis Plating, so 5 - 2 = 3.
    CHECK(duel.declareAttack(Side::Opponent, flyerId, frontId) == ActionResult::Ok);
    CHECK(duel.board().findById(frontId)->damage == 3);

    std::cout << "[PASS] test_vanguard_passive_stacks_with_reactive_on_the_frontline_only\n";
}

void test_overkill_spills_into_the_reactor() {
    DuelEngine duel;
    CardData chump = unitCard("chump", 1, 0, 2);
    CardData breaker = unitCard("breaker", 1, 9, 8, Keyword::Overkill | Keyword::Rush);
    startNeutral(duel, chump, breaker);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);

    const int hpBefore = duel.commander(Side::Player).getHp();
    const int defId = duel.board().units(Side::Player).front()->instanceId;
    const int atkId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, atkId, defId) == ActionResult::Ok);

    // 9 damage into a 2-health frame: 7 rolls through.
    CHECK(duel.commander(Side::Player).getHp() == hpBefore - 7);

    std::cout << "[PASS] test_overkill_spills_into_the_reactor\n";
}

void test_plasma_ignores_absorption_plating() {
    DuelEngine duel;
    CardData target = unitCard("target", 1, 0, 9);
    CardData burner = unitCard("burner", 1, 4, 4, Keyword::Plasma | Keyword::Rush);
    CardData blunt = unitCard("blunt", 1, 4, 4, Keyword::Rush);
    CardData plating = spellCard("plating", 1, SpellKind::ArmourAllAllies, 3);

    startNeutral(duel, target, burner);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    reachEnergy(duel, Side::Player, 2);
    giveCard(duel, Side::Player, plating);
    CHECK(duel.castSpell(Side::Player, 0) == ActionResult::Ok);
    const int defId = duel.board().units(Side::Player).front()->instanceId;
    CHECK(duel.board().findById(defId)->armour == 3);
    duel.endTurn();

    // A blunt hit is absorbed down to 1; plasma lands all 4.
    giveCard(duel, Side::Opponent, blunt);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int bluntId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, bluntId, defId) == ActionResult::Ok);
    CHECK(duel.board().findById(defId)->damage == 1);
    CHECK(duel.board().findById(defId)->armour == 0);

    std::cout << "[PASS] test_plasma_ignores_absorption_plating\n";
}

void test_emp_shorts_out_the_target() {
    DuelEngine duel;
    CardData victim = unitCard("victim", 1, 3, 9);
    CardData jammer = unitCard("jammer", 1, 1, 9, Keyword::EMP | Keyword::Rush);
    startNeutral(duel, victim, jammer);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);

    const int victimId = duel.board().units(Side::Player).front()->instanceId;
    const int jammerId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, jammerId, victimId) == ActionResult::Ok);

    const Unit* shorted = duel.board().findById(victimId);
    CHECK(shorted != nullptr);
    CHECK(shorted->stunTurns >= 1);
    CHECK(shorted->shorted);
    CHECK(!shorted->canAct());
    CHECK(duel.declareAttack(Side::Player, victimId, jammerId) == ActionResult::NotYourTurn);

    // On the player's upkeep the stun ticks off but the burn costs 1 health.
    const int damageBefore = shorted->damage;
    duel.endTurn();
    CHECK(duel.activeSide() == Side::Player);
    CHECK(duel.board().findById(victimId)->damage == damageBefore + 1);

    std::cout << "[PASS] test_emp_shorts_out_the_target\n";
}

void test_splash_hits_the_flanking_frames() {
    DuelEngine duel;
    CardData body = unitCard("body", 1, 0, 9);
    CardData sweeper = unitCard("sweeper", 1, 6, 9, Keyword::Splash | Keyword::Rush);
    startNeutral(duel, body, sweeper);

    // Three bodies side by side in slots 0, 1, 2.
    for (int slot = 0; slot < 3; ++slot) {
        reachEnergy(duel, Side::Player, 1);
        giveCard(duel, Side::Player, body);
        CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, slot) == ActionResult::Ok);
        if (slot < 2) duel.endTurn();
    }
    const int leftId = duel.board().at(Side::Player, BoardLine::Frontline, 0)->instanceId;
    const int midId = duel.board().at(Side::Player, BoardLine::Frontline, 1)->instanceId;
    const int rightId = duel.board().at(Side::Player, BoardLine::Frontline, 2)->instanceId;
    duel.endTurn();

    giveCard(duel, Side::Opponent, sweeper);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int sweeperId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, sweeperId, midId) == ActionResult::Ok);

    CHECK(duel.board().findById(midId)->damage == 6);    // the target takes it all
    CHECK(duel.board().findById(leftId)->damage == 3);   // neighbours take half
    CHECK(duel.board().findById(rightId)->damage == 3);

    std::cout << "[PASS] test_splash_hits_the_flanking_frames\n";
}

void test_thruster_and_dragoon_passive_make_advancing_free() {
    DuelEngine duel;
    CardData walker = unitCard("walker", 1, 1, 3);
    CardData jet = unitCard("jet", 1, 1, 3, Keyword::Thruster);

    // A Dragoon core: the first advance each turn costs nothing.
    duel.startDuel(seat(filler(40, walker), MechRole::Dragoon, MechRole::Siege),
                   seat(filler(40, walker), kNeutral2, MechRole::Siege));

    reachEnergy(duel, Side::Player, 4);
    giveCard(duel, Side::Player, walker);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    giveCard(duel, Side::Player, walker);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 1) == ActionResult::Ok);

    const int firstId = duel.board().at(Side::Player, BoardLine::Support, 0)->instanceId;
    const int secondId = duel.board().at(Side::Player, BoardLine::Support, 1)->instanceId;

    CHECK(duel.advanceCost(firstId) == 0);              // Thruster Assault
    const int energyBefore = duel.commander(Side::Player).getMana();
    CHECK(duel.advanceUnit(Side::Player, firstId) == ActionResult::Ok);
    CHECK(duel.commander(Side::Player).getMana() == energyBefore);

    CHECK(duel.advanceCost(secondId) == DuelEngine::kMoveCost);   // budget spent
    CHECK(duel.advanceUnit(Side::Player, secondId) == ActionResult::Ok);
    CHECK(duel.commander(Side::Player).getMana() == energyBefore - DuelEngine::kMoveCost);

    // A Thruster frame ignores the budget entirely, on any core.
    DuelEngine other;
    startNeutral(other, jet, walker);
    reachEnergy(other, Side::Player, 2);
    giveCard(other, Side::Player, jet);
    CHECK(other.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    const int jetId = other.board().units(Side::Player).front()->instanceId;
    CHECK(other.advanceCost(jetId) == 0);

    std::cout << "[PASS] test_thruster_and_dragoon_passive_make_advancing_free\n";
}

void test_paladin_core_powers_plasma_strikes() {
    DuelEngine duel;
    CardData lancer = unitCard("lancer", 1, 4, 4, Keyword::Plasma | Keyword::Rush);
    CardData blunt = unitCard("blunt", 1, 4, 4, Keyword::Rush);
    CardData wall = unitCard("wall", 1, 0, 30);

    duel.startDuel(seat(filler(40, lancer), MechRole::Paladin, MechRole::Vanguard),
                   seat(filler(40, wall), kNeutral2, MechRole::Siege));

    // One overcharge per upkeep, starting on turn one, and only for a Paladin.
    CHECK(duel.commander(Side::Player).getOvercharge() == DuelEngine::kOverchargePerUpkeep);
    duel.endTurn();
    CHECK(duel.commander(Side::Opponent).getOvercharge() == 0);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.commander(Side::Player).getOvercharge() == 2 * DuelEngine::kOverchargePerUpkeep);

    giveCard(duel, Side::Player, lancer);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int mine = duel.board().units(Side::Player).front()->instanceId;
    const int theirs = duel.board().units(Side::Opponent).front()->instanceId;

    // A plasma strike draws 1 from the core and lands for 4 + 2.
    CHECK(duel.declareAttack(Side::Player, mine, theirs) == ActionResult::Ok);
    CHECK(duel.commander(Side::Player).getOvercharge()
          == 2 * DuelEngine::kOverchargePerUpkeep - DuelEngine::kOverchargePerStrike);
    CHECK(duel.board().findById(theirs)->damage == 4 + DuelEngine::kOverchargeStrikeBonus);

    // A frame without Plasma never touches the core, however full it is.
    reachEnergy(duel, Side::Player, 2);
    const int before = duel.commander(Side::Player).getOvercharge();
    CHECK(before > 0);
    giveCard(duel, Side::Player, blunt);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1) == ActionResult::Ok);
    const int bluntId = duel.board().unitsIn(Side::Player, BoardLine::Frontline)[1]->instanceId;
    const int damageBefore = duel.board().findById(theirs)->damage;
    CHECK(duel.declareAttack(Side::Player, bluntId, theirs) == ActionResult::Ok);
    CHECK(duel.commander(Side::Player).getOvercharge() == before);
    CHECK(duel.board().findById(theirs)->damage == damageBefore + 4);

    std::cout << "[PASS] test_paladin_core_powers_plasma_strikes\n";
}

void test_plasma_ignores_reactive_plating() {
    DuelEngine duel;
    // The defender has Reactive AND a Vanguard commander, so -2 to every
    // ordinary hit. Plasma should not care about either.
    CardData plated = unitCard("plated", 1, 0, 20, Keyword::Reactive);
    CardData burner = unitCard("burner", 1, 5, 5, Keyword::Plasma | Keyword::Rush);
    CardData blunt = unitCard("blunt", 1, 5, 5, Keyword::Rush);

    duel.startDuel(seat(filler(40, plated), MechRole::Vanguard, MechRole::Siege),
                   seat(filler(40, burner), kNeutral2, MechRole::Siege));

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int defId = duel.board().units(Side::Player).front()->instanceId;
    duel.endTurn();

    giveCard(duel, Side::Opponent, blunt);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int bluntId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, bluntId, defId) == ActionResult::Ok);
    CHECK(duel.board().findById(defId)->damage == 3);          // 5 - reactive - aegis

    reachEnergy(duel, Side::Opponent, 2);
    giveCard(duel, Side::Opponent, burner);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 1) == ActionResult::Ok);
    const int burnerId = duel.board().unitsIn(Side::Opponent, BoardLine::Frontline)[1]->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, burnerId, defId) == ActionResult::Ok);
    CHECK(duel.board().findById(defId)->damage == 3 + 5);       // plating ignored

    std::cout << "[PASS] test_plasma_ignores_reactive_plating\n";
}

void test_valkyrie_reclaims_the_first_frame_lost_each_turn() {
    DuelEngine duel;
    CardData fragile = unitCard("fragile", 1, 0, 1);
    CardData killer = unitCard("killer", 1, 9, 9, Keyword::Rush | Keyword::Splash);

    duel.startDuel(seat(filler(40, fragile), MechRole::Valkyrie, MechRole::Siege),
                   seat(filler(40, killer), kNeutral2, MechRole::Siege));

    // Two fragile frames next to each other, so one splash kills both.
    reachEnergy(duel, Side::Player, 2);
    giveCard(duel, Side::Player, fragile);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    giveCard(duel, Side::Player, fragile);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1) == ActionResult::Ok);
    duel.endTurn();

    const size_t deckBefore = duel.commander(Side::Player).getDeck().size();
    const size_t scrapBefore = duel.commander(Side::Player).getCrypt().size();

    giveCard(duel, Side::Opponent, killer);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int killerId = duel.board().units(Side::Opponent).front()->instanceId;
    const int targetId = duel.board().at(Side::Player, BoardLine::Frontline, 0)->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, killerId, targetId) == ActionResult::Ok);

    // Both frames died. Exactly one was rebuilt into the deck; the other was
    // scrapped - the passive is once per turn, not once per loss.
    CHECK(duel.board().unitCount(Side::Player) == 0);
    CHECK(duel.commander(Side::Player).getDeck().size() == deckBefore + 1);
    CHECK(duel.commander(Side::Player).getCrypt().size() == scrapBefore + 1);

    std::cout << "[PASS] test_valkyrie_reclaims_the_first_frame_lost_each_turn\n";
}

void test_ranged_frames_are_soft() {
    DuelEngine duel;
    CardData gun = unitCard("gun", 1, 1, 9, Keyword::Ranged);
    CardData plain = unitCard("plain", 1, 1, 9);
    CardData striker = unitCard("striker", 1, 4, 9, Keyword::Rush | Keyword::Aerial);
    startNeutral(duel, gun, striker);

    // One artillery frame and one ordinary frame, both in the support row.
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    reachEnergy(duel, Side::Player, 2);
    giveCard(duel, Side::Player, plain);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 1) == ActionResult::Ok);
    duel.endTurn();

    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int flyerId = duel.board().units(Side::Opponent).front()->instanceId;
    const int gunId = duel.board().at(Side::Player, BoardLine::Support, 0)->instanceId;
    const int plainId = duel.board().at(Side::Player, BoardLine::Support, 1)->instanceId;

    CHECK(duel.declareAttack(Side::Opponent, flyerId, gunId) == ActionResult::Ok);
    CHECK(duel.board().findById(gunId)->damage == 5);     // 4 + 1: artillery is soft

    duel.endTurn();
    duel.endTurn();
    CHECK(duel.declareAttack(Side::Opponent, flyerId, plainId) == ActionResult::Ok);
    CHECK(duel.board().findById(plainId)->damage == 4);   // an ordinary frame takes 4

    std::cout << "[PASS] test_ranged_frames_are_soft\n";
}

void test_siege_fire_support_boosts_ranged_only() {
    DuelEngine duel;
    CardData gun = unitCard("gun", 1, 3, 3, Keyword::Ranged);
    CardData melee = unitCard("melee", 1, 3, 3);
    CardData target = unitCard("target", 1, 0, 9);

    duel.startDuel(seat(filler(40, gun), MechRole::Siege, MechRole::Vanguard),
                   seat(filler(40, target), kNeutral2, MechRole::Siege));

    reachEnergy(duel, Side::Player, 2);
    giveCard(duel, Side::Player, gun);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Support, 0) == ActionResult::Ok);
    giveCard(duel, Side::Player, melee);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();

    const int gunId = duel.board().unitsIn(Side::Player, BoardLine::Support).front()->instanceId;
    const int meleeId = duel.board().unitsIn(Side::Player, BoardLine::Frontline).front()->instanceId;
    const int targetId = duel.board().units(Side::Opponent).front()->instanceId;

    CHECK(duel.declareAttack(Side::Player, gunId, targetId) == ActionResult::Ok);
    CHECK(duel.board().findById(targetId)->damage == 4);      // 3 + Fire Support

    CHECK(duel.declareAttack(Side::Player, meleeId, targetId) == ActionResult::Ok);
    CHECK(duel.board().findById(targetId)->damage == 7);      // melee gets no bonus

    std::cout << "[PASS] test_siege_fire_support_boosts_ranged_only\n";
}

void test_reactive_armor_protocol_blocks_and_crushes() {
    DuelEngine duel;
    CardData counter = trapCard("counter", TrapTrigger::OnEnemyAttackReactor,
                                TrapKind::BlockAndCrushWeak, 3);
    CardData attacker = unitCard("attacker", 1, 6, 3, Keyword::Rush);
    startNeutral(duel, counter, attacker);

    CHECK(duel.setTrap(Side::Player, 0) == ActionResult::Ok);
    CHECK(duel.board().traps(Side::Player).size() == 1);
    CHECK(duel.board().traps(Side::Player)[0].faceDown == true);
    duel.endTurn();

    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int atkId = duel.board().units(Side::Opponent).front()->instanceId;

    const int myHp = duel.commander(Side::Player).getHp();
    CHECK(duel.declareAttack(Side::Opponent, atkId, -1) == ActionResult::Ok);

    CHECK(duel.commander(Side::Player).getHp() == myHp);         // the strike is blocked
    CHECK(duel.board().findById(atkId) == nullptr);              // 3 health: crushed
    CHECK(duel.board().traps(Side::Player).empty());             // the counter is spent

    std::cout << "[PASS] test_reactive_armor_protocol_blocks_and_crushes\n";
}

void test_firewall_blackout_negates_a_spell_and_burns_the_caster() {
    DuelEngine duel;
    CardData firewall = trapCard("firewall", TrapTrigger::OnEnemySpellCast,
                                 TrapKind::NegateSpellAndBurn);
    CardData body = unitCard("body", 1, 0, 9);
    CardData strike = spellCard("strike", 4, SpellKind::DamageEnemyFrontline, 3);
    startNeutral(duel, body, firewall);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int bodyId = duel.board().units(Side::Player).front()->instanceId;
    duel.endTurn();
    CHECK(duel.setTrap(Side::Opponent, 0) == ActionResult::Ok);

    reachEnergy(duel, Side::Player, 4);
    giveCard(duel, Side::Player, strike);
    const int myHp = duel.commander(Side::Player).getHp();
    CHECK(duel.castSpell(Side::Player, 0) == ActionResult::Ok);

    // The operation never resolves, and it costs the caster its energy in HP.
    CHECK(duel.board().findById(bodyId)->damage == 0);
    CHECK(duel.commander(Side::Player).getHp() == myHp - 4);
    CHECK(duel.board().traps(Side::Opponent).empty());

    std::cout << "[PASS] test_firewall_blackout_negates_a_spell_and_burns_the_caster\n";
}

void test_overload_virus_punishes_a_big_deployment() {
    DuelEngine duel;
    CardData virus = trapCard("virus", TrapTrigger::OnEnemyHighTierDeploy,
                              TrapKind::OverloadSummon);
    CardData heavy = unitCard("heavy", 1, 8, 9, Keyword::None, CardTier::Tier2);
    startNeutral(duel, virus, heavy);

    CHECK(duel.setTrap(Side::Player, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);

    const Unit* deployed = duel.board().units(Side::Opponent).front();
    CHECK(deployed != nullptr);
    CHECK(deployed->damage == 4);   // half its own attack

    std::cout << "[PASS] test_overload_virus_punishes_a_big_deployment\n";
}

void test_grid_snare_weakens_an_advancing_frame() {
    DuelEngine duel;
    CardData snare = trapCard("snare", TrapTrigger::OnEnemyAdvance,
                              TrapKind::WeakenAdvancingUnit, 2, 2);
    CardData mover = unitCard("mover", 1, 5, 6);
    startNeutral(duel, snare, mover);

    CHECK(duel.setTrap(Side::Player, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Support, 0) == ActionResult::Ok);
    duel.endTurn();
    duel.endTurn();   // back to the opponent, with energy to advance

    const int moverId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.advanceUnit(Side::Opponent, moverId) == ActionResult::Ok);

    const Unit* snared = duel.board().findById(moverId);
    CHECK(snared != nullptr);
    CHECK(snared->attack() == 3);      // 5 - 2
    CHECK(snared->maxHealth() == 4);   // 6 - 2

    std::cout << "[PASS] test_grid_snare_weakens_an_advancing_frame\n";
}

void test_detonation_hits_the_killer() {
    DuelEngine duel;
    CardData plain = unitCard("plain", 1, 4, 4);
    CardData bomb = unitCard("bomb", 1, 1, 1);
    bomb.abilities.push_back({ AbilityTrigger::OnDestroyed, AbilityKind::DamageKiller, 2, 0 });
    startNeutral(duel, plain, bomb);

    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    duel.endTurn();

    const int mineId = duel.board().units(Side::Player).front()->instanceId;
    const int foeId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Player, mineId, foeId) == ActionResult::Ok);

    // The bomb dies, returns fire for 1, and its detonation adds 2 more.
    const Unit* survivor = duel.board().findById(mineId);
    CHECK(survivor != nullptr);
    CHECK(survivor->damage == 3);

    std::cout << "[PASS] test_detonation_hits_the_killer\n";
}

void test_aura_applies_and_expires() {
    DuelEngine duel;
    CardData grunt = unitCard("grunt", 1, 1, 1);
    CardData buffer = unitCard("buffer", 1, 1, 4);
    buffer.abilities.push_back({ AbilityTrigger::Aura, AbilityKind::AuraBuffFrontline, 1, 2 });
    CardData sniper = unitCard("sniper", 1, 9, 9, Keyword::Ranged | Keyword::Rush);

    startNeutral(duel, grunt, sniper);

    giveCard(duel, Side::Player, grunt);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int gruntId = duel.board().units(Side::Player).front()->instanceId;
    CHECK(duel.board().findById(gruntId)->attack() == 1);
    CHECK(duel.board().findById(gruntId)->maxHealth() == 1);

    reachEnergy(duel, Side::Player, 2);
    giveCard(duel, Side::Player, buffer);
    CHECK(duel.summonFromHand(Side::Player, 0, BoardLine::Frontline, 1) == ActionResult::Ok);

    // The field lifts the other frontline frame but not its own source.
    CHECK(duel.board().findById(gruntId)->attack() == 2);
    CHECK(duel.board().findById(gruntId)->maxHealth() == 3);

    // Destroy the buffer and the bonus must vanish rather than linger.
    Unit* bufferUnit = nullptr;
    for (Unit* unit : duel.board().units(Side::Player)) {
        if (unit->instanceId != gruntId) bufferUnit = unit;
    }
    CHECK(bufferUnit != nullptr);
    bufferUnit->damage = bufferUnit->maxHealth();
    duel.endTurn();

    CHECK(duel.board().findById(gruntId)->attack() == 1);
    CHECK(duel.board().findById(gruntId)->maxHealth() == 1);

    std::cout << "[PASS] test_aura_applies_and_expires\n";
}

void test_duel_ends_when_a_reactor_falls() {
    DuelEngine duel;
    CardData chump = unitCard("chump", 1, 0, 1);
    CardData killer = unitCard("killer", 1, 30, 30, Keyword::Rush);
    duel.startDuel(seat(filler(40, chump), kNeutral, MechRole::Siege, 30),
                   seat(filler(40, killer), kNeutral2, MechRole::Siege, 30));

    duel.endTurn();
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 0) == ActionResult::Ok);
    const int killerId = duel.board().units(Side::Opponent).front()->instanceId;
    CHECK(duel.declareAttack(Side::Opponent, killerId, -1) == ActionResult::Ok);

    CHECK(duel.isOver());
    CHECK(duel.winner() == Side::Opponent);
    CHECK(duel.summonFromHand(Side::Opponent, 0, BoardLine::Frontline, 1) == ActionResult::DuelOver);

    std::cout << "[PASS] test_duel_ends_when_a_reactor_falls\n";
}

// -----------------------------------------------------------------------------
// The Dual-Core Protocol
// -----------------------------------------------------------------------------

void test_deck_rules_reject_illegal_configurations() {
    DataLoader::loadCatalogue("assets/data/cards.json");

    DeckConfiguration legal = DeckBuilder::build(MechRole::Vanguard, MechRole::Valkyrie);
    CHECK_MSG(legal.isValidDeck(), toString(legal.validate()));
    CHECK(static_cast<int>(legal.cards.size()) == DeckRules::kDeckSize);
    CHECK(legal.countFor(MechRole::Vanguard) >= DeckRules::kMinPrimary);
    CHECK(legal.countFor(MechRole::Valkyrie) <= DeckRules::kMaxSecondary);

    // Same doctrine twice is not a pairing.
    DeckConfiguration doubled = legal;
    doubled.secondaryRole = doubled.primaryRole;
    CHECK(doubled.validate() == DeckError::SameRoleTwice);

    // Too few cards.
    DeckConfiguration short_ = legal;
    short_.cards.pop_back();
    CHECK(short_.validate() == DeckError::WrongSize);

    // A card from a third doctrine.
    DeckConfiguration foreign = legal;
    const auto siege = DeckBuilder::cardsForRole(MechRole::Siege);
    CHECK(!siege.empty());
    foreign.cards.back() = siege.front();
    CHECK(foreign.validate() == DeckError::ForeignCard);

    // The secondary core may never field its titan.
    DeckConfiguration titan = legal;
    const auto valkyrie = DeckBuilder::cardsForRole(MechRole::Valkyrie);
    const CardData* valkyrieTitan = nullptr;
    for (const CardData& card : valkyrie) {
        if (card.tier == CardTier::Tier3) valkyrieTitan = &card;
    }
    CHECK(valkyrieTitan != nullptr);
    titan.cards.back() = *valkyrieTitan;
    CHECK(titan.validate() == DeckError::SecondaryTitan);

    std::cout << "[PASS] test_deck_rules_reject_illegal_configurations\n";
}

void test_every_pairing_builds_a_legal_deck() {
    DataLoader::loadCatalogue("assets/data/cards.json");

    int built = 0;
    for (int a = 0; a < kMechRoleCount; ++a) {
        for (int b = 0; b < kMechRoleCount; ++b) {
            if (a == b) continue;
            const MechRole primary = static_cast<MechRole>(a);
            const MechRole secondary = static_cast<MechRole>(b);
            const DeckConfiguration config = DeckBuilder::build(primary, secondary);

            std::string where = std::string(toString(primary)) + "/" + toString(secondary)
                              + ": " + toString(config.validate());
            CHECK_MSG(config.isValidDeck(), where.c_str());
            ++built;
        }
    }
    CHECK(built == kMechRoleCount * (kMechRoleCount - 1));

    std::cout << "[PASS] test_every_pairing_builds_a_legal_deck (" << built << " pairings)\n";
}

void test_catalogue_loads_and_is_coherent() {
    const auto& cards = DataLoader::loadCatalogue("assets/data/cards.json");
    CHECK(cards.size() >= 40);

    int traps = 0, titans = 0;
    std::vector<int> perRole(kMechRoleCount, 0);
    std::vector<int> titansPerRole(kMechRoleCount, 0);

    for (const CardData& card : cards) {
        CHECK(!card.id.empty());
        CHECK(!card.name.empty());
        CHECK(card.manaCost >= 0);
        CHECK(card.deckCount >= 1);
        if (card.category == CardCategory::Unit) {
            CHECK(card.attack >= 0);
            CHECK(card.health > 0);
        }
        if (card.category == CardCategory::Spell) {
            CHECK(card.spell != SpellKind::None);
        }
        if (card.category == CardCategory::Trap) {
            CHECK(card.trapTrigger != TrapTrigger::None);
            CHECK(card.trapKind != TrapKind::None);
            ++traps;
        }
        if (card.tier == CardTier::Tier3) {
            ++titans;
            ++titansPerRole[static_cast<size_t>(card.role)];
        }
        perRole[static_cast<size_t>(card.role)] += card.deckCount;
    }

    CHECK(traps >= 6);
    CHECK(titans >= 6);

    // Every doctrine must be able to fill a primary core on its own, and must
    // have exactly one titan to build a deck around.
    for (int i = 0; i < kMechRoleCount; ++i) {
        const MechRole role = static_cast<MechRole>(i);
        std::string where = std::string(toString(role)) + " has only "
                          + std::to_string(perRole[static_cast<size_t>(i)]) + " copies";
        CHECK_MSG(perRole[static_cast<size_t>(i)] >= DeckRules::kMinPrimary + 2, where.c_str());
        CHECK(titansPerRole[static_cast<size_t>(i)] == 1);

        // The role helpers must round-trip through the JSON token.
        MechRole parsed = MechRole::Vanguard;
        CHECK(parseMechRole(toString(role), parsed));
        CHECK(parsed == role);
        CHECK(std::string(roleTitle(role)).size() > 0);
        CHECK(std::string(rolePassiveName(role)).size() > 0);
        CHECK(std::string(rolePassiveText(role)).size() > 0);
    }

    std::cout << "[PASS] test_catalogue_loads_and_is_coherent (" << cards.size() << " cards)\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << " DUEL RULES TESTS\n";
    std::cout << "========================================\n";
    Rng::seed(20260907);

    test_energy_curve_and_opening_hand();
    test_tribute_reduces_deploy_cost();
    test_frontline_screens_the_reactor();
    test_melee_must_clear_the_whole_board_for_the_reactor();
    test_ranged_shells_the_reactor_through_a_full_board();
    test_melee_reaches_one_lane_either_side();
    test_guard_screens_only_its_neighbours();
    test_ranged_reaches_support_and_dodges_return_fire();
    test_aerial_strikes_past_the_line_but_guard_still_holds();
    test_reactive_plating_and_return_fire();
    test_vanguard_passive_stacks_with_reactive_on_the_frontline_only();
    test_overkill_spills_into_the_reactor();
    test_plasma_ignores_absorption_plating();
    test_emp_shorts_out_the_target();
    test_splash_hits_the_flanking_frames();
    test_thruster_and_dragoon_passive_make_advancing_free();
    test_paladin_core_powers_plasma_strikes();
    test_plasma_ignores_reactive_plating();
    test_valkyrie_reclaims_the_first_frame_lost_each_turn();
    test_ranged_frames_are_soft();
    test_siege_fire_support_boosts_ranged_only();
    test_reactive_armor_protocol_blocks_and_crushes();
    test_firewall_blackout_negates_a_spell_and_burns_the_caster();
    test_overload_virus_punishes_a_big_deployment();
    test_grid_snare_weakens_an_advancing_frame();
    test_detonation_hits_the_killer();
    test_aura_applies_and_expires();
    test_duel_ends_when_a_reactor_falls();
    test_deck_rules_reject_illegal_configurations();
    test_every_pairing_builds_a_legal_deck();
    test_catalogue_loads_and_is_coherent();

    std::cout << "========================================\n";
    std::cout << " ALL DUEL RULES TESTS PASSED\n";
    std::cout << "========================================\n";
    return 0;
}
