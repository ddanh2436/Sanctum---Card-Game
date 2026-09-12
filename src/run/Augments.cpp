#include "run/Augments.hpp"

#include "utils/Rng.hpp"

#include <algorithm>

namespace Augments {

const std::vector<Augment>& all() {
    static const std::vector<Augment> list = {
        { Id::HydraulicStabilizers, "Hydraulic Stabilizers",
          "Guard frames gain +1 max health and hit back 1 harder." },
        { Id::CapacitorOverdrive, "Capacitor Overdrive",
          "The first frame or operation you play each turn costs 1 less." },
        { Id::ThermalRecycler, "Thermal Recycler",
          "Every counter-protocol that fires refunds you 1 energy." },
        { Id::ReinforcedPlating, "Reinforced Plating",
          "Your frontline takes 1 less from ranged and aerial attacks." },
        { Id::SalvageProtocol, "Salvage Protocol",
          "The first frame you lose each turn draws you a card." },
        { Id::OverclockedCore, "Overclocked Core",
          "Your energy cap rises 2 higher for the rest of the run." },
    };
    return list;
}

const Augment& get(Id id) {
    for (const Augment& augment : all()) {
        if (augment.id == id) return augment;
    }
    static const Augment none{ Id::None, "None", "" };
    return none;
}

std::vector<Id> roll(const std::vector<Id>& owned) {
    std::vector<Id> pool;
    for (const Augment& augment : all()) {
        if (std::find(owned.begin(), owned.end(), augment.id) == owned.end()) {
            pool.push_back(augment.id);
        }
    }
    // Shuffle then take three. With six augments and at most two already held
    // there is always something to offer, but the guard costs nothing.
    for (std::size_t i = pool.size(); i > 1; --i) {
        std::swap(pool[i - 1], pool[static_cast<std::size_t>(Rng::range(0, static_cast<int>(i) - 1))]);
    }
    if (pool.size() > 3) pool.resize(3);
    return pool;
}

const char* toString(Id id) {
    switch (id) {
    case Id::HydraulicStabilizers: return "HydraulicStabilizers";
    case Id::CapacitorOverdrive:   return "CapacitorOverdrive";
    case Id::ThermalRecycler:      return "ThermalRecycler";
    case Id::ReinforcedPlating:    return "ReinforcedPlating";
    case Id::SalvageProtocol:      return "SalvageProtocol";
    case Id::OverclockedCore:      return "OverclockedCore";
    case Id::None:                 break;
    }
    return "None";
}

Id fromString(const std::string& text) {
    for (const Augment& augment : all()) {
        if (text == toString(augment.id)) return augment.id;
    }
    return Id::None;
}

} // namespace Augments
