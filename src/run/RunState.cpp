#include "run/RunState.hpp"

#include "run/DeckStore.hpp"
#include "utils/Settings.hpp"

#include <cmath>
#include "battle/Commander.hpp"
#include "utils/DataLoader.hpp"
#include "utils/Rng.hpp"
#include <algorithm>

const std::vector<Encounter>& RunState::path() {
    // Difficulty climbs three ways: reactor size, how many titans the enemy
    // deck holds, and how sharply their two cores combo. The order also walks
    // the player through the doctrines they will have to answer.
    // Ordered by measured difficulty, not by vibe. SanctumDuelSim reports the
    // win rate against each of these; the first ordering put the doctrine the
    // simulator finds hardest (Dragoon/Paladin, 48%) in slot four and the one
    // it finds easiest (Inquisitor, 87%) in the boss slot, so the run got
    // easier at the end. Reactor size and titan count climb alongside.
    // Ordered by measured difficulty, not by vibe. SanctumDuelSim reports a
    // win rate against each of these and the order follows it: the pairing the
    // simulator finds hardest (Dragoon/Paladin, the only one that ever held the
    // player under 50%) is the boss, and reactor size and titan count climb
    // alongside it.
    // Ordered by measured difficulty, not by vibe. SanctumDuelSim reports the
    // win rate against each of these and the order follows it.
    //
    // Inquisitor appears only as a splash. As an enemy primary the simulator
    // put the player at 97-99% - it is the one doctrine the AI cannot pilot
    // into a threat, and a boss slot is the wrong place to find that out.
    static const std::vector<Encounter> encounters = {
        // Ordered by measured difficulty, not by theme, and every number here
        // came out of SanctumDuelSim rather than a guess.
        //
        // One caveat that matters when reading those numbers: the simulator
        // always plays Vanguard/Valkyrie, which the pairing sweep says is the
        // strongest deck in the game. So this order is tuned against a wall-and-
        // sustain player, and a Siege commander - whose artillery that deck
        // shuts off by plugging lanes - cannot open the campaign.
        //
        // Melee losing its free reach across the row rewrote which doctrines
        // are hard to face. A Guard wall that can only be chipped one lane at a
        // time, backed by artillery that shells the reactor over the top of it,
        // turned Vanguard/Siege from the opening fight into the hardest one on
        // the path - so it moved from first to last.
        //
        // The reactor and titan ladder is deliberately shallow. Under the old
        // rules an extra titan was worth a few points; under these it swung one
        // fight by thirty, and a ladder that steep is a cliff, not a curve.
        // Ordered by MEASURED difficulty. report_opponent_difficulty() in the
        // duel sim prints how hard every one of the thirty pairings is to face,
        // averaged over three reference decks, and the pairings here walk down
        // that list.
        //
        // After the Vanguard and Inquisitor pass the whole table sits inside a
        // 23-55% band - every doctrine is viable, which is the point, but it
        // also means the DOCTRINE can no longer carry the difficulty curve on
        // its own. So the reactor and the titan count do the heavy lifting here,
        // and they climb steeply. That is the opposite of the previous ladder,
        // where one doctrine was strong enough to be a boss at any health.
        //
        // The opening fight fields no titan at all, and that alone is worth
        // roughly thirty points - hence the large reactor on the easiest fight.
        { "Relaybreaker Gunline", "Its guns silenced the relay towers",
          MechRole::Siege, MechRole::Dragoon,          56, 0, DeckRules::kDeckSize, false,
          "assets/portraits/commander_choirbreaker.png" },

        { "The Pale Revenant", "She feeds her own frames to the core",
          MechRole::Valkyrie, MechRole::Vanguard,      34, 1, DeckRules::kDeckSize, false,
          "assets/portraits/commander_pale_sister.png" },

        { "Magistrate Vhal", "Her audit strips your frames while they still stand",
          MechRole::Inquisitor, MechRole::Paladin,     38, 1, DeckRules::kDeckSize, false,
          "assets/portraits/commander_vhal.png" },

        { "Marshal Kaine", "Thunderlance doctrine, no survivors",
          MechRole::Dragoon, MechRole::Siege,          46, 1, DeckRules::kDeckSize, false,
          "assets/portraits/commander_kaine.png" },

        { "Warden AX-7", "The line that has never been broken",
          MechRole::Vanguard, MechRole::Valkyrie,      50, 2, DeckRules::kDeckSize, true,
          "assets/portraits/commander_warden.png" },
    };
    return encounters;
}

void RunState::startNewRun(MechRole primary, MechRole secondary) {
    m_encounter = 0;
    m_commanderHp = playerReactorCap();
    // The player's own deck for this pair when they have built one, otherwise
    // the generated deck. configurationFor() falls back on its own if a saved
    // deck no longer passes the rules.
    m_config = DeckStore::configurationFor(primary, secondary);
}

int RunState::playerReactorCap() {
    return std::max(10, Commander::kStartingHp + Settings::get().playerReactorBonus());
}

int RunState::reactorFor(const Encounter& encounter) {
    // Floored so Recruit cannot make a fight trivial.
    return std::max(12, static_cast<int>(
        std::lround(encounter.commanderHp * Settings::get().enemyReactorScale())));
}

const Encounter& RunState::currentEncounter() const {
    const auto& encounters = path();
    const size_t idx = static_cast<size_t>(std::clamp(m_encounter, 0, kEncounters - 1));
    return encounters[std::min(idx, encounters.size() - 1)];
}

std::vector<CardData> RunState::buildBattleDeck() const {
    return m_config.shuffled();
}

std::vector<CardData> RunState::buildOpponentDeck() const {
    return buildDeckFor(currentEncounter());
}

std::vector<CardData> RunState::buildDeckFor(const Encounter& encounter) {
    // The enemy plays by the same Dual-Core Protocol the player does.
    DeckConfiguration config = DeckBuilder::build(encounter.primary, encounter.secondary);
    std::vector<CardData> deck = config.cards;

    const auto isTitan = [](const CardData& c) { return c.tier == CardTier::Tier3; };

    // Early commanders field no titan at all; later ones bring more than one.
    if (encounter.extraTitans == 0) {
        deck.erase(std::remove_if(deck.begin(), deck.end(), isTitan), deck.end());
    } else {
        for (const CardData& card : DeckBuilder::cardsForRole(encounter.primary)) {
            if (!isTitan(card)) continue;
            for (int i = 0; i < encounter.extraTitans; ++i) deck.push_back(card);
        }
    }

    // Top the deck back up from the primary core, titans excluded so the count
    // above is exact.
    const std::vector<CardData> filler = DeckBuilder::cardsForRole(encounter.primary);
    for (size_t i = 0; static_cast<int>(deck.size()) < encounter.deckSize && !filler.empty(); ++i) {
        const CardData& card = filler[i % filler.size()];
        if (isTitan(card)) continue;
        deck.push_back(card);
    }

    // Trim from the back of the NON-titan cards. A plain resize() cut exactly
    // the tail the extra titans had just been appended to, so a commander set
    // up with three of them shipped with one and the difficulty dial did
    // nothing at all.
    while (static_cast<int>(deck.size()) > encounter.deckSize) {
        auto victim = std::find_if(deck.rbegin(), deck.rend(),
                                   [&](const CardData& c) { return !isTitan(c); });
        if (victim == deck.rend()) break;   // nothing but titans left to cut
        deck.erase(std::next(victim).base());
    }

    std::shuffle(deck.begin(), deck.end(), Rng::engine());
    return deck;
}

std::vector<CardData> RunState::rollRewards() const {
    std::vector<CardData> pool =
        DataLoader::rewardPool(m_config.primaryRole, m_config.secondaryRole);
    if (pool.empty()) return {};

    std::shuffle(pool.begin(), pool.end(), Rng::engine());

    // Three different cards; later fights are allowed to offer the titan.
    std::vector<CardData> offers;
    for (const CardData& card : pool) {
        if (static_cast<int>(offers.size()) >= kRewardChoices) break;
        if (card.tier == CardTier::Tier3 && m_encounter < 2) continue;
        const bool duplicate = std::any_of(offers.begin(), offers.end(),
            [&](const CardData& c) { return c.id == card.id; });
        if (!duplicate) offers.push_back(card);
    }

    // If the filters were too strict, top up from anything left.
    for (const CardData& card : pool) {
        if (static_cast<int>(offers.size()) >= kRewardChoices) break;
        const bool duplicate = std::any_of(offers.begin(), offers.end(),
            [&](const CardData& c) { return c.id == card.id; });
        if (!duplicate) offers.push_back(card);
    }
    return offers;
}

void RunState::purgeCardAt(size_t index) {
    if (!canPurge()) return;
    if (index >= m_config.cards.size()) return;
    m_config.cards.erase(m_config.cards.begin() + static_cast<std::ptrdiff_t>(index));
}

void RunState::winEncounter(const CardData& chosen) {
    if (!chosen.id.empty()) m_config.cards.push_back(chosen);
    // Difficulty tunes the repair between fights as well as the enemy reactor:
    // on Recruit the back half of the campaign is survivable, on Warlord the
    // damage you take in the front half still costs you later.
    const int repair = kHealBetweenFights + Settings::get().bonusRepair();
    m_commanderHp = std::clamp(m_commanderHp + repair, 1, playerReactorCap());
    ++m_encounter;
}
