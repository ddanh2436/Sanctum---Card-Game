#pragma once

#include "battle/CardData.hpp"
#include "run/Augments.hpp"
#include "run/DeckBuilder.hpp"
#include <string>
#include <vector>

/// One fight on the campaign path.
struct Encounter {
    std::string name;
    std::string subtitle;
    MechRole primary = MechRole::Vanguard;    // the enemy's two cores
    MechRole secondary = MechRole::Siege;
    int commanderHp = 30;      // the enemy commander's reactor for this fight
    int extraTitans = 0;       // additional copies of Tier 3 cards in their deck
    int deckSize = DeckRules::kDeckSize;
    bool isFinal = false;
    /// Portrait for the duel HUD. Drop a file at this path and it is used;
    /// otherwise the shared enemy portrait stands in.
    std::string portrait;
};

/**
 * @brief A campaign run: five duels against rival mech commanders.
 *
 * The player's deck and remaining reactor carry from fight to fight; after each
 * victory they choose one card from three to add. Losing ends the run.
 */
class RunState {
public:
    static constexpr int kEncounters = 5;
    static constexpr int kRewardChoices = 3;
    static constexpr int kHealBetweenFights = 6;

    /// Begin a run with a chosen Dual-Core pairing.
    void startNewRun(MechRole primary, MechRole secondary);

    // --- progress ---
    int getEncounterIndex() const { return m_encounter; }
    int getEncounterNumber() const { return m_encounter + 1; }
    bool isComplete() const { return m_encounter >= kEncounters; }
    const Encounter& currentEncounter() const;
    static const std::vector<Encounter>& path();

    /**
     * The reactor an encounter's commander actually starts on, after the
     * difficulty setting.
     *
     * This lives here rather than in the screen that draws it: the rule has to
     * be identical for the duel, for the map's preview of the next fight, and
     * for the balance simulation. It was in the UI layer first, which meant the
     * simulation measured a difficulty nobody plays on.
     */
    static int reactorFor(const Encounter& encounter);
    /// The player's reactor ceiling for this run, after difficulty.
    static int playerReactorCap();

    // --- Core Augments ---
    /// True when the fight just won is one the augment screen follows. After
    /// fights 2 and 4 only: every fight would make augments the progression and
    /// the cards an afterthought.
    bool augmentOfferDue() const { return m_encounter == 2 || m_encounter == 4; }
    const std::vector<Augments::Id>& getAugments() const { return m_augments; }
    void takeAugment(Augments::Id id) { if (id != Augments::Id::None) m_augments.push_back(id); }

    // --- the player's cores ---
    MechRole getPrimaryRole() const { return m_config.primaryRole; }
    MechRole getSecondaryRole() const { return m_config.secondaryRole; }
    const DeckConfiguration& getConfiguration() const { return m_config; }

    // --- the player's evolving deck ---
    const std::vector<CardData>& getDeck() const { return m_config.cards; }
    std::vector<CardData> buildBattleDeck() const;   // a shuffled copy for the duel
    void addCard(const CardData& card) { m_config.cards.push_back(card); }
    int deckSize() const { return static_cast<int>(m_config.cards.size()); }

    /// The smallest deck a run is allowed to carry. Purging is the point of the
    /// reward screen, but a deck that can be whittled to nothing just decks out
    /// and loses, so the floor sits one card below the starting size: every
    /// fight you may trade a card in for a card out, never shrink past that.
    static constexpr int kMinRunDeck = DeckRules::kDeckSize - 1;
    bool canPurge() const { return deckSize() > kMinRunDeck; }

    /// Scrap one card out of the deck for the rest of the run. Ignored when it
    /// would take the deck below kMinRunDeck, or when the index is out of range.
    void purgeCardAt(size_t index);

    // --- reactor carried between fights ---
    int getCommanderHp() const { return m_commanderHp; }
    void setCommanderHp(int hp) { m_commanderHp = hp; }

    /// The enemy deck for this encounter, scaled to its difficulty.
    std::vector<CardData> buildOpponentDeck() const;

    /// The same construction for any hypothetical commander, not just the one
    /// the run is standing in front of. The balance sweep uses this to measure a
    /// candidate opponent pairing before it is written into the campaign table.
    static std::vector<CardData> buildDeckFor(const Encounter& encounter);

    /// Three distinct cards to choose between after a win.
    std::vector<CardData> rollRewards() const;

    /// Take the reward, repair a little, and step to the next fight.
    void winEncounter(const CardData& chosen);

private:
    int m_encounter = 0;
    int m_commanderHp = 30;
    std::vector<Augments::Id> m_augments;
    DeckConfiguration m_config;
};
