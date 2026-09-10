#include "core/Engine.hpp"

#include "utils/Fonts.hpp"
#include "utils/AudioManager.hpp"
#include "utils/Settings.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

// Declared in StateManager.cpp
extern std::unique_ptr<GameState> createInitialMenuState(StateManager& sm, const sf::Font& font);

Engine::Engine() = default;

// =============================================================================
// Window and camera
// =============================================================================

void Engine::openWindow(bool fullscreen) {
    Settings& settings = Settings::get();
    m_fullscreen = fullscreen;

    if (fullscreen) {
        // Take the desktop mode rather than a guessed one: it is always valid
        // and avoids a mode switch, which keeps alt-tabbing sane.
        m_window.create(sf::VideoMode::getDesktopMode(), "Sanctum: Mecha-Chivalry",
                        sf::Style::Fullscreen);
    } else {
        m_window.create(sf::VideoMode(static_cast<unsigned>(settings.windowWidth),
                                      static_cast<unsigned>(settings.windowHeight)),
                        "Sanctum: Mecha-Chivalry",
                        sf::Style::Close | sf::Style::Titlebar | sf::Style::Resize);
    }

    m_window.setVerticalSyncEnabled(settings.vsync);
    m_vsyncApplied = settings.vsync;
    if (!settings.vsync) m_window.setFramerateLimit(120);
    m_window.setKeyRepeatEnabled(false);
    applyLetterbox();
}

void Engine::applyLetterbox() {
    const sf::Vector2u size = m_window.getSize();
    if (size.x == 0 || size.y == 0) return;

    const float windowAspect = static_cast<float>(size.x) / static_cast<float>(size.y);
    const float designAspect = kDesignWidth / kDesignHeight;

    sf::View view(sf::FloatRect(0.0f, 0.0f, kDesignWidth, kDesignHeight));

    // Fit the design space inside the window and centre it; the leftover strip
    // on one axis becomes the letterbox bar.
    float vpWidth = 1.0f, vpHeight = 1.0f;
    if (windowAspect > designAspect) {
        vpWidth = designAspect / windowAspect;      // pillarbox: bars left and right
    } else {
        vpHeight = windowAspect / designAspect;     // letterbox: bars top and bottom
    }
    view.setViewport({ (1.0f - vpWidth) / 2.0f, (1.0f - vpHeight) / 2.0f, vpWidth, vpHeight });

    m_window.setView(view);
}

void Engine::toggleFullscreen() {
    Settings& settings = Settings::get();
    settings.fullscreen = !m_fullscreen;
    settings.save();
    openWindow(settings.fullscreen);
}

// =============================================================================
// Start-up
// =============================================================================

bool Engine::loadDefaultFont() {
    // The faces themselves, and the reasoning behind the split, live in Fonts.
    // The engine keeps a copy of the display face because that is what every
    // state is handed as "the font"; small text and numbers go to Fonts::ui()
    // at the point they are drawn.
    if (!Fonts::load()) return false;
    m_font = Fonts::display();
    return true;
}

bool Engine::init() {
    Settings& settings = Settings::get();
    settings.load();

    openWindow(settings.fullscreen);
    if (!m_window.isOpen()) {
        std::cerr << "[Engine] Error: could not open a window!\n";
        return false;
    }

    if (!loadDefaultFont()) return false;

    AudioManager::get().applySettings();
    // The intro plays through once on launch, then the menu loop takes over.
    AudioManager::get().playMusicCue(AudioManager::Cue::Intro, false);
    AudioManager::get().queueMusicCue(AudioManager::Cue::MusicMenu);

    m_stateManager.changeState(createInitialMenuState(m_stateManager, m_font));
    m_isRunning = true;
    return true;
}

// =============================================================================
// Main loop
// =============================================================================

void Engine::run() {
    while (m_window.isOpen() && m_isRunning) {
        float dt = m_clock.restart().asSeconds();
        if (dt > 0.1f) dt = 0.1f;      // stop a hitch from teleporting animations

        processEvents();
        update(dt);

        if (m_stateManager.isQuitRequested()) {
            Settings::get().save();
            m_window.close();
            m_isRunning = false;
            break;
        }

        render();
    }
    Settings::get().save();
}

void Engine::processEvents() {
    sf::Event event;
    while (m_window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            m_window.close();
            m_isRunning = false;
        }
        else if (event.type == sf::Event::Resized) {
            applyLetterbox();
            if (!m_fullscreen) {
                Settings& settings = Settings::get();
                settings.windowWidth = static_cast<int>(event.size.width);
                settings.windowHeight = static_cast<int>(event.size.height);
            }
        }
        else if (event.type == sf::Event::KeyPressed) {
            if (event.key.code == sf::Keyboard::F11 ||
                (event.key.code == sf::Keyboard::Enter && event.key.alt)) {
                toggleFullscreen();
                continue;   // the window was recreated; this event is spent
            }
            if (event.key.code == sf::Keyboard::F12) {
                saveScreenshot();
            }
        }
        m_stateManager.handleEvent(event, m_window);
    }
}

void Engine::update(float dt) {
    m_stateManager.update(dt);
    // Lets a queued track take over when the one before it finishes.
    AudioManager::get().update();

    // The settings screen only edits the Settings struct; the engine owns the
    // window, so it picks up display changes here rather than through a
    // back-reference from the UI into the engine.
    Settings& settings = Settings::get();
    if (settings.fullscreen != m_fullscreen) {
        openWindow(settings.fullscreen);
    } else if (settings.vsync != m_vsyncApplied) {
        m_vsyncApplied = settings.vsync;
        m_window.setVerticalSyncEnabled(settings.vsync);
        m_window.setFramerateLimit(settings.vsync ? 0 : 120);
    }
}

void Engine::render() {
    // Clear the whole window, bars included, then draw through the letterbox.
    m_window.clear(sf::Color(6, 5, 9));
    m_stateManager.render(m_window);
    m_window.display();
}

void Engine::saveScreenshot() {
    sf::Texture texture;
    if (!texture.create(m_window.getSize().x, m_window.getSize().y)) {
        std::cerr << "[Engine] Could not create texture for screenshot\n";
        return;
    }
    texture.update(m_window);

    std::error_code ec;
    std::filesystem::create_directories("screenshots", ec);

    char name[64];
    std::snprintf(name, sizeof(name), "screenshots/shot_%03d.png", m_screenshotCounter++);
    if (texture.copyToImage().saveToFile(name)) {
        std::cout << "[Engine] Saved " << name << "\n";
    }
}
