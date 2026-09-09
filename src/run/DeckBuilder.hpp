#pragma once

#include "battle/CardData.hpp"
#include <string>
#include <vector>

/**
 * @brief The Dual-Core Protocol: every deck is exactly two doctrines.
 *
 *   Primary core    - the commander's passive comes from it, it may field that
 *                     role's Tier 3 titan, and it must supply most of the deck.
 *   Secondary core  - a tactical splash: Tier 1, Tier 2, spells and traps only,
 *                     never its titan.
 *
 * The rules live here rather than inside the deck-building screen so the AI,
 * the campaign and the tests all validate decks the same way.
 */
struct DeckRules {
    // 24, not 40. At forty cards a run's deck was larger than any duel could
    // draw through, so which cards you owned barely changed what you saw: every
    // game played the same smear of the whole pool. Twenty-four is small enough
    // that adding or cutting one card is a decision the next duel notices, and
    // small enough that fatigue is a real clock in a long game.
    static constexpr int kDeckSize = 24;
    static constexpr int kMinPrimary = 15;
    static constexpr int kMaxSecondary = kDeckSize - kMinPrimary;   // 9
};

/// Why a deck was rejected, so the UI can say something specific.
enum class DeckError {
    Ok,
    WrongSize,
    SameRoleTwice,
    ForeignCard,          // a card from neither chosen role
    SecondaryTitan,       // a Tier 3 from the secondary core
    TooFewPrimary,
    TooManyCopies         // more copies of a card than the catalogue allows
};

const char* toString(DeckError error);

struct DeckConfiguration {
    MechRole primaryRole = MechRole::Vanguard;
    MechRole secondaryRole = MechRole::Siege;
    std::vector<CardData> cards;

    DeckError validate() const;
    bool isValidDeck() const { return validate() == DeckError::Ok; }

    int countFor(MechRole role) const;
    /// A shuffled copy, ready to hand to the duel engine.
    std::vector<CardData> shuffled() const;
};

namespace DeckBuilder {

/// Every card in the catalogue belonging to a role.
std::vector<CardData> cardsForRole(MechRole role);

/// True when this card may legally appear in the secondary half of a deck.
bool allowedAsSecondary(const CardData& card);

/**
 * @brief Assemble a legal deck of DeckRules::kDeckSize cards from two roles.
 *
 * Fills the primary quota first, in catalogue order and honouring each card's
 * `deckCount`, then tops up from the secondary. Returns an empty vector when
 * the two roles cannot produce a legal deck at all.
 */
DeckConfiguration build(MechRole primary, MechRole secondary);

/// The pairings the campaign offers, ordered so the tutorial run is gentle.
const std::vector<std::pair<MechRole, MechRole>>& suggestedPairings();

} // namespace DeckBuilder
