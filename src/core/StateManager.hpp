#pragma once

#include <SFML/Graphics.hpp>
#include <memory>
#include <vector>
#include <string>

enum class GameStateType {
    Menu,
    Map,
    Duel,
    Settings,
    Reward,
    RunOver
};

class GameState {
public:
    virtual ~GameState() = default;
    virtual void handleEvent(const sf::Event& event, const sf::RenderWindow& window) = 0;
    virtual void update(float dt) = 0;
    virtual void render(sf::RenderTarget& target) = 0;

    /// An overlay lets the state underneath keep drawing (settings over a duel).
    virtual bool isOverlay() const { return false; }
};

/**
 * @brief Holds a stack of states so a screen can be opened over another and
 *        closed again without the one underneath losing its progress.
 *
 * Only the top of the stack receives input and updates; every state renders,
 * bottom first, so an overlay draws on top of the live board behind it.
 * Stack changes are deferred to the start of the next update, which keeps a
 * state from destroying itself midway through its own event handler.
 */
class StateManager {
public:
    StateManager() = default;

    /// Replace the entire stack.
    void changeState(std::unique_ptr<GameState> newState);
    /// Open a state on top of the current one.
    void pushState(std::unique_ptr<GameState> newState);
    /// Close the top state and return to the one below.
    void popState();

    void handleEvent(const sf::Event& event, const sf::RenderWindow& window);
    void update(float dt);
    void render(sf::RenderTarget& target);

    void requestQuit() { m_quitRequested = true; }
    bool isQuitRequested() const { return m_quitRequested; }

    size_t depth() const { return m_stack.size(); }

private:
    enum class PendingKind { None, Replace, Push, Pop };

    std::vector<std::unique_ptr<GameState>> m_stack;
    std::unique_ptr<GameState> m_pendingState;
    PendingKind m_pending = PendingKind::None;
    bool m_quitRequested = false;

    void applyPending();
};
