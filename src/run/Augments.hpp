#pragma once

#include "battle/CardData.hpp"

#include <string>
#include <vector>

/**
 * @brief Core Augments: permanent upgrades picked between fights.
 *
 * A five-fight run changed in only two ways: four cards added and four removed.
 * That is a lot of decisions about the *contents* of a deck and none at all
 * about how it plays. An augment is the other kind of decision - it changes a
 * rule for the rest of the run, so the same twenty-four cards behave
 * differently.
 *
 * They are offered after fights 2 and 4 only. Every fight would make them the
 * main progression and the cards an afterthought; two makes them the thing you
 * remember about a run.
 *
 * Each one is read by the duel at a single named place rather than being a
 * general effect system. Six augments with six call sites is far less to get
 * wrong than a scripting layer, and every one of them is measurable.
 */
namespace Augments {

enum class Id {
    None,
    HydraulicStabilizers,   // Guard frames: +1 max health and +1 when they retaliate
    CapacitorOverdrive,     // the first card you deploy each turn costs 1 less
    ThermalRecycler,        // a counter that fires refunds 1 energy next turn
    ReinforcedPlating,      // your frontline takes 1 less from ranged and aerial
    SalvageProtocol,        // you draw an extra card on the turn a frame of yours dies
    OverclockedCore,        // +2 starting energy cap for the rest of the run
};

struct Augment {
    Id id = Id::None;
    const char* name = "";
    const char* text = "";
};

/// Every augment in the game, in a stable order.
const std::vector<Augment>& all();
const Augment& get(Id id);

/// Three distinct augments the player does not already hold.
std::vector<Id> roll(const std::vector<Id>& owned);

/// The token stored in a save file, and its inverse.
const char* toString(Id id);
Id fromString(const std::string& text);

} // namespace Augments
