#pragma once

#include <SFML/Graphics.hpp>
#include <string>

/**
 * @brief The end-of-duel sequence: a scripted four-phase animation.
 *
 *   0.00 - 0.50  Critical glitch. The board convulses, a red alert strobes,
 *                and the dying commander throws sparks.
 *   0.50 - 1.20  The board dims behind a rising scrim.
 *   1.20 - 2.00  The verdict slams in from 3x scale and punches the camera.
 *   2.00 +       The buttons slide up and fade in.
 *
 * This class owns the timeline and the typography only. Sparks and the impact
 * shockwave are spawned by the caller through CombatVFX, because a second
 * particle system next to the one already running would be two things to tune
 * and two things to get wrong.
 *
 * The camera punch is published as an *offset*, never as a view centre: the
 * engine letterboxes through a fitted sf::View, and writing setCenter on it
 * drops the viewport and stretches the board.
 */
class EndScreen {
public:
    enum class Outcome { Defeat, Victory };

    void setFont(const sf::Font& font) { m_font = &font; }

    /// Start the sequence. `focus` is where the losing reactor sat, so the
    /// caller knows where to throw sparks.
    void begin(Outcome outcome, const std::string& headline, const std::string& subtitle);
    void reset() { m_active = false; m_time = 0.0f; }

    void update(float dt);
    /// The alert strobe and the dimming scrim, on their own. Draw this, then
    /// anything that belongs over the dimmed board, then render().
    void renderScrim(sf::RenderTarget& target);
    /// The verdict itself. Draw after the board, before the buttons.
    void render(sf::RenderTarget& target);

    bool active() const { return m_active; }
    float elapsed() const { return m_time; }
    Outcome outcome() const { return m_outcome; }

    /// Camera displacement for this frame; zero once the sequence settles.
    sf::Vector2f shakeOffset() const { return m_shake; }

    /// True on the single frame the verdict lands, so the caller can fire a
    /// shockwave. Reading it clears it.
    bool consumeImpact();
    /// True while phase 1 is still throwing sparks.
    bool sparking() const { return m_active && m_time < kGlitchEnd; }

    /// 0 before the buttons appear, ramping to 1 as they slide into place.
    float uiReveal() const;
    /// How far the buttons still have to travel, in design-space pixels.
    float uiSlide() const { return (1.0f - uiReveal()) * 34.0f; }

    /// Horizontal tear applied to the verdict, in design pixels. Non-zero on
    /// a defeat only, and only while the signal is still failing.
    float textTear() const;

    static constexpr float kGlitchEnd = 0.50f;
    static constexpr float kDimEnd    = 1.20f;
    static constexpr float kSlamEnd   = 2.00f;
    static constexpr float kUiRamp    = 0.45f;

private:
    const sf::Font* m_font = nullptr;
    Outcome m_outcome = Outcome::Defeat;
    std::string m_headline;
    std::string m_subtitle;

    bool m_active = false;
    float m_time = 0.0f;
    bool m_impactPending = false;
    bool m_impactFired = false;
    sf::Vector2f m_shake;
};
