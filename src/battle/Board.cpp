#include "battle/Board.hpp"

Board::Line& Board::line(Side side, BoardLine which) {
    return which == BoardLine::Frontline ? m_frontline[index(side)] : m_support[index(side)];
}

const Board::Line& Board::line(Side side, BoardLine which) const {
    return which == BoardLine::Frontline ? m_frontline[index(side)] : m_support[index(side)];
}

int Board::freeSlot(Side side, BoardLine which) const {
    const Line& row = line(side, which);
    for (int i = 0; i < kLineSlots; ++i) {
        if (!row[static_cast<size_t>(i)]) return i;
    }
    return -1;
}

Unit* Board::at(Side side, BoardLine which, int slot) {
    if (slot < 0 || slot >= kLineSlots) return nullptr;
    return line(side, which)[static_cast<size_t>(slot)].get();
}

const Unit* Board::at(Side side, BoardLine which, int slot) const {
    if (slot < 0 || slot >= kLineSlots) return nullptr;
    return line(side, which)[static_cast<size_t>(slot)].get();
}

bool Board::place(Side side, BoardLine which, int slot, std::unique_ptr<Unit> unit) {
    if (slot < 0 || slot >= kLineSlots || !unit) return false;
    auto& cell = line(side, which)[static_cast<size_t>(slot)];
    if (cell) return false;
    cell = std::move(unit);
    return true;
}

std::unique_ptr<Unit> Board::take(Side side, BoardLine which, int slot) {
    if (slot < 0 || slot >= kLineSlots) return nullptr;
    return std::move(line(side, which)[static_cast<size_t>(slot)]);
}

std::vector<Unit*> Board::units(Side side) {
    std::vector<Unit*> result;
    for (BoardLine which : { BoardLine::Frontline, BoardLine::Support }) {
        for (auto& slot : line(side, which)) {
            if (slot && slot->isAlive()) result.push_back(slot.get());
        }
    }
    return result;
}

std::vector<const Unit*> Board::units(Side side) const {
    std::vector<const Unit*> result;
    for (BoardLine which : { BoardLine::Frontline, BoardLine::Support }) {
        for (const auto& slot : line(side, which)) {
            if (slot && slot->isAlive()) result.push_back(slot.get());
        }
    }
    return result;
}

std::vector<Unit*> Board::unitsIn(Side side, BoardLine which) {
    std::vector<Unit*> result;
    for (auto& slot : line(side, which)) {
        if (slot && slot->isAlive()) result.push_back(slot.get());
    }
    return result;
}

std::vector<const Unit*> Board::unitsIn(Side side, BoardLine which) const {
    std::vector<const Unit*> result;
    for (const auto& slot : line(side, which)) {
        if (slot && slot->isAlive()) result.push_back(slot.get());
    }
    return result;
}

std::vector<Unit*> Board::allUnits() {
    std::vector<Unit*> result;
    for (Side side : { Side::Player, Side::Opponent }) {
        for (Unit* unit : units(side)) result.push_back(unit);
    }
    return result;
}

Unit* Board::findById(int instanceId) {
    for (Side side : { Side::Player, Side::Opponent }) {
        for (BoardLine which : { BoardLine::Frontline, BoardLine::Support }) {
            for (auto& slot : line(side, which)) {
                if (slot && slot->instanceId == instanceId) return slot.get();
            }
        }
    }
    return nullptr;
}

const Unit* Board::findById(int instanceId) const {
    return const_cast<Board*>(this)->findById(instanceId);
}

UnitLocation Board::locate(int instanceId) const {
    for (Side side : { Side::Player, Side::Opponent }) {
        for (BoardLine which : { BoardLine::Frontline, BoardLine::Support }) {
            const Line& row = line(side, which);
            for (int i = 0; i < kLineSlots; ++i) {
                const auto& slot = row[static_cast<size_t>(i)];
                if (slot && slot->instanceId == instanceId) {
                    return { side, which, i };
                }
            }
        }
    }
    return {};
}

bool Board::lineEmpty(Side side, BoardLine which) const {
    for (const auto& slot : line(side, which)) {
        if (slot && slot->isAlive()) return false;
    }
    return true;
}

bool Board::sideEmpty(Side side) const {
    return lineEmpty(side, BoardLine::Frontline) && lineEmpty(side, BoardLine::Support);
}

int Board::unitCount(Side side) const {
    return static_cast<int>(units(side).size());
}

std::vector<Unit> Board::collectDead() {
    std::vector<Unit> dead;
    for (Side side : { Side::Player, Side::Opponent }) {
        for (BoardLine which : { BoardLine::Frontline, BoardLine::Support }) {
            for (auto& slot : line(side, which)) {
                if (slot && !slot->isAlive()) {
                    dead.push_back(*slot);
                    slot.reset();
                }
            }
        }
    }
    return dead;
}

void Board::clear() {
    for (int s = 0; s < 2; ++s) {
        for (auto& slot : m_frontline[s]) slot.reset();
        for (auto& slot : m_support[s]) slot.reset();
        m_traps[s].clear();
    }
}
