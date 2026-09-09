// AI-versus-AI simulation: proves duels terminate, the AI plays legally,
// and the campaign curve lands in a playable difficulty band.

#include "TestAssert.hpp"
#include "battle/AICommander.hpp"
#include "battle/DuelEngine.hpp"
#include "run/DeckBuilder.hpp"
#include "run/RunState.hpp"
#include "utils/DataLoader.hpp"
#include "utils/Rng.hpp"
#include <iostream>
#include <algorithm>
#include <string>
#include <iterator>
#include <vector>

namespace {

struct SimResult {
    bool playerWon = false;
    bool timedOut = false;
    int turns = 0;
    int playerHpLeft = 0;
    int enemyHpLeft = 0;
    int trapsFlipped = 0;
    int trapsSet = 0;
    int tier3Summoned = 0;
};

/// The pairing the simulated player runs unless a test names another.
///
/// Deliberately a MIDDLE deck, not the best one. The campaign curve was tuned
/// for a long time against Vanguard/Valkyrie, which was then the strongest
/// pairing in the game and is now, after the doctrine pass, one of the weakest -
/// so the ladder was first tuned against a wall no ordinary deck could match,
/// and then against a deck no ordinary player would be stuck with. Neither
/// describes the fight a typical run actually has. Siege/Valkyrie sits in the
/// middle of both sweeps, and is not one of the doctrines under adjustment.
constexpr MechRole kDefaultPrimary = MechRole::Siege;
constexpr MechRole kDefaultSecondary = MechRole::Valkyrie;

/// 60, not 40: a 40-card deck drawing one card a turn does not even empty
/// inside forty rounds, so the old cap was cutting duels off before reactor
/// strain could force a conclusion.
SimResult simulateDuel(unsigned int seed, int encounterIndex = 0, int maxTurns = 60,
                       bool verbose = false,
                       MechRole primary = kDefaultPrimary,
                       MechRole secondary = kDefaultSecondary) {
    Rng::seed(seed);

    RunState run;
    run.startNewRun(primary, secondary);
    for (int i = 0; i < encounterIndex; ++i) {
        const auto rewards = run.rollRewards();
        run.winEncounter(rewards.empty() ? CardData{} : rewards.front());
    }

    const Encounter& encounter = run.currentEncounter();

    DuelistSetup you;
    you.deck = run.buildBattleDeck();
    you.primary = run.getPrimaryRole();
    you.secondary = run.getSecondaryRole();
    you.hp = run.getCommanderHp();
    you.name = roleTitle(you.primary);

    DuelistSetup foe;
    foe.deck = run.buildOpponentDeck();
    foe.primary = encounter.primary;
    foe.secondary = encounter.secondary;
    foe.hp = encounter.commanderHp;
    foe.name = encounter.name;

    DuelEngine duel;
    duel.startDuel(std::move(you), std::move(foe));

    AICommander playerBrain(Side::Player);
    AICommander enemyBrain(Side::Opponent);

    SimResult result;
    for (int turn = 0; turn < maxTurns * 2; ++turn) {
        if (duel.isOver()) break;
        result.turns = duel.turnNumber();

        if (duel.activeSide() == Side::Player) playerBrain.takeTurn(duel);
        else                                   enemyBrain.takeTurn(duel);

        for (const DuelEvent& event : duel.events()) {
            if (event.type == DuelEvent::Type::TrapFlipped) ++result.trapsFlipped;
            if (event.type == DuelEvent::Type::TrapSet) ++result.trapsSet;
        }
        duel.drainEvents();

        if (verbose && duel.activeSide() == Side::Player) {
            std::cout << "  turn " << duel.turnNumber()
                      << " | you " << duel.commander(Side::Player).getHp()
                      << " hp, board " << duel.board().unitCount(Side::Player)
                      << "  ||  foe " << duel.commander(Side::Opponent).getHp()
                      << " hp, board " << duel.board().unitCount(Side::Opponent) << "\n";
        }
    }

    result.playerHpLeft = duel.commander(Side::Player).getHp();
    result.enemyHpLeft = duel.commander(Side::Opponent).getHp();
    result.playerWon = duel.isOver() && duel.winner() == Side::Player;
    result.timedOut = !duel.isOver();
    return result;
}


/**
 * @brief The reference player deck against one hypothetical commander.
 *
 * The campaign sweep measures encounters as they are written in the table, which
 * only tells you the ladder is wrong, never which rung to move. This builds an
 * opponent out of any pairing at a fixed reactor and titan count, so the five
 * fights can be ordered by measured difficulty instead of by how threatening
 * their doctrine names sound.
 */
double opponentPairingRate(MechRole foePrimary, MechRole foeSecondary,
                           int hp, int extraTitans, int runs,
                           MechRole refPrimary, MechRole refSecondary) {
    if (foePrimary == foeSecondary) return -1.0;

    Encounter probe;
    probe.name = "probe";
    probe.primary = foePrimary;
    probe.secondary = foeSecondary;
    probe.commanderHp = hp;
    probe.extraTitans = extraTitans;

    int wins = 0;
    for (unsigned int seed = 1; seed <= static_cast<unsigned>(runs); ++seed) {
        Rng::seed(seed * 29);

        RunState run;
        run.startNewRun(refPrimary, refSecondary);

        DuelistSetup you;
        you.deck = run.buildBattleDeck();
        you.primary = run.getPrimaryRole();
        you.secondary = run.getSecondaryRole();
        you.hp = run.getCommanderHp();
        you.name = roleTitle(you.primary);

        DuelistSetup foe;
        foe.deck = RunState::buildDeckFor(probe);
        foe.primary = foePrimary;
        foe.secondary = foeSecondary;
        foe.hp = hp;
        foe.name = probe.name;

        DuelEngine duel;
        duel.startDuel(std::move(you), std::move(foe));
        AICommander playerBrain(Side::Player);
        AICommander enemyBrain(Side::Opponent);

        for (int turn = 0; turn < 120; ++turn) {
            if (duel.isOver()) break;
            if (duel.activeSide() == Side::Player) playerBrain.takeTurn(duel);
            else                                  enemyBrain.takeTurn(duel);
        }
        if (duel.winner() == Side::Player) ++wins;
    }
    return 100.0 * wins / runs;
}

} // namespace

void test_single_duel_plays_out() {
    std::cout << "One duel in detail (seed 7):\n";
    SimResult r = simulateDuel(7, 0, 60, true);
    std::cout << "  => " << (r.playerWon ? "PLAYER WINS" : (r.timedOut ? "TIMED OUT" : "ENEMY WINS"))
              << " after " << r.turns << " turns | you " << r.playerHpLeft
              << " hp, foe " << r.enemyHpLeft << " hp\n";

    CHECK_MSG(!r.timedOut, "a duel must reach a conclusion");
    CHECK_MSG(r.turns >= 4, "duels should last more than a couple of turns");
    std::cout << "[PASS] test_single_duel_plays_out\n";
}

void test_all_duels_terminate() {
    for (unsigned int seed = 1; seed <= 120; ++seed) {
        for (int encounter = 0; encounter < RunState::kEncounters; ++encounter) {
            SimResult r = simulateDuel(seed * 31 + static_cast<unsigned>(encounter), encounter);
            CHECK_MSG(!r.timedOut, "found a duel that never ends");
        }
    }
    std::cout << "[PASS] test_all_duels_terminate (600 duels)\n";
}

void test_traps_and_titans_actually_see_play() {
    int traps = 0;
    int games = 0;
    int probeSet = 0;
    // Measured with a deck that actually HAS counter-protocols. Only three of the
    // six doctrines carry any - Paladin, Valkyrie and Siege have none at all - so
    // running this against the default reference pairing was asserting that a
    // deck holding zero traps flips one.
    for (unsigned int seed = 1; seed <= 60; ++seed) {
        SimResult r = simulateDuel(seed, 3, 60, false,
                                   MechRole::Inquisitor, MechRole::Dragoon);
        traps += r.trapsFlipped;
        probeSet += r.trapsSet;
        ++games;
    }
    std::cout << "  traps flipped across " << games << " duels: " << traps
              << " (from " << probeSet << " armed)\n";
    CHECK_MSG(traps > 0, "traps are never firing - the trap system is dead weight");
    std::cout << "[PASS] test_traps_and_titans_actually_see_play\n";
}

void test_difficulty_curve_rises() {
    std::vector<double> winRates;
    for (int encounter = 0; encounter < RunState::kEncounters; ++encounter) {
        int wins = 0;
        int totalTurns = 0;
        int trapsSet = 0;
        int trapsFired = 0;
        const int runs = 120;
        for (unsigned int seed = 1; seed <= static_cast<unsigned>(runs); ++seed) {
            SimResult r = simulateDuel(seed * 17 + static_cast<unsigned>(encounter), encounter);
            if (r.playerWon) ++wins;
            totalTurns += r.turns;
            trapsSet += r.trapsSet;
            trapsFired += r.trapsFlipped;
        }
        const double winRate = 100.0 * wins / runs;
        const double avgTurns = static_cast<double>(totalTurns) / runs;
        std::cout << "  encounter " << (encounter + 1) << " (" << RunState::path()[encounter].name
                  << "): win " << winRate << "%  avg " << avgTurns << " turns"
                  // Counters armed / counters that actually resolved. A fight
                  // showing 0 armed is one where neither deck carries any:
                  // Paladin, Valkyrie and Siege have no counter-protocols.
                  << "  counters " << trapsSet << " armed, " << trapsFired << " fired\n";

        CHECK_MSG(winRate > 5.0, "an encounter is effectively unwinnable");
        CHECK_MSG(winRate < 96.0, "an encounter is a free win");
        CHECK_MSG(avgTurns >= 4.0, "duels resolve too quickly to be interesting");
        winRates.push_back(winRate);
    }

    // The old version of this test only asked that no encounter was 0% or
    // 100%, which a completely flat campaign passes. What the run actually
    // promises is that it gets harder, so assert that.
    CHECK_MSG(winRates.back() < winRates.front() - 10.0,
              "the final commander is no harder than the first");
    std::cout << "[PASS] test_difficulty_curve_rises\n";
}

void test_run_progression_and_rewards() {
    Rng::seed(99);
    RunState run;
    run.startNewRun(kDefaultPrimary, kDefaultSecondary);

    const int startingSize = run.deckSize();
    CHECK(startingSize == DeckRules::kDeckSize);
    CHECK(run.getConfiguration().isValidDeck());
    CHECK(run.getEncounterNumber() == 1);
    CHECK(!run.isComplete());

    for (int i = 0; i < RunState::kEncounters; ++i) {
        const auto rewards = run.rollRewards();
        CHECK(rewards.size() == static_cast<size_t>(RunState::kRewardChoices));
        // Offers must be distinct and legal for the player's two cores: the
        // primary lends anything, the secondary lends everything but its titan.
        CHECK(rewards[0].id != rewards[1].id);
        CHECK(rewards[1].id != rewards[2].id);
        CHECK(rewards[0].id != rewards[2].id);
        for (const CardData& card : rewards) {
            CHECK(card.role == run.getPrimaryRole() || card.role == run.getSecondaryRole());
            if (card.role == run.getSecondaryRole()) CHECK(card.tier != CardTier::Tier3);
        }

        run.setCommanderHp(12);
        run.winEncounter(rewards.front());
        CHECK(run.deckSize() == startingSize + i + 1);
        CHECK(run.getCommanderHp() == 12 + RunState::kHealBetweenFights);
    }

    CHECK(run.isComplete());
    std::cout << "[PASS] test_run_progression_and_rewards\n";
}

void test_opponent_decks_scale() {
    Rng::seed(5);
    RunState run;
    run.startNewRun(kDefaultPrimary, kDefaultSecondary);

    int firstBosses = 0;
    for (const CardData& card : run.buildOpponentDeck()) {
        if (card.tier == CardTier::Tier3) ++firstBosses;
    }

    for (int i = 0; i < 4; ++i) {
        const auto rewards = run.rollRewards();
        run.winEncounter(rewards.empty() ? CardData{} : rewards.front());
    }

    int finalBosses = 0;
    const auto finalDeck = run.buildOpponentDeck();
    for (const CardData& card : finalDeck) {
        if (card.tier == CardTier::Tier3) ++finalBosses;
    }

    std::cout << "  Tier 3 count: first fight " << firstBosses
              << ", final fight " << finalBosses << "\n";
    CHECK_MSG(firstBosses == 0, "the opening commander should field no titan");
    // extraTitans is additional copies: the builder already puts the role's
    // single titan in, so the deck holds one plus that many.
    CHECK_MSG(finalBosses == RunState::path().back().extraTitans + 1,
              "the final commander's titan count does not match its encounter");
    CHECK(static_cast<int>(finalDeck.size()) == RunState::path().back().deckSize);

    std::cout << "[PASS] test_opponent_decks_scale\n";
}

/**
 * A diagnostic, not an assertion: prints how hard every doctrine pairing is to
 * FACE, at a fixed reactor and titan count so the doctrines are the only
 * variable. Read it when the campaign curve needs reordering - the ladder should
 * climb this list, and a fight sitting out of order here is the cliff.
 */
void report_opponent_difficulty() {
    struct Row { std::string name; double rate; };
    std::vector<Row> rows;

    // Averaged over several reference decks on purpose. Measuring against ONE
    // deck makes the whole table move whenever that deck's doctrine is touched:
    // nerfing Vanguard once dropped every row here by thirty points, which said
    // nothing about the opponents and made the tool useless exactly when it was
    // most needed. These three spread across the middle of the roster.
    const std::pair<MechRole, MechRole> references[] = {
        { MechRole::Siege,    MechRole::Paladin },
        { MechRole::Dragoon,  MechRole::Siege },
        { MechRole::Valkyrie, MechRole::Inquisitor },
    };

    std::cout << "Opponent difficulty at a fixed 38 hp / 1 titan (3 reference decks"
              << " x 24 duels,\n  lower = harder to face):\n";

    for (int a = 0; a < kMechRoleCount; ++a) {
        for (int b = 0; b < kMechRoleCount; ++b) {
            if (a == b) continue;
            const MechRole p = static_cast<MechRole>(a);
            const MechRole s = static_cast<MechRole>(b);

            double total = 0.0;
            for (const auto& ref : references) {
                total += opponentPairingRate(p, s, 38, 1, 24, ref.first, ref.second);
            }
            const double rate = total / static_cast<double>(std::size(references));
            rows.push_back({ std::string(toString(p)) + "/" + toString(s), rate });
        }
    }

    std::sort(rows.begin(), rows.end(),
              [](const Row& x, const Row& y) { return x.rate > y.rate; });
    for (const Row& row : rows) {
        std::cout << "  " << row.name << std::string(
            row.name.size() < 24 ? 24 - row.name.size() : 1, ' ')
                  << static_cast<int>(row.rate) << "%\n";
    }
    std::cout << "[INFO] report_opponent_difficulty\n";
}

void test_no_pairing_is_dead_on_arrival() {
    // A guard rail, not a balance verdict.
    //
    // WHAT THIS MEASURES: how well AICommander - one-ply, greedy, no planning -
    // pilots each pairing. That is not the same as how strong the pairing is in
    // a human's hands. Paladin and Dragoon are tempo and combo doctrines whose
    // payoff needs a plan across turns, and this AI has none, so they sit at
    // the bottom of the table whatever their cards say. Do not tune them down
    // to fit this number.
    //
    // WHAT IT CATCHES: a pairing that cannot win at all, and one that wins on
    // autopilot. Both mean something is structurally broken - a dead card type
    // filling a deck, a keyword with no drawback, a passive that does nothing -
    // and every one of those found so far was a real bug.
    //
    // The floor and the ceiling are different questions and need different
    // reference fights. Measuring both at one encounter makes one of them
    // meaningless: at the opener every pairing clears 65%, so a ceiling there
    // catches nothing, and at the boss every pairing is near the floor, so a
    // floor there condemns all thirty the moment the boss is retuned.
    //
    //   floor   -> the OPENING fight: can a run be started with this pairing?
    //   ceiling -> the FINAL fight:   does this pairing beat the hardest thing
    //                                 on the path without needing a plan?
    //
    // Both matrices are measured before anything is asserted, so one run prints
    // the full picture instead of stopping at the first offender.
    struct Row { std::string name; double rate; };

    const auto sweep = [](int encounter, const char* heading) {
        std::vector<Row> out;
        std::cout << heading;
        for (int a = 0; a < kMechRoleCount; ++a) {
            const MechRole primary = static_cast<MechRole>(a);
            std::cout << "  " << toString(primary) << ":";
            for (int b = 0; b < kMechRoleCount; ++b) {
                if (a == b) continue;
                const MechRole secondary = static_cast<MechRole>(b);

                int wins = 0;
                const int runs = 40;
                for (unsigned int seed = 1; seed <= static_cast<unsigned>(runs); ++seed) {
                    SimResult r = simulateDuel(seed * 13, encounter, 60, false, primary, secondary);
                    if (r.playerWon) ++wins;
                }
                const double rate = 100.0 * wins / runs;
                std::cout << "  +" << toString(secondary) << " " << static_cast<int>(rate) << "%";
                out.push_back({ std::string(toString(primary)) + "/" + toString(secondary), rate });
            }
            std::cout << "\n";
        }
        std::sort(out.begin(), out.end(),
                  [](const Row& x, const Row& y) { return x.rate < y.rate; });
        std::cout << "  weakest: " << out.front().name << " " << out.front().rate << "%"
                  << "   strongest: " << out.back().name << " " << out.back().rate << "%\n";
        return out;
    };

    const std::vector<Row> opener = sweep(0, "Pairing sweep vs fight 1 - the floor (40 duels each):\n");
    const std::vector<Row> boss = sweep(RunState::kEncounters - 1,
                                        "Pairing sweep vs the final fight - the ceiling (40 duels each):\n");

    for (const Row& row : opener) {
        const std::string where = row.name + " wins " + std::to_string(static_cast<int>(row.rate))
                                + "% of openers";
        CHECK_MSG(row.rate >= 5.0, where.c_str());
    }
    for (const Row& row : boss) {
        const std::string where = row.name + " wins " + std::to_string(static_cast<int>(row.rate))
                                + "% of boss fights";
        CHECK_MSG(row.rate <= 90.0, where.c_str());
    }
    std::cout << "[PASS] test_no_pairing_is_dead_on_arrival\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << " DUEL SIMULATION TESTS\n";
    std::cout << "========================================\n";

    test_single_duel_plays_out();
    test_all_duels_terminate();
    test_traps_and_titans_actually_see_play();
    test_run_progression_and_rewards();
    test_opponent_decks_scale();
    // Diagnostics before assertions: when the curve test trips, the sweep it
    // would have printed is exactly what tells you which rung to move.
    report_opponent_difficulty();
    test_difficulty_curve_rises();
    test_no_pairing_is_dead_on_arrival();

    std::cout << "========================================\n";
    std::cout << " ALL DUEL SIMULATION TESTS PASSED\n";
    std::cout << "========================================\n";
    return 0;
}
