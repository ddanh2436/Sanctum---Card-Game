#include "battle/DuelEngine.hpp"
#include "utils/Rng.hpp"
#include <algorithm>
#include <sstream>

const char* toString(ActionResult result) {
    switch (result) {
    case ActionResult::Ok:             return "ok";
    case ActionResult::NotYourTurn:    return "not your turn";
    case ActionResult::NoSuchCard:     return "no such card";
    case ActionResult::NotEnoughMana:  return "not enough energy";
    case ActionResult::RowFull:        return "that row is full";
    case ActionResult::TrapZoneFull:   return "counter zone is full";
    case ActionResult::NeedsTribute:   return "needs a tribute";
    case ActionResult::InvalidTribute: return "invalid tribute";
    case ActionResult::UnitCannotAct:  return "that frame cannot act";
    case ActionResult::IllegalTarget:  return "illegal target";
    case ActionResult::DuelOver:       return "the duel is over";
    }
    return "?";
}

// =============================================================================
// Setup and turn flow
// =============================================================================

void DuelEngine::startDuel(DuelistSetup player, DuelistSetup opponent) {
    m_board.clear();
    m_events.clear();
    m_over = false;
    m_turn = 0;
    m_nextInstanceId = 1;
    m_freeAdvanceUsed = { false, false };
    m_salvageUsed = { false, false };
    m_unitsLostThisTurn = { 0, 0 };
    m_fatigue = { 0, 0 };

    m_commanders[0] = Commander(player.name, player.primary, player.secondary, player.hp);
    m_commanders[1] = Commander(opponent.name, opponent.primary, opponent.secondary, opponent.hp);

    m_commanders[0].setDeck(std::move(player.deck));
    m_commanders[1].setDeck(std::move(opponent.deck));
    m_commanders[0].shuffleDeck();
    m_commanders[1].shuffleDeck();
    drawFor(Side::Player, Commander::kOpeningHand);
    drawFor(Side::Opponent, Commander::kOpeningHand);

    m_active = Side::Opponent;   // beginTurn flips, so the player moves first
    beginTurn(Side::Player);
}

int DuelEngine::drawFor(Side side, int count, const std::string& text) {
    Commander& me = m_commanders[index(side)];
    const std::size_t before = me.getHand().size();
    const int taken = me.draw(count);

    // Counted from the hand rather than from `taken`, because a draw can come
    // up short - an empty core, or a hand already at its limit - and the events
    // have to describe what actually arrived.
    const std::vector<CardData>& hand = me.getHand();
    for (std::size_t i = before; i < hand.size(); ++i) {
        emit(DuelEvent::Type::CardDrawn, side, i == before ? text : std::string(),
             -1, 0, -1, hand[i].id);
    }
    return taken;
}

void DuelEngine::beginTurn(Side side) {
    m_active = side;
    if (side == Side::Player) ++m_turn;

    Commander& me = m_commanders[index(side)];
    me.beginTurnMana();

    // Passive budgets refresh with the turn.
    m_freeAdvanceUsed[index(side)] = false;
    m_salvageUsed[index(side)] = false;
    m_unitsLostThisTurn[index(side)] = 0;

    emit(DuelEvent::Type::TurnStarted, side,
         (side == Side::Player ? "Your turn " : "Enemy turn ") + std::to_string(m_turn));

    // Paladin's Overcharge Core trickles in whether or not you can spend it.
    // Two, not one: at one per upkeep only a single plasma frame per turn ever
    // got its bonus, so a board of three lancers was no better fed than a board
    // of one and the doctrine's whole engine idled.
    if (me.getPrimaryRole() == MechRole::Paladin) {
        me.gainOvercharge(kOverchargePerUpkeep);
    }

    // Upkeep: plating lapses, frames spin up, stuns and EMP burn tick.
    for (Unit* unit : m_board.units(side)) {
        unit->armour = 0;
        unit->attacksThisTurn = 0;
        unit->exposed = false;
        if (unit->stunTurns > 0) {
            --unit->stunTurns;
        } else {
            unit->exhausted = false;
        }
        if (unit->shorted) {
            damageUnit(*unit, 1, true, nullptr, other(side));
        }
    }
    resolveDeaths(nullptr, other(side));

    // Draw step. An empty salvage line strains the reactor, and the strain
    // grows every turn it happens.
    //
    // Without this a duel between two grinding decks never ends: nothing forces
    // a conclusion, and a Valkyrie core recycling its wrecks back into the deck
    // will not even run out of cards. The simulator found duels still going at
    // forty turns with both reactors near full.
    if (drawFor(side, 1) > 0) {
    } else if (me.getDeck().empty()) {
        m_fatigue[index(side)] += kFatigueStep;
        damageCommander(side, m_fatigue[index(side)],
                        me.getName() + " runs dry: reactor strain "
                            + std::to_string(m_fatigue[index(side)]));
    }

    runTurnStartEffects(side);
    recomputeAuras();
    checkGameOver();
}

void DuelEngine::runTurnStartEffects(Side side) {
    for (Unit* unit : m_board.units(side)) {
        runAbilities(*unit, AbilityTrigger::OnTurnStart);
    }
}

void DuelEngine::runTurnEndEffects(Side side) {
    for (Unit* unit : m_board.units(side)) {
        runAbilities(*unit, AbilityTrigger::OnTurnEnd);
    }
    resolveDeaths(nullptr, other(side));
}

void DuelEngine::endTurn() {
    if (m_over) return;
    const Side finished = m_active;
    runTurnEndEffects(finished);
    m_commanders[index(finished)].clearTempMana();
    recomputeAuras();
    checkGameOver();
    if (m_over) return;
    beginTurn(other(finished));
}

// =============================================================================
// Commander passives
// =============================================================================

int DuelEngine::rangedBonus(Side side) const {
    // Siege's Fire Support, plus any Spotter Array aura on the board.
    return commander(side).getPrimaryRole() == MechRole::Siege ? 1 : 0;
}

int DuelEngine::platingOf(const Unit& unit) const {
    int plating = unit.hasKeyword(Keyword::Reactive) ? 1 : 0;

    // Artillery trades armour for reach. Without this, firing from the support
    // row was pure upside - no return fire, a Fire Support bonus, and nothing
    // melee could do about it - and Siege won ~95% of simulated pairings.
    if (unit.hasKeyword(Keyword::Ranged)) --plating;

    // Vanguard's Aegis Plating covers the frontline only - the support row is
    // meant to be soft, which is what makes Aerial and Ranged worth having.
    const UnitLocation where = m_board.locate(unit.instanceId);
    if (where.valid() && where.line == BoardLine::Frontline
        && commander(where.side).getPrimaryRole() == MechRole::Vanguard) {
        ++plating;
    }
    return plating;
}

bool DuelEngine::seesEnemyTraps(Side viewer) const {
    return commander(viewer).getPrimaryRole() == MechRole::Inquisitor;
}

int DuelEngine::trapCost(Side side) const {
    // Cold Read used to be information only, which left the Inquisitor playing
    // with no commander passive in practice - it was the weakest doctrine in
    // the sweep by a wide margin. Arming for free is the tempo half of it.
    return commander(side).getPrimaryRole() == MechRole::Inquisitor ? 0 : kSetTrapCost;
}

// =============================================================================
// Summoning
// =============================================================================

int DuelEngine::summonCost(const CardData& card, int tributeCount) const {
    const int discount = std::min(tributeCount, card.tributeRequirement()) * CardData::kTributeDiscount;
    return std::max(0, card.manaCost - discount);
}

std::unique_ptr<Unit> DuelEngine::makeUnit(const CardData& card, Side owner) {
    auto unit = std::make_unique<Unit>();
    unit->data = card;
    unit->owner = owner;
    unit->instanceId = m_nextInstanceId++;
    unit->exhausted = !card.hasKeyword(Keyword::Rush);

    for (const Ability& ability : card.abilities) {
        if (ability.kind == AbilityKind::ExtraAttackPerTurn) {
            unit->extraAttacks += std::max(1, ability.value);
        }
    }
    return unit;
}

ActionResult DuelEngine::summonFromHand(Side side, size_t handIndex, BoardLine line, int slot,
                                        const std::vector<int>& tributeIds) {
    if (m_over) return ActionResult::DuelOver;
    if (side != m_active) return ActionResult::NotYourTurn;

    Commander& me = m_commanders[index(side)];
    if (handIndex >= me.getHand().size()) return ActionResult::NoSuchCard;

    const CardData card = me.getHand()[handIndex];
    if (card.category != CardCategory::Unit) return ActionResult::NoSuchCard;

    // A titan may always be paid for outright if the energy is there; tributes
    // only buy the discount.
    const int tributes = static_cast<int>(tributeIds.size());
    if (tributes > card.tributeRequirement()) return ActionResult::InvalidTribute;

    for (int id : tributeIds) {
        const UnitLocation where = m_board.locate(id);
        if (!where.valid() || where.side != side) return ActionResult::InvalidTribute;
    }

    const int cost = summonCost(card, tributes);
    if (!me.canAfford(cost)) return ActionResult::NotEnoughMana;
    if (card.overchargeCost > me.getOvercharge()) return ActionResult::NotEnoughMana;

    if (slot < 0) slot = m_board.freeSlot(side, line);
    if (slot < 0) return ActionResult::RowFull;
    if (m_board.at(side, line, slot) != nullptr) return ActionResult::RowFull;

    // Pay: tributes are scrapped before the new frame lands.
    me.spendMana(cost);
    if (card.overchargeCost > 0) me.spendOvercharge(card.overchargeCost);
    for (int id : tributeIds) {
        const UnitLocation where = m_board.locate(id);
        if (!where.valid()) continue;
        std::unique_ptr<Unit> offered = m_board.take(where.side, where.line, where.slot);
        if (offered) {
            emit(DuelEvent::Type::UnitDestroyed, side,
                 offered->data.name + " is stripped for parts", offered->instanceId);
            me.sendToCrypt(offered->data);
        }
    }

    auto unit = makeUnit(card, side);
    const int newId = unit->instanceId;
    m_board.place(side, line, slot, std::move(unit));
    me.getHand().erase(me.getHand().begin() + static_cast<std::ptrdiff_t>(handIndex));

    emit(DuelEvent::Type::UnitSummoned, side, card.name + " deploys", newId, cost, -1, card.id);
    recomputeAuras();

    // The defender may answer a big deployment before it ever acts.
    if (card.tier != CardTier::Tier1) {
        Unit* summoned = m_board.findById(newId);
        fireTraps(other(side), TrapTrigger::OnEnemyHighTierDeploy, summoned);
    }

    Unit* summoned = m_board.findById(newId);
    if (summoned) runAbilities(*summoned, AbilityTrigger::OnDeploy);

    resolveDeaths(nullptr, side);
    recomputeAuras();
    checkGameOver();
    return ActionResult::Ok;
}

// =============================================================================
// Spells and traps
// =============================================================================

ActionResult DuelEngine::castSpell(Side side, size_t handIndex, int targetId) {
    if (m_over) return ActionResult::DuelOver;
    if (side != m_active) return ActionResult::NotYourTurn;

    Commander& me = m_commanders[index(side)];
    if (handIndex >= me.getHand().size()) return ActionResult::NoSuchCard;

    const CardData card = me.getHand()[handIndex];
    if (card.category != CardCategory::Spell) return ActionResult::NoSuchCard;
    if (!me.canAfford(card.manaCost)) return ActionResult::NotEnoughMana;

    if (card.spell == SpellKind::DestroyHighAttackEnemy) {
        const Unit* target = m_board.findById(targetId);
        if (!target || sideOf(*target) == side || target->attack() < card.spellValue) {
            return ActionResult::IllegalTarget;
        }
    }

    me.spendMana(card.manaCost);
    me.getHand().erase(me.getHand().begin() + static_cast<std::ptrdiff_t>(handIndex));
    emit(DuelEvent::Type::SpellCast, side, card.name, -1, card.manaCost, -1, card.id);

    // Firewall Blackout answers the cast itself, before anything resolves.
    int negated = 0;
    fireTraps(other(side), TrapTrigger::OnEnemySpellCast, nullptr, &negated);
    if (negated) {
        // The blackout burns the caster for what the operation cost them.
        damageCommander(side, card.manaCost,
                        card.name + " backfires for " + std::to_string(card.manaCost));
        me.sendToCrypt(card);
        resolveDeaths(nullptr, side);
        recomputeAuras();
        checkGameOver();
        return ActionResult::Ok;
    }

    // Thruster Evasion pulls a targeted frame out from under removal.
    if (card.spell == SpellKind::DestroyHighAttackEnemy) {
        Unit* target = m_board.findById(targetId);
        int recalled = 0;
        fireTraps(other(side), TrapTrigger::OnAllyTargetedByRemoval, target, &recalled);
        if (recalled) {
            me.sendToCrypt(card);
            resolveDeaths(nullptr, side);
            recomputeAuras();
            checkGameOver();
            return ActionResult::Ok;
        }
    }

    resolveSpell(side, card, targetId);
    me.sendToCrypt(card);

    resolveDeaths(nullptr, side);
    recomputeAuras();
    checkGameOver();
    return ActionResult::Ok;
}

void DuelEngine::resolveSpell(Side side, const CardData& card, int targetId) {
    Commander& me = m_commanders[index(side)];
    const Side foe = other(side);

    switch (card.spell) {
    case SpellKind::ArmourAllAllies: {
        for (Unit* unit : m_board.units(side)) unit->armour += card.spellValue;
        log("Every frame gains " + std::to_string(card.spellValue) + " plating");
        break;
    }
    case SpellKind::DestroyHighAttackEnemy: {
        Unit* target = m_board.findById(targetId);
        if (target && !target->wardOff) {
            target->damage = target->maxHealth();
            emit(DuelEvent::Type::UnitDamaged, side, target->data.name + " is scrapped",
                 target->instanceId, target->maxHealth());
        } else if (target) {
            log(target->data.name + " is hardened against removal");
        }
        break;
    }
    case SpellKind::WipeLowHealthUnits: {
        // Orbital Bombardment does not care whose frames are on the ground.
        int wiped = 0;
        for (Unit* unit : m_board.allUnits()) {
            if (unit->wardOff || unit->health() > card.spellValue) continue;
            unit->damage = unit->maxHealth();
            ++wiped;
        }
        log("The bombardment scraps " + std::to_string(wiped) + " frame(s)");
        break;
    }
    case SpellKind::DrawThenRepairIfLosses: {
        drawFor(side, card.spellValue);
        if (m_unitsLostThisTurn[index(side)] > 0 && card.spellValue2 > 0) {
            me.heal(card.spellValue2);
            emit(DuelEvent::Type::CommanderHealed, side,
                 "Salvage repairs the reactor for " + std::to_string(card.spellValue2),
                 -1, card.spellValue2);
        }
        break;
    }
    case SpellKind::OverchargeSurge: {
        me.gainOvercharge(card.spellValue);
        log("The core charges to " + std::to_string(me.getOvercharge()));
        break;
    }
    case SpellKind::DamageEnemyFrontline: {
        for (Unit* unit : m_board.unitsIn(foe, BoardLine::Frontline)) {
            damageUnit(*unit, card.spellValue, true, nullptr, side);
        }
        log("The enemy frontline is raked for " + std::to_string(card.spellValue));
        break;
    }
    case SpellKind::None:
        break;
    }
}

ActionResult DuelEngine::setTrap(Side side, size_t handIndex) {
    if (m_over) return ActionResult::DuelOver;
    if (side != m_active) return ActionResult::NotYourTurn;

    Commander& me = m_commanders[index(side)];
    if (handIndex >= me.getHand().size()) return ActionResult::NoSuchCard;

    const CardData card = me.getHand()[handIndex];
    if (card.category != CardCategory::Trap) return ActionResult::NoSuchCard;
    if (m_board.trapZoneFull(side)) return ActionResult::TrapZoneFull;

    const int cost = trapCost(side);
    if (!me.canAfford(cost)) return ActionResult::NotEnoughMana;

    me.spendMana(cost);
    me.getHand().erase(me.getHand().begin() + static_cast<std::ptrdiff_t>(handIndex));

    TrapCard trap;
    trap.data = card;
    trap.faceDown = true;
    trap.instanceId = m_nextInstanceId++;
    m_board.traps(side).push_back(trap);

    // The card's identity stays hidden: the log only records that something was set.
    // Only the owner learns which card it was; the log line stays generic.
    emit(DuelEvent::Type::TrapSet, side, "A counter-protocol is armed", trap.instanceId,
         0, -1, card.id);
    return ActionResult::Ok;
}

// =============================================================================
// Movement
// =============================================================================

int DuelEngine::advanceCost(int unitId) const {
    const Unit* unit = m_board.findById(unitId);
    if (!unit) return kMoveCost;
    if (unit->hasKeyword(Keyword::Thruster)) return 0;

    // Dragoon's Thruster Assault: the first advance each turn is on the house.
    const UnitLocation where = m_board.locate(unitId);
    if (where.valid()
        && commander(where.side).getPrimaryRole() == MechRole::Dragoon
        && !m_freeAdvanceUsed[index(where.side)]) {
        return 0;
    }
    return kMoveCost;
}

bool DuelEngine::canAdvance(int unitId) const {
    const UnitLocation where = m_board.locate(unitId);
    if (!where.valid() || where.line != BoardLine::Support) return false;
    const Unit* unit = m_board.findById(unitId);
    if (!unit || unit->stunTurns > 0) return false;
    return m_board.hasRoom(where.side, BoardLine::Frontline);
}

ActionResult DuelEngine::advanceUnit(Side side, int unitId) {
    if (m_over) return ActionResult::DuelOver;
    if (side != m_active) return ActionResult::NotYourTurn;

    const UnitLocation where = m_board.locate(unitId);
    if (!where.valid() || where.side != side) return ActionResult::IllegalTarget;
    if (where.line != BoardLine::Support) return ActionResult::IllegalTarget;

    Unit* unit = m_board.findById(unitId);
    if (!unit || unit->stunTurns > 0) return ActionResult::UnitCannotAct;

    const int slot = m_board.freeSlot(side, BoardLine::Frontline);
    if (slot < 0) return ActionResult::RowFull;

    Commander& me = m_commanders[index(side)];
    const int cost = advanceCost(unitId);
    if (!me.canAfford(cost)) return ActionResult::NotEnoughMana;
    me.spendMana(cost);

    // Only the Dragoon passive is a limited budget; Thruster is unconditional.
    if (cost == 0 && !unit->hasKeyword(Keyword::Thruster)) {
        m_freeAdvanceUsed[index(side)] = true;
    }

    std::unique_ptr<Unit> moving = m_board.take(side, BoardLine::Support, where.slot);
    m_board.place(side, BoardLine::Frontline, slot, std::move(moving));

    emit(DuelEvent::Type::UnitAdvanced, side,
         m_board.findById(unitId)->data.name + " pushes to the frontline", unitId);

    // Stepping into the open is exactly when an ambush springs.
    fireTraps(other(side), TrapTrigger::OnEnemyAdvance, m_board.findById(unitId));

    resolveDeaths(nullptr, side);
    recomputeAuras();
    checkGameOver();
    return ActionResult::Ok;
}

// =============================================================================
// Combat
// =============================================================================

/// True when a side's lane is empty in both rows - the corridor a ranged frame
/// needs before it can range on the reactor behind it.
bool DuelEngine::laneIsOpen(Side side, int slot) const {
    if (slot < 0 || slot >= Board::kLineSlots) return false;
    return m_board.at(side, BoardLine::Frontline, slot) == nullptr
        && m_board.at(side, BoardLine::Support, slot) == nullptr;
}

const Unit* DuelEngine::interceptorOver(Side defender, int lane) const {
    for (int slot = 0; slot < Board::kLineSlots; ++slot) {
        if (std::abs(slot - lane) > kInterceptReach) continue;
        const Unit* unit = m_board.at(defender, BoardLine::Frontline, slot);
        if (unit && unit->isAlive() && unit->hasKeyword(Keyword::Intercept)) return unit;
    }
    return nullptr;
}

Unit* DuelEngine::interceptorFor(Side defender, int lane) {
    // Frontline only: an interceptor in the support row is behind the thing it
    // is meant to be shooting at.
    for (int slot = 0; slot < Board::kLineSlots; ++slot) {
        if (std::abs(slot - lane) > kInterceptReach) continue;
        Unit* unit = m_board.at(defender, BoardLine::Frontline, slot);
        if (unit && unit->isAlive() && unit->hasKeyword(Keyword::Intercept)) return unit;
    }
    return nullptr;
}

/// True when a living Guard stands immediately left or right of this frame in
/// its own row. A Guard screens its neighbours, not the whole board.
bool DuelEngine::isScreened(const UnitLocation& where) const {
    for (int offset : { -1, 1 }) {
        const int slot = where.slot + offset;
        if (slot < 0 || slot >= Board::kLineSlots) continue;
        const Unit* neighbour = m_board.at(where.side, where.line, slot);
        if (neighbour && neighbour->isAlive() && neighbour->hasKeyword(Keyword::Taunt)) {
            return true;
        }
    }
    return false;
}

std::vector<int> DuelEngine::legalTargets(int attackerId) const {
    std::vector<int> targets;
    const Unit* attacker = m_board.findById(attackerId);
    const UnitLocation where = m_board.locate(attackerId);
    if (!attacker || !where.valid() || !attacker->canAct()) return targets;

    const Side foe = other(where.side);
    const bool ranged = attacker->hasKeyword(Keyword::Ranged);
    const bool aerial = attacker->hasKeyword(Keyword::Aerial);
    const bool melee = !ranged && !aerial;

    // A grounded melee frame must stand in the frontline to swing at all.
    if (melee && where.line != BoardLine::Frontline) return targets;

    // Collect what this frame can physically reach, before Guard is applied.
    //
    // Melee has a reach of one lane either side: it fights what is in front of
    // it, not whatever it likes anywhere on the board. Ranged and Aerial see
    // the whole width.
    auto inReach = [&](int slot) {
        return !melee || std::abs(slot - where.slot) <= kMeleeReach;
    };

    std::vector<const Unit*> candidates;
    auto gather = [&](BoardLine line) {
        for (int slot = 0; slot < Board::kLineSlots; ++slot) {
            if (!inReach(slot)) continue;
            if (const Unit* unit = m_board.at(foe, line, slot)) candidates.push_back(unit);
        }
    };

    if (aerial) {
        // Flies the line: both rows are reachable at once.
        gather(BoardLine::Frontline);
        gather(BoardLine::Support);
    } else if (ranged) {
        // Fires OVER the frontline but not PAST it - the support row stays out
        // of reach while the enemy still has a line, except for anything that
        // gave away its position by shooting from the back last round.
        gather(BoardLine::Frontline);
        const bool screenStanding = !m_board.unitsIn(foe, BoardLine::Frontline).empty();
        for (int slot = 0; slot < Board::kLineSlots; ++slot) {
            const Unit* unit = m_board.at(foe, BoardLine::Support, slot);
            if (unit && (!screenStanding || unit->exposed)) candidates.push_back(unit);
        }
    } else {
        // Melee works its own lane: whatever is in front of it first, and only
        // once that lane is clear does it reach the support row behind.
        gather(BoardLine::Frontline);
        if (candidates.empty()) gather(BoardLine::Support);
    }

    // Guard screens the frames beside it. A Guard is always attackable itself,
    // which is what stops a wall of them from locking the board solid.
    for (const Unit* unit : candidates) {
        const UnitLocation at = m_board.locate(unit->instanceId);
        if (!at.valid()) continue;
        if (unit->hasKeyword(Keyword::Taunt) || !isScreened(at)) {
            targets.push_back(unit->instanceId);
        }
    }

    // The reactor.
    //
    // Artillery can always range on the reactor; how hard it lands is what the
    // board decides. See declareAttack: a clear corridor is full damage, a
    // blocked one is half.
    //
    // Both purer rules failed measurement. Flat half damage everywhere was
    // arithmetic that read as nothing. Requiring a clear lane made artillery
    // binary instead - against a Guard-heavy deck that simply plugs every lane
    // it did nothing at all, and the Siege commander became a free win at 98%.
    // Position as a bonus rather than as permission keeps the decision without
    // giving either side an off switch.
    if (ranged) {
        targets.push_back(-1);
    } else if (aerial) {
        // Aerial flies over the line rather than shooting through it, so it
        // needs no corridor - it just needs somewhere to land its strike.
        targets.push_back(-1);
    } else if (m_board.sideEmpty(foe)) {
        // Melee has to earn it outright: clear the board, and be standing in
        // the frontline with nothing left between it and the enemy core.
        targets.push_back(-1);
    }
    return targets;
}

ActionResult DuelEngine::declareAttack(Side side, int attackerId, int targetId) {
    if (m_over) return ActionResult::DuelOver;
    if (side != m_active) return ActionResult::NotYourTurn;

    Unit* attacker = m_board.findById(attackerId);
    if (!attacker || sideOf(*attacker) != side) return ActionResult::IllegalTarget;
    if (!attacker->canAct()) return ActionResult::UnitCannotAct;

    const std::vector<int> legal = legalTargets(attackerId);
    if (std::find(legal.begin(), legal.end(), targetId) == legal.end()) {
        return ActionResult::IllegalTarget;
    }

    const Side foe = other(side);

    // Overcharge Core: a plasma strike draws on the core for extra bite. It is
    // automatic because a prompt on every attack would be noise - the badge on
    // the commander plate is where the player watches it drain.
    int overchargeBonus = 0;
    if (attacker->hasKeyword(Keyword::Plasma)
        && m_commanders[index(side)].spendOvercharge(kOverchargePerStrike) > 0) {
        overchargeBonus = kOverchargeStrikeBonus;
        log(attacker->data.name + " draws " + std::to_string(kOverchargePerStrike)
            + " from the core (+" + std::to_string(kOverchargeStrikeBonus) + ")");
    }

    ++attacker->attacksThisTurn;
    if (attacker->attacksThisTurn >= attacker->attacksAllowed()) {
        attacker->exhausted = true;
    }

    // Firing from the back gives away the position until the next upkeep.
    const UnitLocation firingFrom = m_board.locate(attackerId);
    if (firingFrom.valid() && firingFrom.line == BoardLine::Support) {
        attacker->exposed = true;
    }

    emit(DuelEvent::Type::AttackDeclared, side, "", attackerId, attacker->attack(), targetId);

    // ---- flak: Intercept fires before the strike lands ----
    //
    // Aerial was the one keyword with no answer on the board. It ignores the
    // lane rules AND Guard, so the only counterplay was to kill the flier on
    // your own turn - which is not counterplay, it is a race. An Intercept
    // frame in the defending frontline covers its own lane and one either side
    // and shoots first.
    //
    // The lane it defends is the TARGET's lane, not the attacker's: a flier
    // crossing the board is engaged where it arrives, which is the lane the
    // defender actually chose to cover.
    if (attacker->hasKeyword(Keyword::Aerial)) {
        int lane = -1;
        if (targetId >= 0) {
            const UnitLocation at = m_board.locate(targetId);
            if (at.valid()) lane = at.slot;
        } else {
            const UnitLocation from = m_board.locate(attackerId);
            if (from.valid()) lane = from.slot;   // a reactor run flies its own lane
        }

        if (Unit* flak = (lane >= 0 ? interceptorFor(foe, lane) : nullptr)) {
            const int bite = flak->attack();
            if (bite > 0) {
                log(flak->data.name + " intercepts " + attacker->data.name);
                damageUnit(*attacker, bite, false, flak, foe);

                // A flier shot out of the sky never lands its strike.
                attacker = m_board.findById(attackerId);
                if (!attacker || !attacker->isAlive()) {
                    resolveDeaths(nullptr, side);
                    recomputeAuras();
                    checkGameOver();
                    return ActionResult::Ok;
                }
            }
        }
    }

    // ---- attack on the reactor: the classic counter-protocol window ----
    if (targetId < 0) {
        int negated = 0;
        fireTraps(foe, TrapTrigger::OnEnemyAttackReactor, attacker, &negated);
        if (negated) {
            resolveDeaths(nullptr, side);
            recomputeAuras();
            checkGameOver();
            return ActionResult::Ok;
        }
        // The attacker may have been scrapped by the counter.
        attacker = m_board.findById(attackerId);
        if (!attacker || !attacker->isAlive()) {
            resolveDeaths(nullptr, side);
            checkGameOver();
            return ActionResult::Ok;
        }
        int reactorDamage = attacker->attack() + overchargeBonus;
        if (attacker->hasKeyword(Keyword::Ranged)) {
            reactorDamage += rangedBonus(side) + attacker->auraRangedBonus;
            // A clear corridor to the core is worth double. Shooting through a
            // contested lane still lands, but only for half.
            const UnitLocation from = m_board.locate(attackerId);
            if (!from.valid() || !laneIsOpen(foe, from.slot)) {
                reactorDamage = std::max(1, (reactorDamage + 1) / 2);
            }
        }
        damageCommander(foe, reactorDamage, attacker->data.name + " strikes the reactor");
        resolveDeaths(nullptr, side);
        recomputeAuras();
        checkGameOver();
        return ActionResult::Ok;
    }

    // ---- frame versus frame ----
    Unit* defender = m_board.findById(targetId);
    if (!defender) return ActionResult::IllegalTarget;

    const bool ranged = attacker->hasKeyword(Keyword::Ranged);
    int damage = attacker->attack() + overchargeBonus;
    if (ranged) damage += rangedBonus(side) + attacker->auraRangedBonus;

    const int defenderHealthBefore = defender->health();
    // Artillery has no melee answer: it never returns fire, attacking or
    // defending. Without this it was strictly better than a melee frame on
    // both sides of every trade.
    const int retaliation = defender->hasKeyword(Keyword::Ranged) ? 0 : defender->attack();
    const int defenderId = defender->instanceId;
    const UnitLocation defenderAt = m_board.locate(defenderId);

    damageUnit(*defender, damage, attacker->hasKeyword(Keyword::Plasma), attacker, side);

    // Splash rolls into the slots either side of the target.
    if (attacker->hasKeyword(Keyword::Splash) && defenderAt.valid()) {
        splashNeighbours(defenderAt, damage, side);
    }

    // EMP shorts out whatever it touches.
    if (attacker->hasKeyword(Keyword::EMP)) {
        Unit* stillThere = m_board.findById(defenderId);
        if (stillThere && stillThere->isAlive()) {
            stillThere->stunTurns = std::max(stillThere->stunTurns, 1);
            stillThere->shorted = true;
            log(stillThere->data.name + " is shorted out");
        }
    }

    // Overkill: the excess carries through into the reactor.
    if (attacker->hasKeyword(Keyword::Overkill) && damage > defenderHealthBefore) {
        const int spill = damage - defenderHealthBefore;
        damageCommander(foe, spill,
                        attacker->data.name + " punches through for " + std::to_string(spill));
    }

    // Return fire, unless the attacker struck from range or from the air.
    // Combat damage is simultaneous: a defender that dies still swings back, so
    // trading into a bigger chassis is a real risk rather than free removal.
    attacker = m_board.findById(attackerId);
    if (attacker && attacker->isAlive() && retaliation > 0
        && !ranged && !attacker->hasKeyword(Keyword::Aerial)) {
        damageUnit(*attacker, retaliation, false, defender, foe);
    }

    resolveDeaths(m_board.findById(attackerId), side);
    recomputeAuras();
    checkGameOver();
    return ActionResult::Ok;
}

void DuelEngine::splashNeighbours(const UnitLocation& centre, int amount, Side attackerSide) {
    const int collateral = std::max(1, amount / 2);
    for (int offset : { -1, 1 }) {
        const int slot = centre.slot + offset;
        if (slot < 0 || slot >= Board::kLineSlots) continue;
        Unit* neighbour = m_board.at(centre.side, centre.line, slot);
        if (neighbour) damageUnit(*neighbour, collateral, false, nullptr, attackerSide);
    }
}

// =============================================================================
// Damage, repair, destruction
// =============================================================================

void DuelEngine::damageUnit(Unit& unit, int amount, bool plasma, Unit* source, Side attackerSide) {
    if (amount <= 0 || !unit.isAlive()) return;

    int remaining = amount;

    // Reactive plating blunts hits from other frames, not raw ordnance - and
    // not plasma, which is the whole point of the keyword. Plasma used to
    // bypass only `armour`, the temporary absorption a spell grants, so the
    // doctrine built to melt armour lost to the doctrine named Aegis Plating.
    //
    // The floor of 1 is load-bearing. Reactive stacks with Vanguard's Aegis for
    // -2 per hit, and most Vanguard frames swing for 1-3: without a floor two
    // armoured boards deal literally nothing to each other and the duel stalls
    // out at full health until the turn limit. Plating should blunt a hit, not
    // grant immunity to it.
    if (source != nullptr && !plasma) {
        remaining = std::max(1, remaining - platingOf(unit));
    }
    // Plasma burns straight through absorption plating too.
    if (!plasma && unit.armour > 0) {
        const int absorbed = std::min(unit.armour, remaining);
        unit.armour -= absorbed;
        remaining -= absorbed;
    }
    if (remaining <= 0) return;

    unit.damage += remaining;
    emit(DuelEvent::Type::UnitDamaged, attackerSide, "", unit.instanceId, remaining);

    if (unit.isAlive()) {
        runAbilities(unit, AbilityTrigger::OnDamaged, source);
    }
}

void DuelEngine::healUnit(Unit& unit, int amount) {
    if (amount <= 0 || !unit.isAlive()) return;
    const int healed = std::min(amount, unit.damage);
    if (healed <= 0) return;
    unit.damage -= healed;
    emit(DuelEvent::Type::UnitHealed, sideOf(unit), "", unit.instanceId, healed);
}

void DuelEngine::damageCommander(Side side, int amount, const std::string& reason) {
    if (amount <= 0) return;
    const int dealt = m_commanders[index(side)].takeDamage(amount);
    emit(DuelEvent::Type::CommanderDamaged, side, reason, -1, dealt);
}

void DuelEngine::resolveDeaths(Unit* killer, Side killerSide) {
    // Loop: a detonation can scrap something else.
    for (int guard = 0; guard < 8; ++guard) {
        std::vector<Unit> dead = m_board.collectDead();
        if (dead.empty()) return;

        for (Unit& corpse : dead) {
            const Side owner = corpse.owner;
            emit(DuelEvent::Type::UnitDestroyed, owner, corpse.data.name + " is destroyed",
                 corpse.instanceId);
            ++m_unitsLostThisTurn[index(owner)];

            // Valkyrie's Nanite Reclamation: the first frame lost each turn is
            // rebuilt into the deck instead of being written off as scrap.
            //
            // It recycles into the DECK, not the hand. Straight to hand gave the
            // doctrine an endless supply at no tempo cost and it won ~100% of
            // simulated duels on every splash; going through the deck keeps the
            // fantasy but makes the value slow enough to play around.
            Commander& ownerCmd = m_commanders[index(owner)];
            if (ownerCmd.getPrimaryRole() == MechRole::Valkyrie
                && !m_salvageUsed[index(owner)]) {
                m_salvageUsed[index(owner)] = true;
                ownerCmd.getDeck().push_back(corpse.data);
                log(corpse.data.name + " is rebuilt into the salvage line");
            } else {
                ownerCmd.sendToCrypt(corpse.data);
            }

            // Detonations
            for (const Ability& ability : corpse.data.abilities) {
                if (ability.trigger != AbilityTrigger::OnDestroyed) continue;
                if (ability.kind == AbilityKind::DamageKiller && killer) {
                    Unit* stillAlive = m_board.findById(killer->instanceId);
                    if (stillAlive) {
                        damageUnit(*stillAlive, ability.value, true, nullptr, owner);
                        log(corpse.data.name + " detonates for " + std::to_string(ability.value));
                    }
                }
            }

            // Freyja: a wreck leaves a scrap drone standing in the gap.
            for (Unit* ally : m_board.units(owner)) {
                bool spawns = false;
                int size = 1;
                for (const Ability& ability : ally->data.abilities) {
                    if (ability.kind == AbilityKind::SpawnScrapDroneOnAllyLoss) {
                        spawns = true;
                        size = std::max(1, ability.value);
                    }
                }
                if (!spawns) continue;

                const int slot = m_board.freeSlot(owner, BoardLine::Support);
                if (slot < 0) break;

                CardData drone;
                drone.id = "scrap_drone";
                drone.name = "Scrap Drone";
                drone.role = ally->data.role;
                drone.category = CardCategory::Unit;
                drone.attack = size;
                drone.health = size;
                drone.description = "Welded together from the wreck.";

                auto spawned = makeUnit(drone, owner);
                const int droneId = spawned->instanceId;
                m_board.place(owner, BoardLine::Support, slot, std::move(spawned));
                emit(DuelEvent::Type::UnitSummoned, owner,
                     "A scrap drone rises from the wreck", droneId);
                break;   // one drone per loss
            }

            // A fallen titan can take the board with it.
            if (corpse.data.tier == CardTier::Tier3) {
                fireTraps(owner, TrapTrigger::OnOwnTitanDestroyed, &corpse);
            }

            // The killer feeds on the kill.
            if (killer) {
                Unit* stillAlive = m_board.findById(killer->instanceId);
                if (stillAlive && sideOf(*stillAlive) == killerSide) {
                    runAbilities(*stillAlive, AbilityTrigger::OnKill, &corpse);
                }
            }
        }
        recomputeAuras();
    }
}

Side DuelEngine::sideOf(const Unit& unit) const {
    // The board is authoritative while the unit is on it; `owner` covers
    // corpses and units in flight between rows.
    const UnitLocation where = m_board.locate(unit.instanceId);
    return where.valid() ? where.side : unit.owner;
}

void DuelEngine::checkGameOver() {
    if (m_over) return;
    const bool playerDead = !m_commanders[0].isAlive();
    const bool enemyDead = !m_commanders[1].isAlive();
    if (!playerDead && !enemyDead) return;

    m_over = true;
    m_winner = enemyDead ? Side::Player : Side::Opponent;
    emit(DuelEvent::Type::DuelEnded, m_winner,
         m_winner == Side::Player ? "Enemy reactor critical" : "Your reactor is breached");
}

// =============================================================================
// Abilities and auras
// =============================================================================

void DuelEngine::runAbilities(Unit& unit, AbilityTrigger trigger, Unit* other) {
    const Side side = sideOf(unit);
    Commander& me = m_commanders[index(side)];
    const Side foe = ::other(side);

    for (const Ability& ability : unit.data.abilities) {
        if (ability.trigger != trigger) continue;

        switch (ability.kind) {
        case AbilityKind::RepairWoundedAlly: {
            Unit* worst = nullptr;
            for (Unit* ally : m_board.units(side)) {
                if (ally->damage <= 0) continue;
                if (!worst || ally->damage > worst->damage) worst = ally;
            }
            if (worst) {
                healUnit(*worst, ability.value);
                log(unit.data.name + " repairs " + worst->data.name +
                    " for " + std::to_string(ability.value));
            }
            break;
        }
        case AbilityKind::RepairSelfEachTurn: {
            healUnit(unit, ability.value);
            break;
        }
        case AbilityKind::DamageEnemyFrontline: {
            for (Unit* enemy : m_board.unitsIn(foe, BoardLine::Frontline)) {
                damageUnit(*enemy, ability.value, true, nullptr, side);
            }
            log(unit.data.name + " rakes the enemy frontline for " + std::to_string(ability.value));
            break;
        }
        case AbilityKind::DamageEnemyBackline: {
            const auto backline = m_board.unitsIn(foe, BoardLine::Support);
            if (backline.empty()) break;
            const int each = std::max(1, ability.value / static_cast<int>(backline.size()));
            for (Unit* enemy : backline) {
                damageUnit(*enemy, each, true, nullptr, side);
            }
            log(unit.data.name + " shells the support row for " + std::to_string(each) + " each");
            break;
        }
        case AbilityKind::DrawCards: {
            drawFor(side, ability.value);
            break;
        }
        case AbilityKind::RepairReactor: {
            me.heal(ability.value);
            emit(DuelEvent::Type::CommanderHealed, side, "", -1, ability.value);
            break;
        }
        case AbilityKind::GainOvercharge: {
            me.gainOvercharge(ability.value);
            log(unit.data.name + " charges the core by " + std::to_string(ability.value));
            break;
        }
        case AbilityKind::DumpOverchargeOnDeploy: {
            const int held = me.dumpOvercharge();
            const auto enemies = m_board.units(foe);
            if (held <= 0 || enemies.empty()) break;
            const int each = std::max(1, held / static_cast<int>(enemies.size()));
            for (Unit* enemy : enemies) {
                damageUnit(*enemy, each, true, nullptr, side);
            }
            log(unit.data.name + " vents " + std::to_string(held) +
                " overcharge across the enemy board");
            break;
        }
        case AbilityKind::GainStatsOnKill: {
            unit.permAttack += ability.value;
            healUnit(unit, ability.value2);
            log(unit.data.name + " salvages the kill: +" + std::to_string(ability.value) + " attack");
            break;
        }
        case AbilityKind::RetreatAfterKill: {
            const UnitLocation home = m_board.locate(unit.instanceId);
            const int slot = m_board.freeSlot(side, BoardLine::Support);
            if (home.valid() && home.line == BoardLine::Frontline && slot >= 0) {
                std::unique_ptr<Unit> falling = m_board.take(side, home.line, home.slot);
                m_board.place(side, BoardLine::Support, slot, std::move(falling));
                log(unit.data.name + " falls back out of reach");
            }
            break;
        }
        case AbilityKind::ReviveBestFromScrap: {
            auto& crypt = me.getCrypt();
            auto best = crypt.end();
            for (auto it = crypt.begin(); it != crypt.end(); ++it) {
                if (it->category != CardCategory::Unit) continue;
                if (best == crypt.end() || it->manaCost > best->manaCost) best = it;
            }
            const int slot = m_board.freeSlot(side, BoardLine::Support);
            if (best != crypt.end() && slot >= 0) {
                CardData revived = *best;
                crypt.erase(best);
                auto raised = makeUnit(revived, side);
                const int newId = raised->instanceId;
                m_board.place(side, BoardLine::Support, slot, std::move(raised));
                emit(DuelEvent::Type::UnitSummoned, side,
                     revived.name + " is rebuilt from the scrap yard", newId);
            }
            break;
        }
        case AbilityKind::RevealEnemyTrap: {
            auto& traps = m_board.traps(foe);
            auto it = std::find_if(traps.begin(), traps.end(),
                                   [](const TrapCard& t) { return t.faceDown; });
            if (it != traps.end()) {
                it->faceDown = false;
                emit(DuelEvent::Type::TrapFlipped, foe,
                     unit.data.name + " decrypts " + it->data.name, it->instanceId);
            }
            break;
        }
        case AbilityKind::ReflectToRow: {
            if (!other) break;
            const UnitLocation attackerAt = m_board.locate(other->instanceId);
            if (!attackerAt.valid()) break;
            for (Unit* enemy : m_board.unitsIn(attackerAt.side, BoardLine::Frontline)) {
                damageUnit(*enemy, ability.value, true, nullptr, side);
            }
            log(unit.data.name + " arcs " + std::to_string(ability.value) +
                " back across the enemy line");
            break;
        }
        // Continuous effects are applied in recomputeAuras(), not here.
        case AbilityKind::AuraBuffFrontline:
        case AbilityKind::AuraArmourFrontline:
        case AbilityKind::AuraRangedBonus:
        case AbilityKind::SpawnScrapDroneOnAllyLoss:
        case AbilityKind::SeizeUnitOnTrapFlip:
        case AbilityKind::ExtraAttackPerTurn:
        case AbilityKind::SpendOverchargeToStrike:
        case AbilityKind::DamageKiller:
        case AbilityKind::None:
            break;
        }
    }
}

void DuelEngine::recomputeAuras() {
    // Wipe aura contributions, then rebuild them from every living frame. Doing
    // it from scratch avoids the classic bug where a dead buffer leaves its
    // bonus behind on the frames it was pumping.
    for (Unit* unit : m_board.allUnits()) {
        unit->auraAttack = 0;
        unit->auraHealth = 0;
        unit->auraRangedBonus = 0;
        unit->wardOff = false;
    }

    for (Side side : { Side::Player, Side::Opponent }) {
        const auto allies = m_board.units(side);

        for (Unit* source : allies) {
            for (const Ability& ability : source->data.abilities) {
                if (ability.trigger != AbilityTrigger::Aura) continue;

                switch (ability.kind) {
                case AbilityKind::AuraBuffFrontline:
                    for (Unit* ally : m_board.unitsIn(side, BoardLine::Frontline)) {
                        if (ally == source) continue;
                        ally->auraAttack += ability.value;
                        ally->auraHealth += ability.value2;
                    }
                    break;
                case AbilityKind::AuraArmourFrontline:
                    for (Unit* ally : m_board.unitsIn(side, BoardLine::Frontline)) {
                        ally->armour = std::max(ally->armour, ability.value);
                    }
                    break;
                case AbilityKind::BuffPerArmedCounter: {
                    // A counter-protocol used to be worth nothing until the
                    // moment it fired, and most never did - so the whole zone
                    // was a gamble the Inquisitor kept losing. Armed counters
                    // now push the frames that are standing guard over them,
                    // which pays whether or not the window ever opens.
                    const int armed = static_cast<int>(m_board.traps(side).size());
                    source->auraAttack += ability.value * armed;
                    break;
                }
                case AbilityKind::AuraRangedBonus:
                    for (Unit* ally : allies) {
                        if (ally->hasKeyword(Keyword::Ranged)) {
                            ally->auraRangedBonus += ability.value;
                        }
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
}

// =============================================================================
// Counter-protocols
// =============================================================================

bool DuelEngine::fireTraps(Side defender, TrapTrigger trigger, Unit* actor, int* outNegated) {
    auto& traps = m_board.traps(defender);
    for (size_t i = 0; i < traps.size(); ++i) {
        if (traps[i].data.trapTrigger != trigger) continue;

        TrapCard trap = traps[i];
        traps.erase(traps.begin() + static_cast<std::ptrdiff_t>(i));

        emit(DuelEvent::Type::TrapFlipped, defender, trap.data.name + " fires!",
             trap.instanceId, 0, actor ? actor->instanceId : -1, trap.data.id);
        resolveTrap(trap, defender, actor, outNegated);
        m_commanders[index(defender)].sendToCrypt(trap.data);

        // Deus Ex-Machina hijacks a frame every time one of our counters fires.
        seizeEnemyUnit(defender);
        // Cipher Tribunal doctrine: every counter that resolves feeds the core.
        applyTribunalPayoff(defender);

        resolveDeaths(nullptr, defender);
        return true;   // one counter per window keeps chains readable
    }
    return false;
}

void DuelEngine::resolveTrap(TrapCard& trap, Side owner, Unit* actor, int* outNegated) {
    const CardData& card = trap.data;
    const Side foe = other(owner);

    switch (card.trapKind) {
    case TrapKind::BlockAndCrushWeak: {
        if (outNegated) *outNegated = 1;
        if (actor && actor->health() <= card.trapValue) {
            actor->damage = actor->maxHealth();
            log(actor->data.name + " is crushed against the reactor shield");
        } else if (actor) {
            log("The strike is blocked");
        }
        break;
    }
    case TrapKind::NegateSpellAndBurn: {
        // The burn itself is applied by castSpell, which knows the energy cost.
        if (outNegated) *outNegated = 1;
        log("The operation is firewalled");
        break;
    }
    case TrapKind::OverloadSummon: {
        if (actor) {
            const int surge = std::max(1, actor->attack() / 2);
            damageUnit(*actor, surge, true, nullptr, owner);
            log(actor->data.name + " overloads for " + std::to_string(surge));
        }
        break;
    }
    case TrapKind::WeakenAdvancingUnit: {
        if (actor) {
            actor->permAttack -= card.trapValue;
            actor->permHealth -= card.trapValue2;
            log(actor->data.name + " is snared (-" + std::to_string(card.trapValue) +
                "/-" + std::to_string(card.trapValue2) + ")");
        }
        break;
    }
    case TrapKind::RecallTargetToHand: {
        if (outNegated) *outNegated = 1;
        if (actor) {
            const UnitLocation where = m_board.locate(actor->instanceId);
            Commander& ownerCmd = m_commanders[index(owner)];
            if (where.valid()
                && static_cast<int>(ownerCmd.getHand().size()) < Commander::kHandLimit) {
                std::unique_ptr<Unit> pulled = m_board.take(where.side, where.line, where.slot);
                if (pulled) {
                    ownerCmd.getHand().push_back(pulled->data);
                    log(pulled->data.name + " burns its thrusters and disengages");
                }
            }
        }
        break;
    }
    case TrapKind::DetonateForTitanAttack: {
        if (actor) {
            const auto enemies = m_board.units(foe);
            if (!enemies.empty()) {
                const int total = actor->data.attack + actor->permAttack;
                const int each = std::max(1, total / static_cast<int>(enemies.size()));
                for (Unit* enemy : enemies) {
                    damageUnit(*enemy, each, true, nullptr, owner);
                }
                log("The dying titan detonates for " + std::to_string(each) + " to each frame");
            }
        }
        break;
    }
    case TrapKind::None:
        break;
    }
}

void DuelEngine::applyTribunalPayoff(Side owner) {
    // The Inquisitor passive used to be "arm counter-protocols for free", which
    // saved one energy a turn and was worth almost nothing. The doctrine builds
    // its whole deck around counters firing, so that is what it is now paid for.
    if (m_commanders[index(owner)].getPrimaryRole() != MechRole::Inquisitor) return;

    drawFor(owner, kTribunalDraw, "Cold Read: the tribunal reads ahead");
    damageCommander(other(owner), kTribunalBurn,
                    "The tribunal burns the enemy core");
}

void DuelEngine::seizeEnemyUnit(Side owner) {
    // Only fires while a Deus Ex-Machina Core is on our board.
    bool hasCore = false;
    for (const Unit* ally : m_board.units(owner)) {
        for (const Ability& ability : ally->data.abilities) {
            if (ability.kind == AbilityKind::SeizeUnitOnTrapFlip) hasCore = true;
        }
    }
    if (!hasCore) return;

    const Side foe = other(owner);
    const int slot = m_board.freeSlot(owner, BoardLine::Frontline);
    if (slot < 0) return;

    // Take the biggest thing standing in their frontline.
    Unit* best = nullptr;
    for (Unit* enemy : m_board.unitsIn(foe, BoardLine::Frontline)) {
        if (!best || enemy->attack() > best->attack()) best = enemy;
    }
    if (!best) return;

    const UnitLocation where = m_board.locate(best->instanceId);
    std::unique_ptr<Unit> seized = m_board.take(where.side, where.line, where.slot);
    if (!seized) return;

    const std::string name = seized->data.name;
    seized->owner = owner;
    seized->exhausted = true;
    seized->attacksThisTurn = seized->attacksAllowed();
    const int id = seized->instanceId;
    m_board.place(owner, BoardLine::Frontline, slot, std::move(seized));

    emit(DuelEvent::Type::UnitSummoned, owner, name + " is hijacked mid-battle", id);
}

void DuelEngine::destroyAllTraps(int* destroyedCount) {
    int count = 0;
    for (Side side : { Side::Player, Side::Opponent }) {
        auto& traps = m_board.traps(side);
        for (TrapCard& trap : traps) {
            m_commanders[index(side)].sendToCrypt(trap.data);
            ++count;
        }
        traps.clear();
    }
    if (destroyedCount) *destroyedCount = count;
}

// =============================================================================
// Events
// =============================================================================

void DuelEngine::emit(DuelEvent::Type type, Side side, const std::string& text,
                      int instanceId, int amount, int targetId,
                      const std::string& cardId) {
    DuelEvent event;
    event.type = type;
    event.side = side;
    event.text = text;
    event.instanceId = instanceId;
    event.amount = amount;
    event.targetId = targetId;
    event.cardId = cardId;
    m_events.push_back(std::move(event));
}

std::vector<DuelEvent> DuelEngine::drainEvents() {
    std::vector<DuelEvent> out;
    out.swap(m_events);
    return out;
}
