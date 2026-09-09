#pragma once

#include <SFML/Graphics.hpp>
#include "core/StateManager.hpp"

/**
 * @brief Window, main loop and the letterboxed camera.
 *
 * The whole game is authored against a fixed 1280x720 design space. The engine
 * maps that space onto whatever the real window is with a pillar/letterboxed
 * viewport, so a 16:10 or ultrawide monitor gets bars rather than a stretched
 * board, and every screen coordinate in the game stays valid.
 */
class Engine {
public:
    static constexpr float kDesignWidth = 1280.0f;
    static constexpr float kDesignHeight = 720.0f;

    Engine();
    ~Engine() = default;

    bool init();
    void run();

private:
    sf::RenderWindow m_window;
    StateManager m_stateManager;
    sf::Font m_font;
    sf::Clock m_clock;
    bool m_isRunning = false;
    int m_screenshotCounter = 0;
    bool m_fullscreen = false;
    bool m_vsyncApplied = true;

    void processEvents();
    void update(float dt);
    void render();
    bool loadDefaultFont();
    void saveScreenshot();          // F12

    /// (Re)create the window from the current settings.
    void openWindow(bool fullscreen);
    void toggleFullscreen();
    /// Recompute the letterboxed viewport for the window's current size.
    void applyLetterbox();
};
