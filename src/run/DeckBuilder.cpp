#include "run/DeckBuilder.hpp"
#include "utils/DataLoader.hpp"
#include "utils/Rng.hpp"
#include <algorithm>
#include <map>

const char* toString(DeckError error) {
    switch (error) {
    case DeckError::Ok:              return "ok";
    case DeckError::WrongSize:       return "a deck must hold exactly 24 cards";
    case DeckError::SameRoleTwice:   return "the two cores must be different doctrines";
    case DeckError::ForeignCard:     return "a card belongs to neither chosen core";
    case DeckError::SecondaryTitan:  return "the secondary core may not field its titan";
    case DeckError::TooFewPrimary:   return "the primary core must supply at least 24 cards";
    case DeckError::TooManyCopies:   return "too many copies of one card";
    }
    return "?";
}

// =============================================================================
// DeckConfiguration
// =============================================================================

DeckError DeckConfiguration::validate() const {
    if (primaryRole == secondaryRole) return DeckError::SameRoleTwice;
    if (static_cast<int>(cards.size()) != DeckRules::kDeckSize) return DeckError::WrongSize;

    int primaryCount = 0;
    std::map<std::string, int> copies;

    for (const CardData& card : cards) {
        if (card.role == primaryRole) {
            ++primaryCount;
        } else if (card.role == secondaryRole) {
            // The splash doctrine lends tools, never its titan.
            if (card.tier == CardTier::Tier3) return DeckError::SecondaryTitan;
        } else {
            return DeckError::ForeignCard;
        }

        const int allowed = std::max(1, card.deckCount);
        if (++copies[card.id] > allowed) return DeckError::TooManyCopies;
    }

    if (primaryCount < DeckRules::kMinPrimary) return DeckError::TooFewPrimary;
    return DeckError::Ok;
}

int DeckConfiguration::countFor(MechRole role) const {
    return static_cast<int>(std::count_if(cards.begin(), cards.end(),
        [role](const CardData& c) { return c.role == role; }));
}

std::vector<CardData> DeckConfiguration::shuffled() const {
    std::vector<CardData> copy = cards;
    std::shuffle(copy.begin(), copy.end(), Rng::engine());
    return copy;
}

// =============================================================================
// DeckBuilder
// =============================================================================

namespace DeckBuilder {

std::vector<CardData> cardsForRole(MechRole role) {
    std::vector<CardData> result;
    for (const CardData& card : DataLoader::catalogue()) {
        if (card.role == role) result.push_back(card);
    }
    return result;
}

bool allowedAsSecondary(const CardData& card) {
    return card.tier != CardTier::Tier3;
}

namespace {

/**
 * @brief The mix a finished deck should hold, as a share of the deck size.
 *
 * These are TARGETS that sum to the deck, not caps that sum to more than it.
 * The previous version was a fill order - units up to a cap, then spells, then
 * counters - and the primary core's quota was large enough to be spent entirely
 * on units before the spell pass ever ran. At forty cards that squeezed the
 * counter zone down to one card; at twenty-four it produced decks of 17 units,
 * 7 spells and ZERO counters, which quietly removed a whole card category from
 * the game. Allocating each category its own share first cannot fail that way.
 *
 * The counter share is pinned rather than scaled: the counter zone holds three
 * at a time, so four is a working set plus one spare no matter how big the deck.
 */
struct CategoryBudget {
    static constexpr int kTraps = 4;
    static constexpr int kSpells = (DeckRules::kDeckSize - kTraps) * 3 / 10;  // 6 at 24
    static constexpr int kUnits = DeckRules::kDeckSize - kTraps - kSpells;    // 14 at 24

    static int targetFor(CardCategory category) {
        switch (category) {
        case CardCategory::Spell: return kSpells;
        case CardCategory::Trap:  return kTraps;
        default:                  return kUnits;
        }
    }
};

static_assert(CategoryBudget::kUnits + CategoryBudget::kSpells + CategoryBudget::kTraps
                  == DeckRules::kDeckSize,
              "the category targets must add up to exactly one deck");

/// Cards of one category from a pool, cheapest first, ties broken by id so a
/// build is reproducible.
std::vector<CardData> ofCategory(const std::vector<CardData>& pool, CardCategory category,
                                 bool secondaryRules) {
    std::vector<CardData> out;
    for (const CardData& card : pool) {
        if (card.category != category) continue;
        if (secondaryRules && !allowedAsSecondary(card)) continue;
        out.push_back(card);
    }
    std::sort(out.begin(), out.end(), [](const CardData& a, const CardData& b) {
        if (a.manaCost != b.manaCost) return a.manaCost < b.manaCost;
        return a.id < b.id;
    });
    return out;
}

/// Take up to `wanted` cards from `pool`, round-robin so copies spread across
/// the pool instead of stacking one card's full allowance first. `used` tracks
/// copies already taken so the primary and secondary passes cannot exceed a
/// card's deckCount between them.
int draw(std::vector<CardData>& deck, const std::vector<CardData>& pool, int wanted,
         std::map<std::string, int>& used) {
    if (wanted <= 0 || pool.empty()) return 0;

    int added = 0;
    for (int round = 0; added < wanted; ++round) {
        bool progressed = false;
        for (const CardData& card : pool) {
            if (added >= wanted) break;
            const int allowed = std::max(1, card.deckCount);
            if (used[card.id] >= allowed) continue;
            deck.push_back(card);
            ++used[card.id];
            ++added;
            progressed = true;
        }
        if (!progressed) break;   // every copy in the pool is spoken for
    }
    return added;
}

} // namespace

DeckConfiguration build(MechRole primary, MechRole secondary) {
    DeckConfiguration config;
    config.primaryRole = primary;
    config.secondaryRole = secondary;
    if (primary == secondary) return config;

    const std::vector<CardData> primaryPool = cardsForRole(primary);
    const std::vector<CardData> secondaryPool = cardsForRole(secondary);

    // Each category gets its own target, and the primary core's share of that
    // target is allocated up front rather than taken first-come.
    //
    // The obvious version - "primary fills each category until it hits its
    // overall quota" - silently drops whichever category is filled last, because
    // units and spells alone already reach the quota. That is what removed every
    // counter-protocol from every deck in the game. Splitting each category
    // between the two cores in proportion cannot starve the last one.
    const CardCategory order[] = { CardCategory::Unit, CardCategory::Spell, CardCategory::Trap };

    // A little above the minimum, so the splash stays a splash.
    const int primaryQuota = DeckRules::kDeckSize - DeckRules::kMaxSecondary + 2;   // 17 at 24

    std::map<std::string, int> used;

    for (CardCategory category : order) {
        const int target = CategoryBudget::targetFor(category);
        // This category's share of the primary's quota, rounded up so small
        // categories are not rounded out of existence entirely.
        const int primaryShare = std::min(
            target, (target * primaryQuota + DeckRules::kDeckSize - 1) / DeckRules::kDeckSize);

        const int fromPrimary = draw(config.cards, ofCategory(primaryPool, category, false),
                                     primaryShare, used);
        const int fromSecondary = draw(config.cards, ofCategory(secondaryPool, category, true),
                                       target - fromPrimary, used);
        // Whatever the splash could not supply either, the primary covers.
        if (fromPrimary + fromSecondary < target) {
            draw(config.cards, ofCategory(primaryPool, category, false),
                 target - fromPrimary - fromSecondary, used);
        }
    }

    // A doctrine with no counter-protocols at all - Paladin, Valkyrie and Siege
    // carry none - cannot fill the counter share, so the shortfall goes back into
    // bodies. A legal deck beats a tidy one.
    for (CardCategory category : order) {
        if (static_cast<int>(config.cards.size()) >= DeckRules::kDeckSize) break;
        draw(config.cards, ofCategory(primaryPool, category, false),
             DeckRules::kDeckSize - static_cast<int>(config.cards.size()), used);
    }
    for (CardCategory category : order) {
        if (static_cast<int>(config.cards.size()) >= DeckRules::kDeckSize) break;
        draw(config.cards, ofCategory(secondaryPool, category, true),
             DeckRules::kDeckSize - static_cast<int>(config.cards.size()), used);
    }

    if (static_cast<int>(config.cards.size()) != DeckRules::kDeckSize) {
        config.cards.clear();
    }
    return config;
}

const std::vector<std::pair<MechRole, MechRole>>& suggestedPairings() {
    // The four combos the design brief calls out, then the rest of the wheel.
    static const std::vector<std::pair<MechRole, MechRole>> pairings = {
        { MechRole::Vanguard,   MechRole::Valkyrie },   // Iron Line
        { MechRole::Dragoon,    MechRole::Paladin },    // Punishing Spearhead
        { MechRole::Inquisitor, MechRole::Siege },      // Trap and Bombard
        { MechRole::Paladin,    MechRole::Vanguard },   // Kinetic Conversion
        { MechRole::Siege,      MechRole::Vanguard },
        { MechRole::Valkyrie,   MechRole::Dragoon },
    };
    return pairings;
}

} // namespace DeckBuilder
