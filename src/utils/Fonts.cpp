#include "utils/Fonts.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

sf::Font g_display;
sf::Font g_ui;
bool g_loaded = false;

/// First candidate that exists and parses. Existence is checked first so SFML
/// does not log an error for every miss.
bool loadFirst(sf::Font& into, const std::vector<std::string>& candidates,
               const char* role) {
    for (const std::string& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        if (into.loadFromFile(path)) {
            std::cout << "[Fonts] " << role << ": " << path << "\n";
            return true;
        }
    }
    return false;
}

} // namespace

namespace Fonts {

bool load() {
    if (g_loaded) return true;

    // A .ttf in assets/fonts always wins, so the look can be changed without a
    // rebuild. Georgia is kept at the end of the display list as a last resort
    // only - it is the face this split exists to get away from.
    const bool haveDisplay = loadFirst(g_display, {
        "assets/fonts/Display.ttf",
        "assets/fonts/Cinzel-Bold.ttf",
        "C:/Windows/Fonts/constan.ttf",     // Constantia - screen serif, full Vietnamese
        "C:/Windows/Fonts/pala.ttf",        // Palatino Linotype
        "C:/Windows/Fonts/georgia.ttf",
        "C:/Windows/Fonts/times.ttf",
    }, "display");

    const bool haveUi = loadFirst(g_ui, {
        "assets/fonts/UI.ttf",
        "C:/Windows/Fonts/seguisb.ttf",     // Segoe UI Semibold - holds up light-on-dark
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/verdana.ttf",     // last: legible but wide enough to overrun cards
        "C:/Windows/Fonts/arial.ttf",
    }, "ui");

    // Either one alone is enough to run: whichever loaded stands in for the
    // other, so a machine missing one face gets a plain game rather than none.
    if (!haveDisplay && !haveUi) {
        std::cerr << "[Fonts] Error: could not load any font!\n";
        return false;
    }
    if (!haveDisplay) g_display = g_ui;
    if (!haveUi) g_ui = g_display;

    g_loaded = true;
    return true;
}

const sf::Font& display() { return g_display; }
const sf::Font& ui() { return g_ui; }

} // namespace Fonts
