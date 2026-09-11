#include "battle/AICommander.hpp"
#include <algorithm>
#include <vector>

namespace {

/// Rough board value of a unit, used for trades and tribute choices.
int unitValue(const Unit& unit) {
    int value = unit.attack() * 2 + unit.health();
    if (unit.hasKeyword(Keyword::Taunt))   value += 3;
    if (unit.hasKeyword(Keyword::Overkill)) value += 3;
    if (unit.hasKeyword(Keyword::Ranged))  value += 3;
    if (unit.hasKeyword(Keyword::Aerial))  value += 3;
    if (!unit.data.abilities.empty())      value += 2;
    if (unit.data.tier == CardTier::Tier3) value += 6;
    return value;
}

} // namespace

void AICommander::takeTurn(DuelEngine& duel, int actionBudget) {
    for (int i = 0; i < actionBudget; ++i) {
        if (duel.isOver()) return;
        if (!step(duel)) break;
    }
    if (!duel.isOver()) duel.endTurn();
}

bool AICommander::step(DuelEngine& duel) {
    if (duel.isOver() || duel.activeSide() != m_side) return false;
    // Deploy first, then reposition, then swing: cards played this turn can
    // still matter to the attack step (Rush), but not the other way round.
    if (tryPlayCard(duel)) return true;
    if (tryAdvance(duel)) return true;
    if (tryAttack(duel)) return true;
    return false;
}

// =============================================================================
// Deployment
// =============================================================================

int AICommander::scoreTrap(const DuelEngine& duel, const CardData& card) const {
    const Board& board = duel.board();
    const Side foe = other(m_side);

    // A counter-protocol used to score a flat 6 while a unit scored
    // attack * 3 + health * 2 - typically 20 to 35. So a trap was never the best
    // card in hand, and got armed only when nothing else could be played at all:
    // fourteen times across sixty duels, against six hundred reactor hits that
    // would have triggered one. That single number was most of why Inquisitor,
    // whose deck is a third counter-protocols and whose titan pays out per
    // counter fired, could not win a game.
    //
    // Scored on the unit scale now: how likely the window is to open, times what
    // happens when it does.
    int score = 12;

    switch (card.trapTrigger) {
    case TrapTrigger::OnEnemyAttackReactor:
        // The most reliable window there is - every duel has a dozen of these.
        score += 11;
        break;
    case TrapTrigger::OnEnemyAdvance:
        score += 9;
        break;
    case TrapTrigger::OnEnemyHighTierDeploy:
        // Only pays against decks that field heavy frames, and the later
        // commanders do. Worth less on turn two than on turn ten.
        score += duel.turnNumber() >= 4 ? 8 : 3;
        break;
    case TrapTrigger::OnEnemySpellCast:
        score += 6;
        break;
    case TrapTrigger::OnAllyTargetedByRemoval:
        score += board.unitCount(m_side) >= 2 ? 5 : 1;
        break;
    case TrapTrigger::OnOwnTitanDestroyed: {
        // Dead weight until a titan of ours is actually standing there to die.
        bool haveTitan = false;
        for (const Unit* unit : board.units(m_side)) {
            if (unit->data.tier == CardTier::Tier3) { haveTitan = true; break; }
        }
        score += haveTitan ? 10 : -6;
        break;
    }
    case TrapTrigger::None:
        return -1;
    }

    switch (card.trapKind) {
    case TrapKind::BlockAndCrushWeak:     score += 7; break;
    case TrapKind::NegateSpellAndBurn:    score += 6; break;
    case TrapKind::OverloadSummon:        score += 6; break;
    case TrapKind::WeakenAdvancingUnit:   score += 5; break;
    case TrapKind::RecallTargetToHand:    score += 4; break;
    case TrapKind::DetonateForTitanAttack: score += 5; break;
    case TrapKind::None:                  break;
    }

    // Deus Ex-Machina seizes an enemy frame every time one of our counters
    // fires, which turns every armed trap into a two-for-one.
    for (const Unit* unit : board.units(m_side)) {
        for (const Ability& ability : unit->data.abilities) {
            if (ability.kind == AbilityKind::SeizeUnitOnTrapFlip) score += 12;
        }
    }

    // With nothing on the board, a body is more urgent than a trick.
    if (board.unitCount(m_side) == 0) score -= 6;
    // The counter zone holds three; the third is worth less than the first.
    score -= static_cast<int>(board.traps(m_side).size()) * 3;

    (void)foe;
    return score - DuelEngine::kSetTrapCost * 2;
}

int AICommander::scoreCard(const DuelEngine& duel, const CardData& card, int cost) const {
    const Board& board = duel.board();
    const Side foe = other(m_side);

    switch (card.category) {
    case CardCategory::Unit: {
        int score = card.attack * 3 + card.health * 2;
        // A frame that bills the overcharge core for every swing is only worth
        // deploying if the core can actually pay for it.
        for (const Ability& ability : card.abilities) {
            if (ability.kind != AbilityKind::SpendOverchargeToStrike) continue;
            score += duel.commander(m_side).getOvercharge() >= ability.value * 2 ? 2 : -10;
        }
        if (card.hasKeyword(Keyword::Rush))    score += 6;
        if (card.hasKeyword(Keyword::Overkill)) score += 5;
        if (card.hasKeyword(Keyword::Taunt))   score += 4;
        if (card.hasKeyword(Keyword::Aerial))  score += 5;
        if (card.hasKeyword(Keyword::Splash))  score += 4;
        if (!card.abilities.empty())           score += 4;
        if (card.tier == CardTier::Tier3)      score += 10;
        // Cheap bodies matter more when the board is empty.
        if (board.unitCount(m_side) == 0)      score += 6;
        return score - cost * 2;
    }
    case CardCategory::Spell: {
        switch (card.spell) {
        case SpellKind::DamageEnemyFrontline: {
            const auto targets = board.unitsIn(foe, BoardLine::Frontline);
            int hit = 0;
            for (const Unit* unit : targets) {
                if (unit->health() <= card.spellValue) hit += 12;   // a kill
                else hit += 4;
            }
            return hit - cost * 2;
        }
        case SpellKind::OverchargeSurge: {
            // Worth an operation in proportion to what is waiting to spend it.
            int hungry = 0;
            for (const Unit* unit : board.units(m_side)) {
                for (const Ability& ability : unit->data.abilities) {
                    if (ability.kind == AbilityKind::SpendOverchargeToStrike) ++hungry;
                    if (ability.kind == AbilityKind::DumpOverchargeOnDeploy) ++hungry;
                }
            }
            if (duel.commander(m_side).getPrimaryRole() != MechRole::Paladin && hungry == 0) {
                return 1;
            }
            return 6 + hungry * 4 - cost;
        }
        case SpellKind::WipeLowHealthUnits: {
            // Count the swing: their wrecks minus ours.
            int swing = 0;
            for (const Unit* unit : board.units(foe)) {
                if (unit->health() <= card.spellValue) swing += 10;
            }
            for (const Unit* unit : board.units(m_side)) {
                if (unit->health() <= card.spellValue) swing -= 8;
            }
            return swing - cost * 2;
        }
        case SpellKind::DestroyHighAttackEnemy: {
            const auto targets = board.units(foe);
            const bool anyTarget = std::any_of(targets.begin(), targets.end(),
                [&](const Unit* u) { return u->attack() >= card.spellValue; });
            return anyTarget ? 16 - cost : -1;
        }
        case SpellKind::ArmourAllAllies:
            return board.unitCount(m_side) >= 2 ? 6 - cost : -1;
        default:
            return 5 - cost;
        }
    }
    case CardCategory::Trap:
        return board.trapZoneFull(m_side) ? -1 : scoreTrap(duel, card);
    }
    return -1;
}

std::vector<int> AICommander::chooseTributes(const DuelEngine& duel, const CardData& card,
                                             int needed) const {
    std::vector<int> chosen;
    if (needed <= 0) return chosen;

    std::vector<const Unit*> mine = duel.board().units(m_side);
    // Offer up the least valuable bodies, and never the card we would be
    // buffing with an aura we still want.
    std::sort(mine.begin(), mine.end(), [](const Unit* a, const Unit* b) {
        return unitValue(*a) < unitValue(*b);
    });

    for (const Unit* unit : mine) {
        if (static_cast<int>(chosen.size()) >= needed) break;
        if (unit->data.tier == CardTier::Tier3) continue;   // never scrap a titan
        chosen.push_back(unit->instanceId);
    }
    (void)card;
    return chosen;
}

bool AICommander::tryPlayCard(DuelEngine& duel) {
    Commander& me = duel.commander(m_side);
    const auto& hand = me.getHand();

    int bestIndex = -1;
    int bestScore = 0;
    std::vector<int> bestTributes;
    BoardLine bestLine = BoardLine::Frontline;
    // -1 means "first free slot", which is what the AI used to do on every
    // deployment. Under lane rules that put Guards on the edge where they cover
    // one frame instead of two, and left soft frames standing in the open.
    int bestSlot = -1;

    for (size_t i = 0; i < hand.size(); ++i) {
        const CardData& card = hand[i];

        if (card.category == CardCategory::Trap) {
            if (duel.board().trapZoneFull(m_side)) continue;
            if (!me.canAfford(DuelEngine::kSetTrapCost)) continue;
            const int score = scoreCard(duel, card, DuelEngine::kSetTrapCost);
            if (score > bestScore) {
                bestScore = score; bestIndex = static_cast<int>(i); bestTributes.clear();
            }
            continue;
        }

        if (card.category == CardCategory::Spell) {
            if (!me.canAfford(card.manaCost)) continue;
            if (card.spell == SpellKind::DestroyHighAttackEnemy) continue; // needs a target picker
            const int score = scoreCard(duel, card, card.manaCost);
            if (score > bestScore) {
                bestScore = score; bestIndex = static_cast<int>(i); bestTributes.clear();
            }
            continue;
        }

        // Units: try the cheapest legal tribute count that we can pay for.
        const int maxTributes = card.tributeRequirement();
        for (int tributes = 0; tributes <= maxTributes; ++tributes) {
            std::vector<int> offered = chooseTributes(duel, card, tributes);
            if (static_cast<int>(offered.size()) < tributes) continue;

            const int cost = duel.summonCost(card, tributes);
            if (!me.canAfford(cost)) continue;

            // Prefer the frontline; fall back to support when it is full.
            BoardLine line = duel.board().hasRoom(m_side, BoardLine::Frontline)
                                 ? BoardLine::Frontline : BoardLine::Support;
            if (!duel.board().hasRoom(m_side, line)) continue;
            // A ranged unit is safer in the back.
            if (card.hasKeyword(Keyword::Ranged) &&
                duel.board().hasRoom(m_side, BoardLine::Support)) {
                line = BoardLine::Support;
            }

            int score = scoreCard(duel, card, cost);
            // Tributing costs board presence: only do it when it unlocks the play.
            score -= static_cast<int>(offered.size()) * 6;

            if (score > bestScore) {
                bestScore = score;
                bestIndex = static_cast<int>(i);
                bestTributes = offered;
                bestLine = line;
                bestSlot = bestSlotFor(duel, card, line);
            }
        }
    }

    if (bestIndex < 0) return false;

    const CardData card = hand[static_cast<size_t>(bestIndex)];
    switch (card.category) {
    case CardCategory::Trap:
        return duel.setTrap(m_side, static_cast<size_t>(bestIndex)) == ActionResult::Ok;
    case CardCategory::Spell:
        return duel.castSpell(m_side, static_cast<size_t>(bestIndex)) == ActionResult::Ok;
    case CardCategory::Unit:
        return duel.summonFromHand(m_side, static_cast<size_t>(bestIndex), bestLine,
                                   bestSlot, bestTributes) == ActionResult::Ok;
    }
    return false;
}

// =============================================================================
// Movement
// =============================================================================

bool AICommander::tryAdvance(DuelEngine& duel) {
    if (!duel.commander(m_side).canAfford(DuelEngine::kMoveCost)) return false;
    if (!duel.board().hasRoom(m_side, BoardLine::Frontline)) return false;

    for (Unit* unit : duel.board().unitsIn(m_side, BoardLine::Support)) {
        if (!duel.canAdvance(unit->instanceId)) continue;
        // Ranged units are doing their job from the back row.
        if (unit->hasKeyword(Keyword::Ranged)) continue;
        // Only push forward with something that can actually fight.
        if (unit->attack() <= 0) continue;
        return duel.advanceUnit(m_side, unit->instanceId) == ActionResult::Ok;
    }
    return false;
}

// =============================================================================
// Combat
// =============================================================================

int AICommander::guardCoverage(const DuelEngine& duel, const Unit& guard) const {
    if (!guard.hasKeyword(Keyword::Taunt)) return 0;

    const UnitLocation at = duel.board().locate(guard.instanceId);
    if (!at.valid()) return 0;

    int covered = 0;
    for (int offset : { -1, 1 }) {
        const int slot = at.slot + offset;
        if (slot < 0 || slot >= Board::kLineSlots) continue;
        const Unit* ally = duel.board().at(at.side, at.line, slot);
        // A neighbouring Guard screens itself, so it is not "covered".
        if (ally && ally->isAlive() && !ally->hasKeyword(Keyword::Taunt)) ++covered;
    }
    return covered;
}

int AICommander::bestSlotFor(const DuelEngine& duel, const CardData& card,
                             BoardLine line) const {
    const Board& board = duel.board();

    int bestSlot = -1;
    int bestScore = -1000;
    for (int slot = 0; slot < Board::kLineSlots; ++slot) {
        if (board.at(m_side, line, slot)) continue;

        int score = 0;
        if (card.hasKeyword(Keyword::Taunt)) {
            // A Guard screens its neighbours, so an interior lane is worth
            // twice an edge one: lane 0 and lane 3 only ever cover one side.
            const bool interior = slot > 0 && slot < Board::kLineSlots - 1;
            score += interior ? 14 : 6;
            // Standing beside something worth protecting is the whole point.
            for (int offset : { -1, 1 }) {
                const int beside = slot + offset;
                if (beside < 0 || beside >= Board::kLineSlots) continue;
                if (board.at(m_side, line, beside)) score += 8;
            }
        } else {
            // Everything else wants to stand where a Guard already covers it.
            for (int offset : { -1, 1 }) {
                const int beside = slot + offset;
                if (beside < 0 || beside >= Board::kLineSlots) continue;
                const Unit* neighbour = board.at(m_side, line, beside);
                if (neighbour && neighbour->hasKeyword(Keyword::Taunt)) {
                    // The softer the frame, the more the cover is worth.
                    score += 12 + std::max(0, 6 - card.health) * 2;
                }
            }
            // Failing that, keep the interior lanes free for future Guards.
            if (score == 0 && (slot == 0 || slot == Board::kLineSlots - 1)) score += 2;
        }

        if (score > bestScore) { bestScore = score; bestSlot = slot; }
    }
    return bestSlot;
}

int AICommander::scoreAttack(const DuelEngine& duel, const Unit& attacker, int targetId) const {
    // Face damage is the win condition; weight it, but not above a free kill.
    if (targetId < 0) return 10 + attacker.attack() * 3;

    const Unit* target = duel.board().findById(targetId);
    if (!target) return -1;

    const bool ranged = attacker.hasKeyword(Keyword::Ranged);
    const int myDamage = attacker.attack();
    const int theirDamage = ranged ? 0 : target->attack();

    int score = 0;
    const bool kills = myDamage >= target->health();
    const bool dies = !ranged && theirDamage >= attacker.health();

    if (kills) score += unitValue(*target) + 8;
    else       score += myDamage * 2;

    if (dies)  score -= unitValue(attacker) + 6;

    // Focus fire. A frame already carrying damage is closer to dead, and a
    // half-killed frame that survives the turn swings back at full strength -
    // spreading damage across a row is how the old AI lost trades it had
    // already paid for.
    score += target->damage * 2;

    // A Guard is worth what it is screening, not a flat bonus: killing one that
    // covers two frames opens two lanes, killing one on the edge opens one.
    if (kills && target->hasKeyword(Keyword::Taunt)) {
        score += 6 + guardCoverage(duel, *target) * 9;
    }

    // Flak. Flying into a covered lane costs the flier its interceptor's attack
    // before it lands anything, and can cost it the whole frame. Without this
    // the AI flew its Titans into a 4 attack turret every turn, which made
    // Intercept read as a trap on the AI rather than as a rule.
    if (attacker.hasKeyword(Keyword::Aerial)) {
        const UnitLocation at = duel.board().locate(targetId);
        if (at.valid()) {
            if (const Unit* flak = duel.interceptorOver(other(m_side), at.slot)) {
                const int bite = flak->attack();
                score -= bite * 2;
                // Being shot down is worse than taking the hit: the strike it
                // was carrying never lands either.
                if (bite >= attacker.health()) score -= unitValue(attacker) + 10;
            }
        }
    }
    return score;
}

bool AICommander::tryAttack(DuelEngine& duel) {
    struct Option { int attacker; int target; int score; };
    std::vector<Option> options;

    for (Unit* unit : duel.board().units(m_side)) {
        if (!unit->canAct() || unit->attack() <= 0) continue;
        for (int targetId : duel.legalTargets(unit->instanceId)) {
            const int score = scoreAttack(duel, *unit, targetId);
            if (score > 0) options.push_back({ unit->instanceId, targetId, score });
        }
    }

    std::sort(options.begin(), options.end(),
              [](const Option& a, const Option& b) { return a.score > b.score; });

    // Walk the ranked list rather than committing to the single best pair. The
    // engine can legitimately refuse an attack - a Judicator whose overcharge
    // core is dry, for one - and taking that as "no attacks are possible" used
    // to end the turn with a full board standing idle.
    for (const Option& option : options) {
        if (duel.declareAttack(m_side, option.attacker, option.target) == ActionResult::Ok) {
            return true;
        }
    }
    return false;
}
