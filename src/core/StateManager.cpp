#include "core/StateManager.hpp"
#include "battle/AICommander.hpp"
#include "battle/DuelEngine.hpp"
#include "rendering/CardArt.hpp"
#include "rendering/CardSpotlight.hpp"
#include "rendering/CombatVFX.hpp"
#include "rendering/MenuBackdrop.hpp"
#include "rendering/EndScreen.hpp"
#include "rendering/DeploySignature.hpp"
#include "rendering/DrawFlight.hpp"
#include "rendering/EndGameVFX.hpp"
#include "rendering/PortraitRig.hpp"
#include "rendering/FloatingText.hpp"
#include "rendering/HolyVFX.hpp"
#include "run/DeckBuilder.hpp"
#include "run/DeckStore.hpp"
#include "run/RunState.hpp"
#include "utils/AudioManager.hpp"
#include "utils/Avatars.hpp"
#include "utils/DataLoader.hpp"
#include "utils/Fonts.hpp"
#include "utils/ResourceManager.hpp"
#include "utils/Settings.hpp"
#include "utils/Rng.hpp"
#include "utils/TextUtils.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <deque>
#include <iostream>
#include <sstream>
#include <unordered_map>

// =============================================================================
// StateManager
// =============================================================================

void StateManager::changeState(std::unique_ptr<GameState> newState) {
    m_pendingState = std::move(newState);
    m_pending = PendingKind::Replace;
}

void StateManager::pushState(std::unique_ptr<GameState> newState) {
    m_pendingState = std::move(newState);
    m_pending = PendingKind::Push;
}

void StateManager::popState() {
    m_pendingState.reset();
    m_pending = PendingKind::Pop;
}

void StateManager::applyPending() {
    switch (m_pending) {
    case PendingKind::Replace:
        m_stack.clear();
        if (m_pendingState) m_stack.push_back(std::move(m_pendingState));
        break;
    case PendingKind::Push:
        if (m_pendingState) m_stack.push_back(std::move(m_pendingState));
        break;
    case PendingKind::Pop:
        if (!m_stack.empty()) m_stack.pop_back();
        break;
    case PendingKind::None:
        return;
    }
    m_pending = PendingKind::None;
    m_pendingState.reset();
}

void StateManager::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    // Only the top of the stack is interactive; anything below is frozen.
    if (!m_stack.empty()) m_stack.back()->handleEvent(event, window);
}

void StateManager::update(float dt) {
    applyPending();
    if (!m_stack.empty()) m_stack.back()->update(dt);
}

void StateManager::render(sf::RenderTarget& target) {
    // Bottom-up, so an overlay is drawn over the screen it opened on.
    for (const auto& state : m_stack) state->render(target);
}

// =============================================================================
// Shared UI pieces
// =============================================================================

namespace {

/// Rectangular button with a hover state, used across every screen.
struct UiButton {
    sf::RectangleShape box;
    sf::Text label;
    bool hovered = false;
    bool enabled = true;
    sf::Color accent;

    void setup(const sf::Font& font, const std::string& text, sf::Vector2f centre,
               sf::Vector2f size, sf::Color accentColour, unsigned int charSize = 18) {
        accent = accentColour;
        box.setSize(size);
        box.setOrigin(size.x / 2.0f, size.y / 2.0f);
        box.setPosition(centre);
        box.setFillColor(sf::Color(16, 13, 10, 215));
        box.setOutlineThickness(1.5f);
        box.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, 190));

        label.setFont(font);
        label.setString(text);
        label.setCharacterSize(charSize);
        label.setLetterSpacing(2.0f);
        label.setFillColor(sf::Color(240, 232, 214));
        TextUtils::centerBoth(label);
        label.setPosition(centre);
    }

    void setText(const std::string& text) {
        label.setString(text);
        const sf::Vector2f centre = box.getPosition();
        TextUtils::centerBoth(label);
        label.setPosition(centre);
    }

    bool contains(sf::Vector2f p) const { return enabled && box.getGlobalBounds().contains(p); }

    void setHovered(bool value) {
        hovered = value && enabled;
        box.setFillColor(hovered ? sf::Color(accent.r / 4 + 26, accent.g / 4 + 20, accent.b / 5 + 12, 235)
                                 : sf::Color(16, 13, 10, 215));
        box.setOutlineThickness(hovered ? 2.5f : 1.5f);
        box.setOutlineColor(hovered ? accent : sf::Color(accent.r, accent.g, accent.b, 190));
    }

    /// `alpha` fades the whole control, and `slide` nudges it down from its
    /// resting place - together they are the phase 4 reveal.
    void render(sf::RenderTarget& target, float alpha = 1.0f, float slide = 0.0f) const {
        if (alpha <= 0.01f) return;
        sf::RectangleShape drawn = box;
        sf::Text text = label;
        if (!enabled) {
            drawn.setOutlineColor(sf::Color(70, 66, 62, 160));
            text.setFillColor(sf::Color(126, 120, 112));
        }
        if (slide != 0.0f) {
            drawn.move(0.0f, slide);
            text.move(0.0f, slide);
        }
        if (alpha < 1.0f) {
            auto fade = [alpha](sf::Color c) {
                return sf::Color(c.r, c.g, c.b, static_cast<sf::Uint8>(c.a * alpha));
            };
            drawn.setFillColor(fade(drawn.getFillColor()));
            drawn.setOutlineColor(fade(drawn.getOutlineColor()));
            text.setFillColor(fade(text.getFillColor()));
        }
        target.draw(drawn);
        target.draw(text);
    }
};

/// A labelled horizontal slider. Values are edited by clicking or dragging
/// anywhere on the track, which is far more forgiving than a thin handle.
struct UiSlider {
    sf::FloatRect track;
    std::string label;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float* value = nullptr;
    bool dragging = false;
    /// Renders the value as a percentage rather than a raw float.
    bool asPercent = true;

    bool hit(sf::Vector2f p) const {
        // Generous vertical target: the whole row, not just the 6px bar.
        return sf::FloatRect(track.left - 6.0f, track.top - 14.0f,
                             track.width + 12.0f, track.height + 28.0f).contains(p);
    }

    void setFromMouse(sf::Vector2f p) {
        if (!value || track.width <= 0.0f) return;
        const float t = std::clamp((p.x - track.left) / track.width, 0.0f, 1.0f);
        *value = minValue + t * (maxValue - minValue);
    }

    float ratio() const {
        if (!value || maxValue <= minValue) return 0.0f;
        return std::clamp((*value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
    }

    std::string readout() const {
        if (!value) return "";
        if (asPercent) return std::to_string(static_cast<int>(std::round(*value * 100.0f))) + "%";
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "x%.1f", *value);
        return buffer;
    }
};

/// A labelled on/off row.
/**
 * @brief One of N named settings, as a segmented row.
 *
 * A toggle cannot say "Recruit / Knight / Warlord" and a slider would show the
 * number rather than the word. Three segments name themselves, which is what a
 * difficulty control has to do: nobody should have to guess whether 2 is harder
 * than 0.
 */
struct UiChoice {
    sf::FloatRect box;              // the whole segmented row
    std::string label;
    int* value = nullptr;
    int count = 3;
    const char* (*nameOf)(int) = nullptr;

    sf::FloatRect segment(int index) const {
        const float w = box.width / static_cast<float>(count);
        return { box.left + index * w, box.top, w, box.height };
    }
    int hit(sf::Vector2f p) const {
        for (int i = 0; i < count; ++i) {
            if (segment(i).contains(p)) return i;
        }
        return -1;
    }
};

struct UiToggle {
    sf::FloatRect box;
    std::string label;
    bool* value = nullptr;

    bool hit(sf::Vector2f p) const {
        return sf::FloatRect(box.left - 6.0f, box.top - 8.0f,
                             box.width + 12.0f, box.height + 16.0f).contains(p);
    }
};

/// Fill the 1280x720 design space with a texture without distorting it: scale
/// uniformly to cover, then crop the overhang evenly. The old code scaled x and
/// y independently, so any source that was not exactly 16:9 came out stretched.
void coverScreen(sf::Sprite& sprite, const sf::Texture& texture) {
    const float tw = static_cast<float>(texture.getSize().x);
    const float th = static_cast<float>(texture.getSize().y);
    if (tw < 1.0f || th < 1.0f) return;

    const float scale = std::max(1280.0f / tw, 720.0f / th);
    const int visW = static_cast<int>(1280.0f / scale);
    const int visH = static_cast<int>(720.0f / scale);
    sprite.setTexture(texture, true);
    sprite.setTextureRect(sf::IntRect((static_cast<int>(tw) - visW) / 2,
                                      (static_cast<int>(th) - visH) / 2,
                                      visW, visH));
    sprite.setScale(scale, scale);
    sprite.setPosition(0.0f, 0.0f);
}

/// Shown in the menu footer. One place to bump it.
constexpr const char* kGameVersion = "v0.9.0-alpha";

void drawPanel(sf::RenderTarget& target, sf::FloatRect bounds, sf::Color fill, sf::Color outline) {
    sf::RectangleShape panel({ bounds.width, bounds.height });
    panel.setPosition(bounds.left, bounds.top);
    panel.setFillColor(fill);
    panel.setOutlineThickness(1.0f);
    panel.setOutlineColor(outline);
    target.draw(panel);
}

/**
 * Small caps-and-spacing text: row captions, pile names, hints, readouts.
 *
 * It takes no font any more. Every one of these is between 8 and 13 pixels
 * tall, and at that size the display serif was the least readable thing on the
 * screen - so they all go through the UI face, and the choice is made here once
 * instead of at fifty call sites.
 */
void drawLabel(sf::RenderTarget& target, const std::string& text,
               sf::Vector2f pos, unsigned int size, sf::Color colour,
               float letterSpacing = 1.0f, bool bold = false) {
    sf::Text label;
    label.setFont(Fonts::ui());
    // Tracking has to come off as the text gets smaller, not go on. These were
    // set at 2.0 to 2.6 across the board, which at 8px pulls the letters far
    // enough apart that the word stops reading as a word.
    if (size <= 10) letterSpacing = 1.0f + (letterSpacing - 1.0f) * 0.45f;
    label.setString(text);
    label.setCharacterSize(size);
    label.setLetterSpacing(letterSpacing);
    label.setFillColor(colour);
    if (bold) label.setStyle(sf::Text::Bold);
    label.setPosition(pos);
    target.draw(label);
}

void drawBar(sf::RenderTarget& target, sf::FloatRect bounds, float ratio,
             sf::Color fill, sf::Color track) {
    sf::RectangleShape back({ bounds.width, bounds.height });
    back.setPosition(bounds.left, bounds.top);
    back.setFillColor(track);
    back.setOutlineThickness(1.0f);
    back.setOutlineColor(sf::Color(90, 82, 78, 190));
    target.draw(back);

    sf::RectangleShape front({ bounds.width * std::clamp(ratio, 0.0f, 1.0f), bounds.height });
    front.setPosition(bounds.left, bounds.top);
    front.setFillColor(fill);
    target.draw(front);
}

/// Mana curve shown as pips rather than a number, so the curve is felt.
void drawManaPips(sf::RenderTarget& target, sf::Vector2f start, int available, int cap) {
    for (int i = 0; i < Commander::kMaxManaCap; ++i) {
        sf::CircleShape pip(5.0f);
        pip.setOrigin(5.0f, 5.0f);
        pip.setPosition(start.x + i * 13.0f, start.y);
        if (i < available)      pip.setFillColor(sf::Color(104, 190, 255));
        else if (i < cap)       pip.setFillColor(sf::Color(38, 54, 74));
        else                    pip.setFillColor(sf::Color(24, 24, 30));
        pip.setOutlineThickness(1.0f);
        pip.setOutlineColor(sf::Color(14, 16, 22, 220));
        target.draw(pip);
    }
}

} // namespace

// =============================================================================
// Board layout - one place that maps board coordinates to screen rectangles
// =============================================================================

namespace Layout {

// A KARDS-style board: the HQ card sits behind each side's rows, the two
// frontlines meet on a single thin rule, and empty ground is left empty -
// slots only appear while you are actually holding a card.

constexpr float kCentreX = 640.0f;

// Unit rows.
//
// The tile is sized around its artwork, not the other way round. Card art is
// 4:3; the old 148x78 tile left a 144x47 art window - ratio 3.08 - so coverFit
// threw away well over half the height of every illustration and every frame
// on the board was a letterboxed strip of its own picture. 112x100 with a 0.70
// art fraction gives a 108x70 window - ratio 1.54 against the old 3.08, so
// roughly twice as much of each illustration survives.
//
// The gap went from 10 to 28 for the same reason a Yu-Gi-Oh mat has spaced
// zones: at 10px, four tiles read as one continuous bar and it was not obvious
// where one frame ended and the next began.
constexpr float kUnitW = 112.0f;
constexpr float kUnitH = 100.0f;
constexpr float kUnitGap = 28.0f;
constexpr float kRowW = Board::kLineSlots * kUnitW + (Board::kLineSlots - 1) * kUnitGap;
constexpr float kRowX = kCentreX - kRowW / 2.0f;

// Rows are stacked from the top with 6px between them; every value below is
// derived from kUnitH, so changing the tile height moves the whole board.
constexpr float kEnemySupportY = 62.0f;
constexpr float kEnemyFrontY = 168.0f;
constexpr float kFrontLineY = 274.0f;
constexpr float kPlayerFrontY = 280.0f;
constexpr float kPlayerSupportY = 386.0f;

// Reactor cards, one per side.
//
// They used to sit centred at the top and bottom of the screen, which put the
// only bar that matters at the opposite end of the board from the portrait,
// energy and piles that describe the same commander - four separate readings of
// one thing, spread across the screen.
//
// They are pinned to the left column now, touching their own commander strip:
// the enemy's directly under theirs, the player's directly over theirs. The
// column is 16..284 and the unit rows start at 374, so nothing moved on top of
// a lane. It also moves the drop target for "attack the core" onto the
// commander it belongs to, instead of at a card floating in open ground.
constexpr float kHqW = 268.0f;
constexpr float kHqH = 50.0f;
// 178, not 162: the enemy piles end at 160 and their DECK / SCRAP captions run
// to 172, so a card starting at 162 sat on top of them.
constexpr float kEnemyHqY = 178.0f;
constexpr float kPlayerHqY = 496.0f;

// Trap zone and end turn live in the right margin.
//
// Card-shaped, not a plate: your own counter-protocols are drawn as real cards
// now, so the slot has to carry the same 0.73 proportion a hand card does or the
// art window comes out letterboxed. 94 tall is what fits between the support row
// and the commander strip.
constexpr float kTrapW = 68.0f;
constexpr float kTrapH = 94.0f;
constexpr float kTrapGap = 8.0f;
constexpr float kTrapX = 1000.0f;

inline float rowY(Side side, BoardLine line) {
    if (side == Side::Opponent) {
        return line == BoardLine::Support ? kEnemySupportY : kEnemyFrontY;
    }
    return line == BoardLine::Frontline ? kPlayerFrontY : kPlayerSupportY;
}

/// The whole strip a row occupies. Dropping anywhere on it deploys to that row,
/// which is far kinder than asking the player to hit one exact cell.
inline sf::FloatRect rowBand(Side side, BoardLine line) {
    return { kRowX - 8.0f, rowY(side, line) - 4.0f, kRowW + 16.0f, kUnitH + 8.0f };
}

/// Where slot `index` of a row is drawn. Fixed cells, not packed: a frame's
/// column is now part of the rules - melee reaches one lane either side, and a
/// Guard only screens the frames beside it - so a half-empty row has to show
/// the gaps rather than close them up.
inline sf::FloatRect unitRect(Side side, BoardLine line, int slot) {
    return { kRowX + slot * (kUnitW + kUnitGap), rowY(side, line), kUnitW, kUnitH };
}

/// Ghost cell shown while dragging, so the player sees where the unit lands.
inline sf::FloatRect ghostRect(Side side, BoardLine line, int slot) {
    return unitRect(side, line, slot);
}

inline sf::FloatRect hqCard(Side side) {
    const float y = side == Side::Opponent ? kEnemyHqY : kPlayerHqY;
    return { 16.0f, y, kHqW, kHqH };
}

inline sf::FloatRect trapSlot(Side side, int index) {
    const float y = side == Side::Opponent ? kEnemySupportY + 8.0f : kPlayerSupportY + 8.0f;
    return { kTrapX + index * (kTrapW + kTrapGap), y, kTrapW, kTrapH };
}

inline sf::FloatRect trapZone(Side side) {
    const float y = side == Side::Opponent ? kEnemySupportY + 8.0f : kPlayerSupportY + 8.0f;
    return { kTrapX - 8.0f, y - 8.0f,
             Board::kTrapSlots * kTrapW + (Board::kTrapSlots - 1) * kTrapGap + 16.0f,
             kTrapH + 16.0f };
}

/// Commander strips are pinned to the corners rather than floating beside the
/// rows: the enemy reads top-left, the player bottom-left, so a glance at one
/// edge of the screen answers "whose turn, how much energy, how many cards".
/// 240 wide, not 250, because the leftmost hand card reaches x 254.
inline sf::FloatRect commanderPanel(Side side) {
    return side == Side::Opponent ? sf::FloatRect{ 16.0f, 8.0f, 240.0f, 150.0f }
                                  : sf::FloatRect{ 16.0f, 552.0f, 240.0f, 150.0f };
}

// Draw piles sit under their commander. They are real stacks of card backs
// whose height tracks the count, so "am I running out of deck" is answerable
// without reading a number.
constexpr float kPileW = 54.0f;
constexpr float kPileH = 68.0f;

inline sf::FloatRect deckPile(Side side) {
    const float y = side == Side::Opponent ? 92.0f : 636.0f;
    return { 16.0f, y, kPileW, kPileH };
}

inline sf::FloatRect scrapPile(Side side) {
    const float y = side == Side::Opponent ? 92.0f : 636.0f;
    return { 82.0f, y, kPileW, kPileH };
}

/// The enemy's hand, face-down along the top edge. A number in a box never
/// conveyed "they are holding a full grip"; eight card backs do.
inline sf::FloatRect enemyHandFan() {
    return { 812.0f, 2.0f, 208.0f, 66.0f };
}

/// Opens the battle log. Three bars, middle of the left margin.
inline sf::FloatRect logButton() {
    // Was at y 336, in the middle of the left margin - which is the reactor
    // column now. It sits in the right margin instead, in the gap between the
    // enemy counter zone (ends at 164) and the End Turn control (starts at 296).
    return { 1208.0f, 200.0f, 54.0f, 44.0f };
}

} // namespace Layout

// =============================================================================
// Forward declarations
// =============================================================================

class IntroState;
class MenuState;
class RoleSelectState;
class MapState;
class DeckListState;
class DeckEditState;
class DuelState;
class RewardState;
class RunOverState;
class SettingsState;
class CardInspectState;

/// The campaign lives outside the states so it survives every transition.
static RunState g_run;

// =============================================================================
// MENU
// =============================================================================

// =============================================================================
// INTRO - the three-act opening
// =============================================================================

/**
 * @brief The nine-second opening: ruins, oath, crusade, title slam.
 *
 * Three still paintings and one clock. Every move is a transform on a sprite -
 * a Ken Burns push, a slow rise, a shake offset - because a filmed intro would
 * cost more texture memory than the rest of the game together and could not be
 * re-timed afterwards. The whole script lives in the constants below, so the
 * beats can be moved without hunting through the drawing code.
 *
 * The camera never touches sf::View. Shake is added to the sprite position; a
 * setCenter() here would drop the letterbox and stretch the board on the frame
 * the menu takes over.
 *
 * Not skippable while it plays, by request. Once it has finished it waits on
 * a prompt, and moves on by itself if nobody answers.
 */
class IntroState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;
    float m_time = 0.0f;
    bool m_leaving = false;

    // ---- the script, in seconds -------------------------------------------
    static constexpr float kAct1End    = 2.80f;   // ruins hold
    static constexpr float kAct1Fade   = 3.00f;   // fully black
    static constexpr float kAct2End    = 5.80f;   // oath hold
    static constexpr float kAct2Flash  = 6.00f;   // white flash peak
    static constexpr float kSlamAt     = 7.50f;   // the title lands
    static constexpr float kScriptEnd  = 9.00f;
    static constexpr float kAutoGo     = 13.50f;  // moves on if nobody presses
    static constexpr float kHardCap    = 20.0f;

    // ---- sound cues --------------------------------------------------------
    // One cue, on purpose. The bell, the sword draw, the reactor boot and the
    // anvil hit were all cut: over a nine-second piece of music they read as
    // clutter rather than as punctuation. The roar lands with the title, which
    // is the only beat that needs marking.
    static constexpr float kRoarAt = kSlamAt;
    bool m_roar = false;

    // ---- shake -------------------------------------------------------------
    float m_shake = 0.0f;          // current amplitude in design pixels
    sf::Vector2f m_shakeOffset;

    const sf::Texture* m_scene[3] = { nullptr, nullptr, nullptr };

    /// The game's face is Georgia, which is missing ten uppercase Vietnamese
    /// glyphs - E-circumflex-acute, I-hook, O-dot-below and the rest - so the
    /// subtitles would have come out with holes in them. Rather than restyle
    /// every screen in the game, the intro loads one face of its own that has
    /// full coverage and uses it for the captions only.
    sf::Font m_captionFont;
    bool m_haveCaptionFont = false;
    const sf::Font& captionFont() const {
        return m_haveCaptionFont ? m_captionFont : m_font;
    }

    struct Mote { sf::Vector2f pos; float speed; float radius; float phase; };
    std::vector<Mote> m_motes;

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float easeOut(float t) { const float u = 1.0f - clamp01(t); return 1.0f - u * u * u; }
    /// Accelerating: used for the title, which should arrive rather than drift in.
    static float easeIn(float t) { const float u = clamp01(t); return u * u * u; }
    static float window(float time, float from, float to) {
        return to <= from ? 1.0f : clamp01((time - from) / (to - from));
    }

    void leave();

    /// Draw one scene filling the screen, pushed in by `zoom` about `focus`
    /// (0..1 across the image) and nudged by `offset`.
    void drawScene(sf::RenderTarget& target, int index, float zoom,
                   sf::Vector2f focus, sf::Vector2f offset, float dim) const {
        if (!m_scene[index]) return;
        const sf::Texture& tex = *m_scene[index];
        const float tw = static_cast<float>(tex.getSize().x);
        const float th = static_cast<float>(tex.getSize().y);
        if (tw < 1.0f || th < 1.0f) return;

        const float scale = std::max(1280.0f / tw, 720.0f / th) * zoom;
        sf::Sprite sprite(tex);
        sprite.setScale(scale, scale);

        // Hold `focus` still while the picture grows around it, so the push
        // reads as moving toward the tower rather than as the frame swelling.
        const float drawnW = tw * scale;
        const float drawnH = th * scale;
        sprite.setPosition(offset.x + (1280.0f - drawnW) * focus.x,
                           offset.y + (720.0f - drawnH) * focus.y);
        const sf::Uint8 d = static_cast<sf::Uint8>(clamp01(dim) * 255.0f);
        sprite.setColor(sf::Color(d, d, d));
        target.draw(sprite);
    }

    /// UTF-8 in, an SFML string out. Written this way rather than assigning the
    /// literal directly because sf::String would otherwise take the bytes as
    /// Latin-1 and every diacritic would land as two wrong characters.
    static sf::String vi(const char* utf8) {
        return sf::String::fromUtf8(utf8, utf8 + std::strlen(utf8));
    }

    void drawSubtitle(sf::RenderTarget& target, const char* utf8, float alpha) const {
        if (alpha <= 0.01f) return;
        sf::Text line;
        line.setFont(captionFont());
        line.setString(vi(utf8));
        line.setCharacterSize(19);
        line.setLetterSpacing(2.6f);
        line.setFillColor(sf::Color(236, 224, 202, static_cast<sf::Uint8>(alpha * 255.0f)));
        line.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(alpha * 210.0f)));
        line.setOutlineThickness(2.0f);
        line.setPosition(78.0f, 612.0f);
        target.draw(line);

        // A short rule above it, the way a caption card is set.
        sf::RectangleShape rule({ 54.0f, 2.0f });
        rule.setPosition(78.0f, 600.0f);
        rule.setFillColor(sf::Color(198, 62, 58, static_cast<sf::Uint8>(alpha * 255.0f)));
        target.draw(rule);
    }

public:
    IntroState(StateManager& sm, const sf::Font& font)
        : m_stateManager(sm), m_font(font) {
        const char* files[3] = { "assets/intro/Phan_Canh1.png",
                                 "assets/intro/Phan_Canh2.png",
                                 "assets/intro/Phan_Canh3.png" };
        int found = 0;
        for (int i = 0; i < 3; ++i) {
            if (!ResourceManager::exists(files[i])) continue;
            const sf::Texture& tex = ResourceManager::get().getTexture(files[i]);
            if (tex.getSize().x < 2) continue;
            m_scene[i] = &tex;
            ++found;
        }
        // A missing painting is not fatal: drawScene skips a null texture, so
        // the act plays out over black with its caption rather than crashing.
        (void)found;

        // Drop a .ttf in assets/fonts and it wins; otherwise a system serif with
        // full Vietnamese coverage. Checked with fontTools, not assumed.
        const char* faces[] = {
            "assets/fonts/Caption.ttf",
            "C:/Windows/Fonts/cambria.ttc",
            "C:/Windows/Fonts/constan.ttf",
            "C:/Windows/Fonts/pala.ttf",
            "C:/Windows/Fonts/times.ttf",
        };
        for (const char* face : faces) {
            std::error_code ec;
            if (!std::filesystem::exists(face, ec)) continue;
            if (m_captionFont.loadFromFile(face)) { m_haveCaptionFont = true; break; }
        }

        m_motes.reserve(80);
        for (int i = 0; i < 80; ++i) {
            m_motes.push_back({ { Rng::rangeF(0.0f, 1280.0f), Rng::rangeF(0.0f, 720.0f) },
                                Rng::rangeF(8.0f, 34.0f),
                                Rng::rangeF(0.6f, 2.2f),
                                Rng::rangeF(0.0f, 6.28f) });
        }
    }

    void handleEvent(const sf::Event& event, const sf::RenderWindow&) override {
        // Locked while the film runs. Once the title has landed and the music is
        // done, the prompt takes any of the usual "go" keys.
        if (m_time < kScriptEnd) return;
        const bool go = (event.type == sf::Event::KeyPressed &&
                         (event.key.code == sf::Keyboard::Space ||
                          event.key.code == sf::Keyboard::Enter ||
                          event.key.code == sf::Keyboard::Escape)) ||
                        event.type == sf::Event::MouseButtonPressed;
        if (go) leave();
    }

    void update(float dt) override {
        m_time += dt;

        for (Mote& mote : m_motes) {
            mote.pos.y -= mote.speed * dt;
            if (mote.pos.y < -4.0f) {
                mote.pos.y = 724.0f;
                mote.pos.x = Rng::rangeF(0.0f, 1280.0f);
            }
        }

        // --- sound ------------------------------------------------------------
        // The named file wins if it exists, so dropping `intro_roar.wav` into
        // assets/audio takes over from the borrowed titan spawn with no code
        // change. The camera still kicks whether or not the sound is there.
        if (!m_roar && m_time >= kRoarAt) {
            m_roar = true;
            AudioManager::get().playNamed({ "intro_roar", "Dragoon_Titan_Spawn" }, 0.82f);
            m_shake = 12.0f;                       // the title landing
        }

        // --- shake ------------------------------------------------------------
        // Act three marches: a constant low rumble under the column, with the
        // slam spike decaying on top of it.
        const float marching = (m_time >= kAct2Flash && m_time < kSlamAt) ? 3.0f : 0.0f;
        m_shake = std::max(marching, m_shake - dt * 60.0f);
        m_shakeOffset = { Rng::rangeF(-m_shake, m_shake), Rng::rangeF(-m_shake, m_shake) };

        if (m_time >= kAutoGo || m_time >= kHardCap) leave();
    }

    void render(sf::RenderTarget& target) override {
        sf::RectangleShape bg({ 1280.0f, 720.0f });
        bg.setFillColor(sf::Color(6, 5, 8));
        target.draw(bg);

        // ---- ACT 1: the ruined sanctum -------------------------------------
        if (m_time < kAct1Fade) {
            // Ken Burns toward the glowing tower in the top right.
            const float k = window(m_time, 0.0f, kAct1End);
            const float zoom = 1.0f + 0.06f * k;
            // Fade to black over the last 200ms rather than cutting.
            const float dim = 1.0f - window(m_time, kAct1End, kAct1Fade);
            drawScene(target, 0, zoom, { 0.82f, 0.18f }, m_shakeOffset, dim);
            drawSubtitle(target, u8"KHI THẾ GIỚI TRƯỢT DẦN VỀ PHÍA TẬN THẾ...",
                         window(m_time, 0.35f, 1.30f) * dim);
        }
        // ---- ACT 2: the vow --------------------------------------------------
        else if (m_time < kAct2Flash) {
            const float k = window(m_time, kAct1Fade, kAct2End);
            // Starts low on the blade and rises to her face: the picture slides
            // DOWN, which is the camera tilting up.
            const sf::Vector2f rise(0.0f, 15.0f * easeOut(k));
            const float in = window(m_time, kAct1Fade, kAct1Fade + 0.22f);
            drawScene(target, 1, 1.04f, { 0.45f, 0.42f }, rise + m_shakeOffset, in);
            drawSubtitle(target, u8"...LỜI THỀ THIẾT KỈ CHÍNH THỨC TÁI KHỞI ĐỘNG.",
                         window(m_time, kAct1Fade + 0.30f, kAct1Fade + 1.10f));
        }
        // ---- ACT 3: the crusade ---------------------------------------------
        else {
            const float k = window(m_time, kAct2Flash, kScriptEnd);
            drawScene(target, 2, 1.0f + 0.03f * k, { 0.5f, 0.55f }, m_shakeOffset, 1.0f);
        }

        // Embers drift over everything from act two on, so the stills breathe.
        const float embers = window(m_time, kAct1Fade, kAct1Fade + 0.8f);
        if (embers > 0.01f) {
            for (const Mote& mote : m_motes) {
                const float twinkle = 0.5f + 0.5f * std::sin(m_time * 2.1f + mote.phase);
                sf::CircleShape dot(mote.radius);
                dot.setOrigin(mote.radius, mote.radius);
                dot.setPosition(mote.pos + m_shakeOffset);
                dot.setFillColor(sf::Color(255, 176, 96,
                    static_cast<sf::Uint8>(88.0f * twinkle * embers)));
                target.draw(dot);
            }
        }

        // ---- the white flash on the act 2 -> 3 beat --------------------------
        // Rises fast into 6.0s and falls away just as fast: a cut, not a wash.
        if (m_time > kAct2End && m_time < kAct2Flash + 0.14f) {
            const float up = window(m_time, kAct2End, kAct2Flash);
            const float down = 1.0f - window(m_time, kAct2Flash, kAct2Flash + 0.14f);
            const float a = std::min(up, down > 0.0f ? 1.0f : 0.0f) * (m_time < kAct2Flash ? up : down);
            sf::RectangleShape flash({ 1280.0f, 720.0f });
            flash.setFillColor(sf::Color(255, 250, 244,
                static_cast<sf::Uint8>(clamp01(a) * 255.0f)));
            target.draw(flash);
        }

        // ---- the title slam ---------------------------------------------------
        if (m_time >= kSlamAt - 0.30f) {
            const float k = window(m_time, kSlamAt - 0.30f, kSlamAt);

            // Ground for the lettering. The script asked for the title to land
            // in the empty sky at the top of the picture, but scene three has no
            // empty sky - it is dragon, spires and red cloud all the way across,
            // and dark red type on dark red cloud simply vanished. A soft band
            // gives the words something to sit on without hiding the painting.
            const float ground = window(m_time, kSlamAt - 0.30f, kSlamAt + 0.20f);
            if (ground > 0.01f) {
                sf::VertexArray band(sf::TriangleStrip, 6);
                const sf::Uint8 deep = static_cast<sf::Uint8>(ground * 214.0f);
                const sf::Uint8 mid = static_cast<sf::Uint8>(ground * 150.0f);
                const sf::Color top(6, 4, 8, deep);
                const sf::Color middle(6, 4, 8, mid);
                const sf::Color gone(6, 4, 8, 0);
                band[0] = sf::Vertex({ 0.0f, 0.0f }, top);
                band[1] = sf::Vertex({ 1280.0f, 0.0f }, top);
                band[2] = sf::Vertex({ 0.0f, 190.0f }, middle);
                band[3] = sf::Vertex({ 1280.0f, 190.0f }, middle);
                band[4] = sf::Vertex({ 0.0f, 300.0f }, gone);
                band[5] = sf::Vertex({ 1280.0f, 300.0f }, gone);
                target.draw(band);
            }
            const float e = easeIn(k);
            const float scale = 2.5f - 1.5f * e;
            const float alpha = 50.0f + 205.0f * e;

            sf::Text title;
            title.setFont(m_font);
            title.setString("SANCTUM");
            title.setCharacterSize(72);
            title.setStyle(sf::Text::Bold);
            title.setLetterSpacing(5.0f);
            title.setFillColor(sf::Color(246, 242, 236, static_cast<sf::Uint8>(alpha)));
            title.setOutlineColor(sf::Color(10, 6, 8, static_cast<sf::Uint8>(alpha * 0.85f)));
            title.setOutlineThickness(3.0f);
            TextUtils::centerBoth(title);
            title.setScale(scale, scale);
            title.setPosition(640.0f + m_shakeOffset.x, 150.0f + m_shakeOffset.y);
            target.draw(title);

            // The sub-line only arrives once the slam has landed, so it does not
            // ride down with the big word and blur into it.
            const float subIn = window(m_time, kSlamAt + 0.10f, kSlamAt + 0.55f);
            if (subIn > 0.01f) {
                sf::Text sub;
                sub.setFont(m_font);
                sub.setString("MECHA - CHIVALRY");
                sub.setCharacterSize(20);
                sub.setLetterSpacing(9.0f);
                // Warm gold, not the crimson the menu uses: the menu sets that
                // line on a pale sky, and the same red on this one disappeared.
                sub.setFillColor(sf::Color(238, 196, 138,
                    static_cast<sf::Uint8>(subIn * 255.0f)));
                sub.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(subIn * 235.0f)));
                sub.setOutlineThickness(2.5f);
                TextUtils::centerBoth(sub);
                sub.setPosition(640.0f + m_shakeOffset.x, 204.0f + m_shakeOffset.y);
                target.draw(sub);

                sf::RectangleShape rule({ 300.0f * subIn, 2.0f });
                rule.setOrigin(150.0f * subIn, 0.0f);
                rule.setPosition(640.0f + m_shakeOffset.x, 226.0f + m_shakeOffset.y);
                rule.setFillColor(sf::Color(226, 96, 78, static_cast<sf::Uint8>(subIn * 230.0f)));
                target.draw(rule);
            }

            // Whose game this is, small, under the title.
            const float byIn = window(m_time, kSlamAt + 0.35f, kSlamAt + 0.90f);
            if (byIn > 0.01f) {
                drawLabel(target, "A GAME BY DAO DUY ANH", { 640.0f, 250.0f }, 13,
                          sf::Color(214, 202, 178, static_cast<sf::Uint8>(byIn * 255.0f)),
                          4.5f, true);
            }
        }

        // ---- the prompt -------------------------------------------------------
        if (m_time >= kScriptEnd) {
            sf::VertexArray foot(sf::TriangleStrip, 4);
            const sf::Color gone(6, 4, 8, 0);
            const sf::Color deep(6, 4, 8, 196);
            foot[0] = sf::Vertex({ 0.0f, 612.0f }, gone);
            foot[1] = sf::Vertex({ 1280.0f, 612.0f }, gone);
            foot[2] = sf::Vertex({ 0.0f, 720.0f }, deep);
            foot[3] = sf::Vertex({ 1280.0f, 720.0f }, deep);
            target.draw(foot);

            const float blink = 0.55f + 0.45f * std::sin((m_time - kScriptEnd) * 4.4f);
            drawLabel(target, "[ PRESS SPACE TO COMMENCE ]", { 640.0f, 664.0f }, 16,
                      sf::Color(244, 230, 202, static_cast<sf::Uint8>(clamp01(blink) * 255.0f)),
                      3.4f, true);
        }
    }
};

class MenuState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    MenuBackdrop m_backdrop;
    sf::VertexArray m_scrim;

    sf::Text m_titleText;
    sf::Text m_subtitleText;
    std::vector<sf::Text> m_taglineLines;
    sf::RectangleShape m_titleRule;
    sf::Text m_footerText;

    UiButton m_startButton;
    UiButton m_deckButton;
    UiButton m_settingsButton;
    UiButton m_quitButton;
    float m_time = 0.0f;

    // Portrait picker. It used to live on the Dual-Core screen, wedged under the
    // doctrine tiles, where it read as part of building a deck - which it is
    // not. Your face is a profile setting, so it belongs on the menu, next to
    // the other settings, and is chosen once rather than re-confirmed every run.
    std::vector<sf::FloatRect> m_avatarSlots;
    int m_avatarHovered = -1;

    static constexpr float kColumnX = 320.0f;
    // The picker sits in a panel of its own. Loose thumbnails tucked under the
    // last button read as a fourth row of buttons, which is how a player ends up
    // clicking a face when they meant to quit.
    static constexpr float kProfileTop = 588.0f;
    static constexpr float kProfileH   = 88.0f;
    static constexpr float kAvatarRowY = 612.0f;

    void layoutAvatars();
    void renderAvatars(sf::RenderTarget& target) const;

public:
    MenuState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override;
    void render(sf::RenderTarget& target) override;
};

void IntroState::leave() {
    if (m_leaving) return;
    m_leaving = true;
    m_stateManager.changeState(std::make_unique<MenuState>(m_stateManager, m_font));
}

// =============================================================================
// CAMPAIGN MAP
// =============================================================================

// =============================================================================
// ROLE SELECT - the Dual-Core Protocol
// =============================================================================

/**
 * @brief Pick the two doctrines a run is built from.
 *
 * The primary core decides the commander passive and is the only core allowed
 * to field a Titan; the secondary is a splash of at most 16 cards. The screen
 * enforces that by simply refusing to let the same role fill both slots.
 */
class RoleSelectState : public GameState {
public:
    /// The same pair of cores is picked for two different reasons, so the
    /// screen is shared and only its exit differs.
    enum class Then { StartRun, EditDeck };

private:
    StateManager& m_stateManager;
    const sf::Font& m_font;
    Then m_then = Then::StartRun;

    MechRole m_primary = MechRole::Vanguard;
    MechRole m_secondary = MechRole::Siege;
    /// Which column the next click assigns to.
    bool m_pickingPrimary = true;

    std::array<sf::FloatRect, kMechRoleCount> m_tiles;
    int m_hovered = -1;
    float m_time = 0.0f;
    sf::Vector2f m_mouse;

    /// The deck the current pair would build. Rebuilt only when the pair
    /// changes: the old code built a 24-card deck inside render(), every frame,
    /// to print one line of text.
    DeckConfiguration m_preview;
    sf::FloatRect m_compBar;

    /// Each doctrine's Titan, resolved once. Held BY VALUE: the first version
    /// kept a pointer into the vector cardsForRole() returns, which is a
    /// temporary - it died at the end of the range-for, and the tile printed a
    /// Titan with no name because the ints survived in freed memory and the
    /// std::string did not.
    std::array<CardData, kMechRoleCount> m_titan;
    std::array<bool, kMechRoleCount> m_hasTitan{};

    UiButton m_confirmButton;
    UiButton m_backButton;

    void layoutTiles();
    void refreshPreview();
    void drawRoleTile(sf::RenderTarget& target, MechRole role, int slot) const;
    /// The doctrine's emblem, drawn from primitives at low alpha behind the
    /// tile text. Six shapes rather than six PNGs, so a tile is never waiting
    /// on an art file that does not exist yet.
    void drawDoctrineGlyph(sf::RenderTarget& target, MechRole role,
                           sf::Vector2f centre, float radius, sf::Color colour) const;
    void drawCompositionBar(sf::RenderTarget& target) const;

public:
    RoleSelectState(StateManager& sm, const sf::Font& font, Then then = Then::StartRun);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override;
    void render(sf::RenderTarget& target) override;

private:
    /// Leave for whatever this screen was opened to do.
    void commit();
};

/**
 * @brief Build a deck by hand, one card at a time.
 *
 * The game generates a deck the moment two cores are picked, and that stays the
 * default - it is what makes a run one click away. This screen is the other
 * option: the catalogue on the left, the deck on the right, and the same rules
 * the generator obeys enforced as you click rather than explained afterwards.
 *
 * Nothing here can produce an illegal deck. A card that would break a rule
 * simply refuses to go in and says which rule, which is far kinder than letting
 * you build twenty-four cards and then rejecting the lot.
 */
class DeckEditState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    MechRole m_primary;
    MechRole m_secondary;
    /// Empty until the deck has been saved once, so a new deck does not
    /// overwrite whatever was last edited.
    std::string m_deckId;
    std::string m_deckName;
    bool m_renaming = false;

    /// Every card legally available to this pair, ordered by cost then name so
    /// the grid reads as a curve rather than as catalogue order.
    std::vector<CardData> m_pool;
    /// Catalogue id -> copies currently in the deck.
    std::unordered_map<std::string, int> m_counts;
    /// Distinct ids in deck order, so the right-hand list does not reshuffle
    /// itself every time a count changes.
    std::vector<std::string> m_order;

    /// What the grid is currently showing: indices into m_pool. The grid reads
    /// this, never m_pool directly, so search and filter need no special cases
    /// anywhere else.
    std::vector<int> m_shown;
    std::string m_search;
    /// Which kinds of card the grid admits. Deliberately not a bitmask - four
    /// exclusive chips are read faster than four independent toggles.
    enum class Show { All, Units, Spells, Counters };
    Show m_show = Show::All;
    /// Empty means both cores.
    int m_roleFilter = -1;       // -1 both, 0 primary, 1 secondary

    int m_scroll = 0;            // first visible grid ROW
    int m_hoverCard = -1;        // index into m_shown
    int m_hoverRow = -1;         // index into m_order
    sf::Vector2f m_mouse;
    float m_time = 0.0f;
    std::string m_notice;        // why the last click did nothing
    float m_noticeTimer = 0.0f;

    UiButton m_backButton;
    UiButton m_saveButton;
    UiButton m_autoButton;
    UiButton m_clearButton;

    static constexpr int kCols = 5;
    static constexpr int kRows = 3;
    // Sized to what is left after the filter strip, keeping the card's own
    // 0.73 proportion so the frame art is not stretched.
    static constexpr float kCardW = 134.0f;
    static constexpr float kCardH = 184.0f;
    static constexpr float kGapX = 16.0f;
    static constexpr float kGapY = 8.0f;
    static constexpr float kGridX = 93.0f;
    static constexpr float kGridY = 100.0f;

public:
    /// A brand new deck on this pair.
    DeckEditState(StateManager& sm, const sf::Font& font, MechRole primary, MechRole secondary);
    /// Reopen a deck the player already saved.
    DeckEditState(StateManager& sm, const sf::Font& font, const DeckStore::SavedDeck& deck);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override;
    void render(sf::RenderTarget& target) override;

private:
    sf::FloatRect cellAt(int visibleIndex) const;
    int cardUnder(sf::Vector2f point) const;
    int rowUnder(sf::Vector2f point) const;
    sf::FloatRect listPanel() const { return { 896.0f, 100.0f, 356.0f, 572.0f }; }
    sf::FloatRect rowRect(int index) const;

    int total() const;
    int countForRole(MechRole role) const;
    int copiesOf(const std::string& id) const;
    /// Why this card cannot go in, or an empty string when it can.
    std::string refuseReason(const CardData& card) const;
    void addCard(const CardData& card);
    void removeCard(const std::string& id);
    void setFromConfiguration(const DeckConfiguration& config);
    DeckConfiguration asConfiguration() const;
    void refreshButtons();
    void say(const std::string& text) { m_notice = text; m_noticeTimer = 2.4f; }

    void renderGrid(sf::RenderTarget& target);
    void renderList(sf::RenderTarget& target);
    /// The card under the cursor in the deck list, drawn large over the grid.
    void renderRowPreview(sf::RenderTarget& target) const;
    void renderHeader(sf::RenderTarget& target);
    void buildPool();
    void setupChrome();
    /// Recompute m_shown from the search box and the chips.
    void applyFilter();
    sf::FloatRect nameField() const { return { 896.0f, 66.0f, 356.0f, 30.0f }; }
    sf::FloatRect searchField() const { return { 326.0f, 62.0f, 224.0f, 26.0f }; }
    sf::FloatRect chipRect(int index) const {
        return { 566.0f + index * 78.0f, 62.0f, 74.0f, 26.0f };
    }
    sf::FloatRect roleChipRect(int index) const {
        // 72 wide, not 64: every doctrine's display name is eight characters or
        // fewer, and the narrower chip was clipping "Vanguard" to "Vanguar".
        return { 88.0f + index * 76.0f, 62.0f, 72.0f, 26.0f };
    }
};


/**
 * @brief Every deck the player has built, as a shelf of cards.
 *
 * A builder that keeps exactly one deck per pair of cores is not really a
 * builder: the interesting question is usually "the aggressive Vanguard list or
 * the grindy one", not "which two cores". This screen is where those live.
 *
 * The deck a RUN uses is still chosen by the cores picked on the way in, so
 * several decks can share a pair and the newest of them wins. That rule is
 * printed on the tile as IN USE rather than left to be discovered.
 */
class DeckListState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    std::vector<DeckStore::SavedDeck> m_decks;
    int m_hovered = -1;          // -1 none, -2 the NEW DECK tile
    int m_deleteArmed = -1;      // a tile whose X was clicked once
    sf::Vector2f m_mouse;
    float m_time = 0.0f;
    UiButton m_backButton;

    static constexpr int kCols = 4;
    static constexpr float kTileW = 232.0f;
    static constexpr float kTileH = 252.0f;
    static constexpr float kGapX = 22.0f;
    static constexpr float kGapY = 26.0f;
    static constexpr float kGridX = 96.0f;
    static constexpr float kGridY = 118.0f;

public:
    DeckListState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override { m_time += dt; }
    void render(sf::RenderTarget& target) override;

private:
    /// Tile 0 is always NEW DECK, so saved deck i sits at slot i + 1.
    sf::FloatRect tileAt(int slot) const;
    sf::FloatRect deleteAt(int slot) const;
    int slotUnder(sf::Vector2f point) const;
    bool isInUse(const DeckStore::SavedDeck& deck) const;
    void drawNewTile(sf::RenderTarget& target) const;
    void drawDeckTile(sf::RenderTarget& target, int index) const;
};

class MapState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    UiButton m_marchButton;
    UiButton m_settingsButton;
    float m_time = 0.0f;

    // Counted once on entry rather than every frame: the deck does not change
    // while this screen is up.
    int m_units = 0, m_spells = 0, m_traps = 0;

public:
    MapState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override { m_time += dt; }
    void render(sf::RenderTarget& target) override;

private:
    void countDeck();
    /// A labelled bar. Used for both reactors, so the two read as the same
    /// measurement rather than as one bar and one sentence.
    void drawGauge(sf::RenderTarget& target, sf::FloatRect box, float fill,
                   sf::Color colour, const std::string& caption) const;
    void renderYourPanel(sf::RenderTarget& target) const;
    void renderNextPanel(sf::RenderTarget& target) const;
    void renderPath(sf::RenderTarget& target) const;
};

// =============================================================================
// DUEL
// =============================================================================

enum class Interaction { Idle, DraggingCard, DraggingUnit, SelectingTributes };

class DuelState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    DuelEngine m_duel;
    AICommander m_ai{ Side::Opponent };
    FloatingTextSystem m_floating;
    HolyVFX m_vfx;
    CombatVFX m_combat;
    CardSpotlight m_spotlight;
    DrawFlight m_drawFlight;
    EndScreen m_endScreen;
    EndGameVFX m_endVfx;
    /// Seconds of real time left in the hit stop. While this is running the
    /// end sequence is stepped at a fraction of normal speed.
    float m_hitStop = 0.0f;
    PortraitRig m_portraitRig;

    /// Phase 4 of the defeat sequence hands the player the decision instead of
    /// dumping them onto the run-over screen.
    UiButton m_retryButton;
    UiButton m_menuButton;
    float m_sparkClock = 0.0f;
    /// Free-running, for anything that breathes on its own: the End Turn glow
    /// when the turn is spent, and whatever else wants a phase. Separate from
    /// m_sparkClock, which is a countdown between spark bursts and sits at zero
    /// for the whole duel.
    float m_uiClock = 0.0f;

    /// Where every frame was standing as of the last refresh. A destroyed frame
    /// is off the board by the time its event is read, so its wreck has to be
    /// drawn from a remembered position rather than a live lookup.
    std::unordered_map<int, sf::FloatRect> m_lastRect;
    /// Where each face-down counter sits, so a flip can play in its own slot
    /// after the engine has already removed it from the zone.
    std::unordered_map<int, sf::FloatRect> m_lastTrapRect;

    // A spotlight waiting on an animation that has to finish first.
    CardData m_queuedCard;
    Side m_queuedSide = Side::Player;
    CardSpotlight::Kind m_queuedKind = CardSpotlight::Kind::Counter;
    float m_queuedDelay = 0.0f;
    /// The frame that struck most recently, so damage knows which way to spray.
    int m_lastAttackerId = -1;

    // interaction
    Interaction m_mode = Interaction::Idle;
    int m_dragCardIndex = -1;
    int m_dragUnitId = -1;
    sf::Vector2f m_mousePos;
    sf::Vector2f m_pressPos;        // where the button went down
    bool m_dragMoved = false;       // a press that never moves is a click, not a drag
    int m_hoverCardIndex = -1;
    /// A card lifts only after the cursor rests on it. Without this, dragging
    /// the mouse across the hand set off every card in turn, which is both
    /// noisy and slow to read.
    int m_hoverCandidate = -1;
    float m_hoverDwell = 0.0f;
    static constexpr float kHoverDelay = 0.22f;
    int m_hoverUnitId = -1;

    /// Below this many pixels of travel a press counts as a click and opens
    /// the card's detail sheet instead of trying to play or attack with it.
    static constexpr float kDragThreshold = 9.0f;

    // pending tribute summon
    CardData m_pendingCard;
    int m_pendingHandIndex = -1;
    BoardLine m_pendingLine = BoardLine::Frontline;
    int m_pendingSlot = -1;
    int m_pendingNeeded = 0;
    std::vector<int> m_tributes;

    // enemy turn pacing
    bool m_enemyThinking = false;
    float m_enemyTimer = 0.0f;

    // presentation
    std::deque<std::string> m_log;
    /// The log is a panel behind a button now, not a permanent column of text
    /// down the left edge competing with the board for attention.
    /// Follows the setting: turning the battle log off hides the button as
    /// well as the panel. The toggle used to be written by the settings screen
    /// and read by nobody, so it did nothing at all.
    bool m_logOpen = false;
    UiButton m_endTurnButton;
    float m_bannerTimer = 0.0f;
    std::string m_bannerText;
    sf::Sprite m_enemyPortrait;
    sf::Sprite m_playerPortrait;
    /// Resolved once in the constructor; the HUD paints from these directly so
    /// avatar cropping lives in one place.
    std::string m_playerArtPath;
    std::string m_enemyArtPath;
    bool m_hasPortraits = false;
    float m_resultTimer = 0.0f;

public:
    DuelState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override;
    void render(sf::RenderTarget& target) override;

private:
    // input helpers
    int handCardAt(sf::Vector2f point) const;
    sf::Vector2f handCardCentre(int index, int count) const;
    /// Degrees this hand card leans, so the hand reads as a fan rather than a
    /// row of tiles. Positive is clockwise, matching sf::Transformable.
    float handCardTilt(int index, int count) const;

    /// How far the pointed-at card lifts clear of the fan, and how much bigger
    /// it is drawn. At 1.45 the card covered the two beside it completely; at
    /// 1.35 the fan still reads while the hovered card is legible outright,
    /// which is the whole point - no click, no inspector.
    static constexpr float kHoverLift = 45.0f;
    static constexpr float kHoverZoom = 1.35f;
    sf::Vector2f handCardSize() const { return { 106.0f, 146.0f }; }
    /// Where face-down card `index` of the enemy's grip sits. Shared with the
    /// draw animation so a card flies to the exact slot it will occupy.
    sf::FloatRect enemyHandCardBox(int index, int count) const;
    /// Send whatever was just drawn on its way from the pile to the hand.
    void launchDraws(const std::vector<DuelEvent>& batch);
    Unit* unitAt(sf::Vector2f point);
    sf::FloatRect rectOf(const Unit& unit) const;
    bool trapZoneHit(Side side, sf::Vector2f point) const;
    /// True when nothing in hand can still be paid for. The End Turn control
    /// changes colour on this, so "I am done" is answered by the button rather
    /// than by the player checking every card against their energy.
    bool outOfMoves() const;
    /// The player's own armed counter under this point, or nullptr. Only ever
    /// returns one of YOUR counters: the enemy's are face down and stay that way.
    const TrapCard* ownTrapAt(sf::Vector2f point) const;
    bool commanderHit(Side side, sf::Vector2f point) const;
    bool rowAt(sf::Vector2f point, Side& side, BoardLine& line) const;
    void openInspector(const CardData& card);
    /// Inspect a deployed frame, so the sheet can list what is on it.
    void openInspector(const Unit& unit);

    void beginDrag(sf::Vector2f point);
    void releaseDrag(sf::Vector2f point);
    void handleTributeClick(sf::Vector2f point);
    void tryPendingSummon();
    void cancelPending();

    void pushLog(const std::string& line);
    void consumeEvents();
    void refreshRectCache();
    /// Centre of a frame, falling back to its remembered position once it is
    /// off the board.
    sf::Vector2f centreOf(int unitId) const;
    /// Where a spent card's remains drift to.
    sf::Vector2f scrapCentre(Side side) const;
    sf::Color accentOf(int unitId) const;
    void updateLiveArrow();
    void showBanner(const std::string& text) { m_bannerText = text; m_bannerTimer = 2.1f; }
    void finishDuel();
    void beginEndSequence();
    void updateEndSequence(float dt);

    /// Dims the battlefield art so the board reads as furniture on top of it,
    /// and draws the sixteen unit cells and six counter cells as real slots.
    void renderBoardGrid(sf::RenderTarget& target);
    void renderRow(sf::RenderTarget& target, Side side, BoardLine line);
    void renderCommanderPanel(sf::RenderTarget& target, Side side);
    void renderPiles(sf::RenderTarget& target, Side side);
    void renderEnemyHand(sf::RenderTarget& target);
    void renderEndTurn(sf::RenderTarget& target);
    void renderLogButton(sf::RenderTarget& target);
    void renderLogPanel(sf::RenderTarget& target);
    void drawCardBack(sf::RenderTarget& target, sf::FloatRect box, Side owner,
                      sf::Color accent, float alpha) const;
    void renderHq(sf::RenderTarget& target, Side side);
    void renderFrontLine(sf::RenderTarget& target);
    void renderTraps(sf::RenderTarget& target, Side side);
    void renderHand(sf::RenderTarget& target);
    void renderDrawFlights(sf::RenderTarget& target);
    void renderDragOverlay(sf::RenderTarget& target);

    bool isPlayerTurn() const { return !m_duel.isOver() && m_duel.activeSide() == Side::Player; }
    bool canPlayCard(const CardData& card) const;
};

// =============================================================================
// REWARD
// =============================================================================

class RewardState : public GameState {
private:
    /**
     * The reward is two decisions, not one. Choosing what to ADD is the obvious
     * half, but at a fixed deck size adding is the weaker lever: a card you
     * never want still turns up on a share of your draws. Being allowed to
     * SCRAP one is what turns a pile into a deck, so both happen here, in order.
     */
    /**
     * Three decisions, not two. Offer and Purge change what is IN the deck;
     * Augment changes how the deck plays, and only turns up after fights 2 and
     * 4 - every fight would make augments the progression and the cards an
     * afterthought.
     */
    enum class Phase { Offer, Purge, Augment };

    StateManager& m_stateManager;
    const sf::Font& m_font;
    std::vector<CardData> m_offers;
    Phase m_phase = Phase::Offer;
    int m_hovered = -1;
    bool m_skipHovered = false;
    std::vector<Augments::Id> m_augmentOffers;
    sf::Text m_heading;
    sf::Text m_subheading;

public:
    RewardState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float) override {}
    void render(sf::RenderTarget& target) override;

private:
    void enterPurgePhase();
    /// Returns false when this fight is not one of the augment fights.
    bool enterAugmentPhase();
    void leaveToMap();
    sf::FloatRect augmentPanel(int index) const {
        return { 128.0f + index * 348.0f, 210.0f, 320.0f, 250.0f };
    }
    void renderAugments(sf::RenderTarget& target) const;
    /// The card under this point, or -1. One routine, so hover and click can
    /// never disagree about what is being pointed at.
    int pickAt(sf::Vector2f point) const;

    sf::Vector2f cardCentre(int index) const {
        return { 400.0f + index * 240.0f, 380.0f };
    }
    sf::Vector2f cardSize() const { return { 200.0f, 280.0f }; }

    // --- the purge grid: the whole deck on screen at once ---
    /// The most cards that fit across the design width at gridCardSize().
    static constexpr int kGridMaxColumns = 12;
    sf::Vector2f gridCardSize() const { return { 84.0f, 118.0f }; }

    /// Cards per row, chosen so the rows come out even. Packing greedily to
    /// twelve leaves a deck of 25 as 12/12/1, and that orphan reads as a bug;
    /// spreading the same three rows as 9/8/8 does not.
    int gridColumnsFor(int count) const {
        if (count <= 0) return 1;
        const int rows = (count + kGridMaxColumns - 1) / kGridMaxColumns;
        return (count + rows - 1) / rows;
    }

    sf::Vector2f gridCentre(int index, int count) const {
        const int columns = gridColumnsFor(count);
        const int row = index / columns;
        const int column = index % columns;

        // Each row is centred on its own contents, so a short final row sits
        // under the middle of the grid rather than hanging off the left edge.
        const int rows = (count + columns - 1) / columns;
        const int inThisRow = (row == rows - 1) ? (count - row * columns) : columns;

        const float pitchX = gridCardSize().x + 8.0f;
        const float startX = 640.0f - (inThisRow - 1) * pitchX / 2.0f;
        return { startX + column * pitchX, 258.0f + row * (gridCardSize().y + 12.0f) };
    }
    sf::FloatRect skipButton() const { return { 490.0f, 640.0f, 300.0f, 44.0f }; }
};

// =============================================================================
// RUN OVER
// =============================================================================

class RunOverState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;
    bool m_victory;
    sf::Sprite m_background;
    bool m_hasBackground = false;
    sf::Text m_heading;
    sf::Text m_summary;
    UiButton m_againButton;

public:
    RunOverState(StateManager& sm, const sf::Font& font, bool victory);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float) override {}
    void render(sf::RenderTarget& target) override;
};

// =============================================================================
// SETTINGS  (an overlay, so it can be opened over a live duel)
// =============================================================================

class SettingsState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    std::vector<UiSlider> m_sliders;
    std::vector<UiToggle> m_toggles;
    std::vector<UiChoice> m_choices;
    int m_hoveredChoice = -1;
    UiButton m_backButton;
    UiButton m_defaultsButton;
    int m_hoveredSlider = -1;
    int m_hoveredToggle = -1;

    static constexpr float kPanelX = 350.0f;
    static constexpr float kPanelY = 38.0f;
    static constexpr float kPanelW = 580.0f;
    // 660, not 620: the panel gained a segmented difficulty row and the hint
    // line at the bottom was printing straight over it.
    static constexpr float kPanelH = 660.0f;

public:
    SettingsState(StateManager& sm, const sf::Font& font);

    bool isOverlay() const override { return true; }
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float) override {}
    void render(sf::RenderTarget& target) override;

private:
    void buildControls();
    void close();
};

SettingsState::SettingsState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font) {
    buildControls();
    m_backButton.setup(font, "BACK", { kPanelX + kPanelW - 116.0f, kPanelY + kPanelH - 44.0f },
                       { 176.0f, 44.0f }, sf::Color(236, 190, 74), 17);
    m_defaultsButton.setup(font, "RESTORE DEFAULTS",
                           { kPanelX + 152.0f, kPanelY + kPanelH - 44.0f },
                           { 232.0f, 44.0f }, sf::Color(150, 142, 130), 14);
}

void SettingsState::buildControls() {
    Settings& settings = Settings::get();
    m_sliders.clear();
    m_toggles.clear();
    m_choices.clear();

    const float x = kPanelX + 34.0f;
    const float width = kPanelW - 68.0f;
    float y = kPanelY + 100.0f;

    auto slider = [&](const char* label, float* value, float lo, float hi, bool percent) {
        UiSlider s;
        s.track = { x, y, width, 6.0f };
        s.label = label;
        s.minValue = lo;
        s.maxValue = hi;
        s.value = value;
        s.asPercent = percent;
        m_sliders.push_back(s);
        y += 58.0f;
    };

    slider("Master volume", &settings.masterVolume, 0.0f, 1.0f, true);
    slider("Music", &settings.musicVolume, 0.0f, 1.0f, true);
    slider("Sound effects", &settings.sfxVolume, 0.0f, 1.0f, true);
    slider("Enemy turn speed", &settings.enemyTurnSpeed, 0.5f, 3.0f, false);

    y += 8.0f;
    auto toggle = [&](const char* label, bool* value) {
        UiToggle t;
        t.box = { x, y, 52.0f, 26.0f };
        t.label = label;
        t.value = value;
        m_toggles.push_back(t);
        y += 44.0f;
    };

    toggle("Fullscreen", &settings.fullscreen);
    toggle("Vertical sync", &settings.vsync);
    toggle("Screen shake", &settings.screenShake);
    toggle("Battle log", &settings.showBattleLog);

    y += 6.0f;
    UiChoice difficulty;
    difficulty.box = { x, y, 268.0f, 30.0f };
    difficulty.label = "Difficulty";
    difficulty.value = &settings.difficulty;
    difficulty.count = 3;
    difficulty.nameOf = &Settings::difficultyName;
    m_choices.push_back(difficulty);
}

void SettingsState::close() {
    Settings::get().clampAll();
    Settings::get().save();
    AudioManager::get().applySettings();
    m_stateManager.popState();
}

void SettingsState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_hoveredSlider = -1;
        m_hoveredToggle = -1;
        for (size_t i = 0; i < m_sliders.size(); ++i) {
            if (m_sliders[i].dragging) {
                m_sliders[i].setFromMouse(p);
                AudioManager::get().applySettings();   // volume follows the drag live
            }
            if (m_sliders[i].hit(p)) m_hoveredSlider = static_cast<int>(i);
        }
        for (size_t i = 0; i < m_toggles.size(); ++i) {
            if (m_toggles[i].hit(p)) m_hoveredToggle = static_cast<int>(i);
        }
        m_hoveredChoice = -1;
        for (const UiChoice& choice : m_choices) {
            const int segment = choice.hit(p);
            if (segment >= 0) m_hoveredChoice = segment;
        }
        m_backButton.setHovered(m_backButton.contains(p));
        m_defaultsButton.setHovered(m_defaultsButton.contains(p));
        return;
    }

    if (event.type == sf::Event::MouseButtonPressed &&
        event.mouseButton.button == sf::Mouse::Left) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });

        for (UiSlider& slider : m_sliders) {
            if (!slider.hit(p)) continue;
            slider.dragging = true;
            slider.setFromMouse(p);
            AudioManager::get().applySettings();
            return;
        }
        for (UiChoice& choice : m_choices) {
            const int segment = choice.hit(p);
            if (segment < 0 || !choice.value) continue;
            *choice.value = segment;
            Settings::get().save();
            return;
        }
        for (UiToggle& toggle : m_toggles) {
            if (!toggle.hit(p) || !toggle.value) continue;
            *toggle.value = !*toggle.value;
            // The engine notices a fullscreen or vsync change next frame and
            // rebuilds the window, so nothing else is needed here.
            Settings::get().save();
            return;
        }
        if (m_backButton.contains(p)) { close(); return; }
        if (m_defaultsButton.contains(p)) {
            Settings::get().resetToDefaults();
            buildControls();
            AudioManager::get().applySettings();
            Settings::get().save();
            return;
        }
        return;
    }

    if (event.type == sf::Event::MouseButtonReleased) {
        for (UiSlider& slider : m_sliders) slider.dragging = false;
        return;
    }

    if (event.type == sf::Event::KeyPressed &&
        (event.key.code == sf::Keyboard::Escape || event.key.code == sf::Keyboard::Enter)) {
        close();
    }
}

void SettingsState::render(sf::RenderTarget& target) {
    sf::RectangleShape veil({ 1280.0f, 720.0f });
    veil.setFillColor(sf::Color(8, 6, 14, 205));
    target.draw(veil);

    drawPanel(target, { kPanelX, kPanelY, kPanelW, kPanelH },
              sf::Color(19, 16, 24, 250), sf::Color(96, 82, 56, 220));

    sf::Text heading;
    heading.setFont(m_font);
    heading.setString("SETTINGS");
    heading.setCharacterSize(26);
    heading.setStyle(sf::Text::Bold);
    heading.setLetterSpacing(5.0f);
    heading.setFillColor(sf::Color(243, 208, 122));
    heading.setPosition(kPanelX + 34.0f, kPanelY + 26.0f);
    target.draw(heading);

    sf::RectangleShape rule({ kPanelW - 68.0f, 1.0f });
    rule.setPosition(kPanelX + 34.0f, kPanelY + 70.0f);
    rule.setFillColor(sf::Color(104, 90, 64, 190));
    target.draw(rule);

    // --- sliders ---
    for (size_t i = 0; i < m_sliders.size(); ++i) {
        const UiSlider& slider = m_sliders[i];
        const bool active = (static_cast<int>(i) == m_hoveredSlider) || slider.dragging;

        drawLabel(target, slider.label,
                  { slider.track.left, slider.track.top - 26.0f }, 15,
                  active ? sf::Color(240, 232, 214) : sf::Color(196, 190, 180));

        sf::Text readout;
        readout.setFont(Fonts::ui());
        readout.setString(slider.readout());
        readout.setCharacterSize(14);
        readout.setFillColor(sf::Color(214, 180, 116));
        sf::FloatRect rb = readout.getLocalBounds();
        readout.setPosition(slider.track.left + slider.track.width - rb.width,
                            slider.track.top - 26.0f);
        target.draw(readout);

        sf::RectangleShape trackShape({ slider.track.width, slider.track.height });
        trackShape.setPosition(slider.track.left, slider.track.top);
        trackShape.setFillColor(sf::Color(44, 38, 52));
        target.draw(trackShape);

        sf::RectangleShape fill({ slider.track.width * slider.ratio(), slider.track.height });
        fill.setPosition(slider.track.left, slider.track.top);
        fill.setFillColor(active ? sf::Color(247, 209, 106) : sf::Color(198, 162, 86));
        target.draw(fill);

        sf::CircleShape knob(active ? 10.0f : 8.0f);
        knob.setOrigin(knob.getRadius(), knob.getRadius());
        knob.setPosition(slider.track.left + slider.track.width * slider.ratio(),
                         slider.track.top + slider.track.height / 2.0f);
        knob.setFillColor(sf::Color(247, 224, 168));
        knob.setOutlineThickness(1.5f);
        knob.setOutlineColor(sf::Color(30, 24, 16));
        target.draw(knob);
    }

    // --- toggles ---
    for (size_t i = 0; i < m_toggles.size(); ++i) {
        const UiToggle& toggle = m_toggles[i];
        const bool on = toggle.value && *toggle.value;
        const bool active = (static_cast<int>(i) == m_hoveredToggle);

        sf::RectangleShape pill({ toggle.box.width, toggle.box.height });
        pill.setPosition(toggle.box.left, toggle.box.top);
        pill.setFillColor(on ? sf::Color(84, 66, 26) : sf::Color(34, 30, 40));
        pill.setOutlineThickness(1.5f);
        pill.setOutlineColor(on ? sf::Color(233, 190, 92)
                                : sf::Color(active ? 130 : 88, 84, 96));
        target.draw(pill);

        sf::CircleShape dot(9.0f);
        dot.setOrigin(9.0f, 9.0f);
        dot.setPosition(toggle.box.left + (on ? toggle.box.width - 15.0f : 15.0f),
                        toggle.box.top + toggle.box.height / 2.0f);
        dot.setFillColor(on ? sf::Color(247, 224, 168) : sf::Color(128, 120, 134));
        target.draw(dot);

        drawLabel(target, toggle.label,
                  { toggle.box.left + toggle.box.width + 18.0f, toggle.box.top + 4.0f }, 15,
                  active ? sf::Color(240, 232, 214) : sf::Color(196, 190, 180));

        drawLabel(target, on ? "ON" : "OFF",
                  { kPanelX + kPanelW - 74.0f, toggle.box.top + 5.0f }, 13,
                  on ? sf::Color(214, 180, 116) : sf::Color(120, 114, 126), 2.0f);
    }

    // --- segmented choices ---
    for (const UiChoice& choice : m_choices) {
        const int picked = choice.value ? *choice.value : 0;
        drawLabel(target, choice.label,
                  { choice.box.left + choice.box.width + 18.0f, choice.box.top + 7.0f }, 15,
                  sf::Color(196, 190, 180));

        for (int i = 0; i < choice.count; ++i) {
            const sf::FloatRect seg = choice.segment(i);
            const bool on = i == picked;
            const bool hot = i == m_hoveredChoice;
            sf::RectangleShape cell({ seg.width - 3.0f, seg.height });
            cell.setPosition(seg.left + 1.5f, seg.top);
            cell.setFillColor(on ? sf::Color(84, 66, 26) : sf::Color(30, 28, 36));
            cell.setOutlineThickness(1.5f);
            cell.setOutlineColor(on ? sf::Color(233, 190, 92)
                                    : sf::Color(hot ? 130 : 84, 82, 94));
            target.draw(cell);

            const char* name = choice.nameOf ? choice.nameOf(i) : "";
            sf::Text text;
            text.setFont(Fonts::ui());
            text.setString(name);
            text.setCharacterSize(12);
            text.setLetterSpacing(1.4f);
            text.setStyle(on ? sf::Text::Bold : sf::Text::Regular);
            text.setFillColor(on ? sf::Color(247, 224, 168) : sf::Color(150, 144, 156));
            TextUtils::centerBoth(text);
            text.setPosition(seg.left + seg.width / 2.0f, seg.top + seg.height / 2.0f);
            target.draw(text);
        }
    }

    m_defaultsButton.render(target);
    m_backButton.render(target);

    drawLabel(target, "F11 toggles fullscreen anywhere  -  Esc closes this panel",
              { kPanelX + 34.0f, kPanelY + kPanelH - 88.0f }, 11, sf::Color(126, 120, 132));
}

// =============================================================================
// CARD INSPECT  (click any card: the full card plus a rules glossary)
// =============================================================================

class CardInspectState : public GameState {
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;
    CardData m_card;
    std::vector<CardArt::GlossaryEntry> m_glossary;
    std::vector<CardArt::GlossaryEntry> m_statuses;
    UiButton m_closeButton;

public:
    CardInspectState(StateManager& sm, const sf::Font& font, CardData card,
                     std::vector<CardArt::GlossaryEntry> statuses = {})
        : m_stateManager(sm), m_font(font), m_card(std::move(card)),
          m_statuses(std::move(statuses)) {
        m_glossary = CardArt::glossaryFor(m_card);
        m_closeButton.setup(font, "CLOSE", { 1090.0f, 640.0f }, { 180.0f, 44.0f },
                            sf::Color(206, 180, 130), 16);
    }

    bool isOverlay() const override { return true; }

    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override {
        if (event.type == sf::Event::MouseMoved) {
            const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
            m_closeButton.setHovered(m_closeButton.contains(p));
            return;
        }
        // Any click anywhere dismisses it - a reference sheet should never trap
        // the player behind a button hunt.
        if (event.type == sf::Event::MouseButtonPressed ||
            (event.type == sf::Event::KeyPressed &&
             (event.key.code == sf::Keyboard::Escape || event.key.code == sf::Keyboard::Space))) {
            m_stateManager.popState();
        }
    }

    void update(float) override {}

    void render(sf::RenderTarget& target) override {
        sf::RectangleShape veil({ 1280.0f, 720.0f });
        veil.setFillColor(sf::Color(6, 5, 10, 216));
        target.draw(veil);

        // The card itself, large, right of centre - drawn WITHOUT its badges.
        //
        // At hand size the cost gem and the attack/health circles have to sit on
        // the card, because that is the only place there is. Blown up to 320x448
        // they became three discs parked on the artwork the player opened the
        // inspector to look at. Here they move to their own strip underneath,
        // where there is room and nothing is covered.
        const sf::Vector2f cardCentre(838.0f, 330.0f);
        const sf::Vector2f cardSize(320.0f, 448.0f);
        CardArt::drawCard(target, m_font, m_card, cardCentre, cardSize,
                          0.0f, true, false, false);

        // --- stat strip, clear of the card ------------------------------------
        struct Stat { const char* label; std::string value; sf::Color ink; bool show; };
        const bool isUnit = m_card.category == CardCategory::Unit;
        const Stat stats[] = {
            { "COST",   std::to_string(m_card.manaCost), sf::Color(120, 200, 255), true },
            { "ATTACK", std::to_string(m_card.attack),   sf::Color(226, 148, 62),  isUnit },
            { "HEALTH", std::to_string(m_card.health),   sf::Color(214, 96, 100),  isUnit },
        };

        int shown = 0;
        for (const Stat& stat : stats) if (stat.show) ++shown;

        const float pitch = 104.0f;
        const float stripY = cardCentre.y + cardSize.y / 2.0f + 40.0f;
        float sx = cardCentre.x - (shown - 1) * pitch / 2.0f;

        // A plate under the strip. Without it the discs float over whatever is
        // on the board behind the overlay - here, the player's own hand - and
        // read as loose pieces rather than as part of the card being inspected.
        const float plateW = shown * pitch + 24.0f;
        drawPanel(target, { cardCentre.x - plateW / 2.0f, stripY - 44.0f, plateW, 100.0f },
                  sf::Color(20, 18, 24, 244), sf::Color(88, 80, 96, 210));

        for (const Stat& stat : stats) {
            if (!stat.show) continue;

            sf::CircleShape disc(26.0f);
            disc.setOrigin(26.0f, 26.0f);
            disc.setPosition(sx, stripY);
            disc.setFillColor(sf::Color(stat.ink.r / 4, stat.ink.g / 4, stat.ink.b / 4, 235));
            disc.setOutlineThickness(2.0f);
            disc.setOutlineColor(stat.ink);
            target.draw(disc);

            sf::Text number;
            number.setFont(m_font);
            number.setString(stat.value);
            number.setCharacterSize(26);
            number.setStyle(sf::Text::Bold);
            number.setFillColor(sf::Color(246, 242, 236));
            TextUtils::centerBoth(number);
            number.setPosition(sx, stripY - 2.0f);
            target.draw(number);

            drawLabel(target, stat.label, { sx - 30.0f, stripY + 34.0f }, 11,
                      sf::Color(stat.ink.r, stat.ink.g, stat.ink.b, 210), 2.0f);
            sx += pitch;
        }

        // Rules glossary down the left. Measure first so the panel hugs its
        // contents instead of leaving a slab of empty board below them.
        auto measure = [&](const std::vector<CardArt::GlossaryEntry>& list) {
            float h = 0.0f;
            for (const CardArt::GlossaryEntry& entry : list) {
                sf::Text probe;
                probe.setFont(Fonts::ui());
                probe.setCharacterSize(14);
                probe.setLineSpacing(1.28f);
                probe.setString(TextUtils::wrap(entry.text, m_font, 14, 464.0f));
                h += 26.0f + probe.getLocalBounds().height + 22.0f;
            }
            return h;
        };
        float needed = 96.0f + measure(m_glossary);
        if (!m_statuses.empty()) needed += 40.0f + measure(m_statuses);
        const float panelH = std::clamp(needed, 150.0f, 520.0f);
        const sf::FloatRect panel(150.0f, 350.0f - panelH / 2.0f, 520.0f, panelH);
        drawPanel(target, panel, sf::Color(22, 20, 26, 242), sf::Color(96, 88, 74, 210));

        float y = panel.top + 22.0f;

        // What is being done to this frame right now comes first: it is the
        // volatile half, and the reason the player opened the card.
        auto section = [&](const char* heading, sf::Color headingInk, sf::Color termInk,
                           const std::vector<CardArt::GlossaryEntry>& list) {
            if (list.empty()) return;

            drawLabel(target, heading, { panel.left + 26.0f, y }, 12,
                      headingInk, 3.0f);
            y += 24.0f;

            sf::RectangleShape rule({ panel.width - 52.0f, 1.0f });
            rule.setPosition(panel.left + 26.0f, y);
            rule.setFillColor(sf::Color(headingInk.r, headingInk.g, headingInk.b, 150));
            target.draw(rule);
            y += 16.0f;

            for (const CardArt::GlossaryEntry& entry : list) {
                if (y > panel.top + panel.height - 60.0f) break;

                sf::Text term;
                term.setFont(m_font);
                term.setString(entry.term);
                term.setCharacterSize(17);
                term.setStyle(sf::Text::Bold);
                term.setLetterSpacing(2.2f);
                term.setFillColor(termInk);
                term.setPosition(panel.left + 26.0f, y);
                target.draw(term);
                y += 26.0f;

                sf::Text body;
                body.setFont(Fonts::ui());
                body.setCharacterSize(14);
                body.setLineSpacing(1.28f);
                body.setFillColor(sf::Color(196, 190, 182));
                body.setString(TextUtils::wrap(entry.text, m_font, 14, panel.width - 56.0f));
                body.setPosition(panel.left + 26.0f, y);
                target.draw(body);
                y += body.getLocalBounds().height + 22.0f;

                sf::RectangleShape divider({ panel.width - 52.0f, 1.0f });
                divider.setPosition(panel.left + 26.0f, y - 11.0f);
                divider.setFillColor(sf::Color(64, 58, 50, 170));
                target.draw(divider);
            }
            y += 14.0f;
        };

        section("CURRENTLY AFFECTED BY", sf::Color(120, 200, 240),
                sf::Color(150, 220, 255), m_statuses);
        section("RULES ON THIS CARD", sf::Color(176, 158, 118),
                sf::Color(240, 214, 150), m_glossary);

        if (m_glossary.empty() && m_statuses.empty()) {
            drawLabel(target, "A plain card with no special rules.",
                      { panel.left + 26.0f, panel.top + 66.0f }, 15, sf::Color(160, 154, 148));
        }

        m_closeButton.render(target);
        drawLabel(target, "click anywhere to close",
                  { 940.0f, 664.0f }, 12, sf::Color(126, 120, 114));
    }
};

// =============================================================================
// MenuState implementation
// =============================================================================

MenuState::MenuState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font) {
    m_backdrop.load();

    // Three columns, not two. A single ramp from the edge was already half
    // faded by the time it reached the text at x 320, and the tagline sat on
    // the brightest part of the artwork. The scrim now holds full strength
    // past the whole text block and only then falls away.
    m_scrim = sf::VertexArray(sf::TriangleStrip, 6);
    const sf::Color deep(5, 11, 24, 232);
    const sf::Color mid(5, 11, 24, 214);
    const sf::Color clear(5, 11, 24, 0);
    m_scrim[0] = sf::Vertex({ 0.0f, 0.0f }, deep);
    m_scrim[1] = sf::Vertex({ 0.0f, 720.0f }, deep);
    m_scrim[2] = sf::Vertex({ 540.0f, 0.0f }, mid);
    m_scrim[3] = sf::Vertex({ 540.0f, 720.0f }, mid);
    m_scrim[4] = sf::Vertex({ 810.0f, 0.0f }, clear);
    m_scrim[5] = sf::Vertex({ 810.0f, 720.0f }, clear);

    m_titleText.setFont(font);
    m_titleText.setString("SANCTUM");
    m_titleText.setCharacterSize(70);
    m_titleText.setStyle(sf::Text::Bold);
    m_titleText.setLetterSpacing(4.0f);
    m_titleText.setFillColor(sf::Color(244, 248, 252));
    // Outlined, not drop-shadowed. The art behind this column is a lit figure
    // with pale hair, and a directional shadow only helps on one side of a
    // glyph; an outline holds the letterform against whatever is behind it.
    m_titleText.setOutlineColor(sf::Color(4, 8, 18, 225));
    m_titleText.setOutlineThickness(3.0f);
    TextUtils::centerBoth(m_titleText);
    m_titleText.setPosition(kColumnX, 164.0f);

    m_subtitleText.setFont(font);
    m_subtitleText.setString("MECHA-CHIVALRY");
    m_subtitleText.setCharacterSize(22);
    m_subtitleText.setLetterSpacing(7.0f);
    m_subtitleText.setFillColor(sf::Color(240, 96, 100));
    m_subtitleText.setOutlineColor(sf::Color(4, 8, 18, 225));
    m_subtitleText.setOutlineThickness(2.0f);
    TextUtils::centerBoth(m_subtitleText);
    m_subtitleText.setPosition(kColumnX, 220.0f);

    m_titleRule.setSize({ 300.0f, 1.0f });
    m_titleRule.setOrigin(150.0f, 0.5f);
    m_titleRule.setPosition(kColumnX, 254.0f);
    m_titleRule.setFillColor(sf::Color(238, 92, 96, 220));

    const char* tagline[] = {
        "Two doctrines, one reactor. Command the line.",
        "Hold the frontline, set your traps,",
        "and tribute your own frames to call down a titan."
    };
    float y = 290.0f;
    for (const char* line : tagline) {
        sf::Text text;
        text.setFont(font);
        text.setString(line);
        text.setCharacterSize(15);
        text.setFillColor(sf::Color(186, 206, 222));
        text.setOutlineColor(sf::Color(4, 8, 18, 215));
        text.setOutlineThickness(2.0f);
        TextUtils::centerBoth(text);
        text.setPosition(kColumnX, y);
        m_taglineLines.push_back(text);
        y += 24.0f;
    }

    m_startButton.setup(font, "NEW GAME", { kColumnX, 392.0f },
                        { 320.0f, 54.0f }, sf::Color(214, 60, 66), 20);
    // Second, not last: building a deck is the thing a returning player comes
    // back for, and burying it under SETTINGS would say the opposite.
    m_deckButton.setup(font, "DECK BUILDER", { kColumnX, 450.0f },
                       { 320.0f, 46.0f }, sf::Color(150, 176, 210), 17);
    m_settingsButton.setup(font, "SETTINGS", { kColumnX, 502.0f },
                           { 320.0f, 46.0f }, sf::Color(186, 172, 140), 17);
    // "DEPART" read as "set out" - the same thing NEW GAME does - so it was the
    // wrong word on the one button you cannot undo.
    m_quitButton.setup(font, "QUIT", { kColumnX, 554.0f },
                       { 320.0f, 46.0f }, sf::Color(150, 142, 130), 17);

    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMenu);
    layoutAvatars();

    // The old footer explained how to fight, on a screen where you cannot. It
    // belongs in the duel HUD, and here it only crowded the picker above it.
    m_footerText.setFont(Fonts::ui());
    m_footerText.setString(std::string("SANCTUM  ") + kGameVersion
                           + "        [ENTER] new game        [ESC] settings");
    m_footerText.setCharacterSize(12);
    m_footerText.setLetterSpacing(1.4f);
    m_footerText.setFillColor(sf::Color(118, 134, 152));
    m_footerText.setPosition(28.0f, 690.0f);
}

void MenuState::layoutAvatars() {
    m_avatarSlots.clear();
    const auto& pool = Avatars::pool();
    if (pool.empty()) return;

    // The row is centred on the menu column and shrinks rather than running off
    // it, so dropping a tenth portrait into assets/avatars/ does not push the
    // last one off the screen.
    const int count = static_cast<int>(pool.size());
    // Inside the panel, not across the whole column: the row has to leave the
    // panel's own border and label room to breathe.
    const float width = 312.0f;
    const float gap = 9.0f;
    float size = (width - (count - 1) * gap) / static_cast<float>(count);
    size = std::clamp(size, 24.0f, 40.0f);

    const float span = count * size + (count - 1) * gap;
    const float startX = kColumnX - span / 2.0f;
    for (int i = 0; i < count; ++i) {
        m_avatarSlots.push_back({ startX + i * (size + gap), kAvatarRowY, size, size });
    }
}

void MenuState::renderAvatars(sf::RenderTarget& target) const {
    const sf::FloatRect panel{ kColumnX - 178.0f, kProfileTop, 356.0f, kProfileH };
    drawPanel(target, panel, sf::Color(10, 14, 24, 216), sf::Color(96, 106, 122, 190));

    // A hairline across the top of the panel, under the label, so the group
    // reads as one control rather than as a box someone drew around a row.
    sf::RectangleShape rule({ panel.width - 26.0f, 1.0f });
    rule.setPosition(panel.left + 13.0f, panel.top + 23.0f);
    rule.setFillColor(sf::Color(86, 96, 112, 160));
    target.draw(rule);

    drawLabel(target, "PILOT PROFILE", { panel.left + 14.0f, panel.top + 8.0f }, 10,
              sf::Color(176, 192, 210), 3.0f, true);

    const auto& pool = Avatars::pool();
    if (pool.empty()) {
        drawLabel(target, "Drop images into assets/avatars/ to choose a face",
                  { panel.left + 14.0f, panel.top + 36.0f }, 12, sf::Color(112, 106, 112));
        return;
    }

    // An empty choice means "follow whichever doctrine I pick", which is a real
    // option and has to be visible as one - otherwise a player who never clicks
    // cannot tell the difference between that and a broken picker.
    const std::string picked = Avatars::chosen();

    for (std::size_t i = 0; i < m_avatarSlots.size() && i < pool.size(); ++i) {
        const sf::FloatRect box = m_avatarSlots[i];
        const bool selected = !picked.empty() && Avatars::label(pool[i]) == Avatars::label(picked);
        const bool hot = static_cast<int>(i) == m_avatarHovered;

        const sf::Color edge = selected ? sf::Color(236, 190, 74)
                             : hot      ? sf::Color(180, 190, 205)
                                        : sf::Color(84, 82, 92);
        CardArt::drawAvatar(target, pool[i], box, edge);

        if (selected) {
            // Two rings and four corner ticks. One thin gold outline was not
            // telling the eye anything the hover state did not already say.
            for (int ring = 0; ring < 2; ++ring) {
                const float pad = 3.0f + ring * 3.0f;
                sf::RectangleShape halo({ box.width + pad * 2.0f, box.height + pad * 2.0f });
                halo.setPosition(box.left - pad, box.top - pad);
                halo.setFillColor(sf::Color::Transparent);
                halo.setOutlineThickness(ring == 0 ? 2.5f : 1.0f);
                halo.setOutlineColor(ring == 0 ? sf::Color(248, 202, 92)
                                               : sf::Color(248, 202, 92, 90));
                target.draw(halo);
            }
            const float tick = 7.0f;
            for (int corner = 0; corner < 4; ++corner) {
                const float cx = (corner % 2 == 0) ? box.left - 6.0f
                                                   : box.left + box.width + 6.0f - tick;
                const float cy = (corner < 2) ? box.top - 6.0f
                                              : box.top + box.height + 6.0f - 2.0f;
                sf::RectangleShape bar({ tick, 2.0f });
                bar.setPosition(cx, cy);
                bar.setFillColor(sf::Color(252, 216, 120));
                target.draw(bar);
            }
        }
        // The hovered name goes in the panel header, right-aligned, NOT above
        // the thumbnail: above the thumbnail is exactly where the panel's own
        // label sits, and the two printed over each other.
        if (hot) {
            sf::Text name;
            name.setFont(Fonts::ui());
            name.setString(Avatars::label(pool[i]));
            name.setCharacterSize(10);
            name.setLetterSpacing(1.6f);
            name.setStyle(sf::Text::Bold);
            name.setFillColor(sf::Color(240, 218, 176));
            const sf::FloatRect bb = name.getLocalBounds();
            name.setPosition(panel.left + panel.width - 14.0f - bb.width, panel.top + 8.0f);
            target.draw(name);
        }
    }

    // Under the thumbnails but clear of the footer hint at y=686. The first
    // version put this at 686 exactly and the two lines printed over each other.
    drawLabel(target,
              picked.empty() ? "following your primary core" : "click again to follow your core",
              { panel.left + 14.0f, kAvatarRowY + m_avatarSlots.front().height + 7.0f }, 10,
              sf::Color(126, 138, 152), 1.4f);
}

void MenuState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_startButton.setHovered(m_startButton.contains(p));
        m_deckButton.setHovered(m_deckButton.contains(p));
        m_settingsButton.setHovered(m_settingsButton.contains(p));
        m_quitButton.setHovered(m_quitButton.contains(p));
        // Already mapped through the view: a raw pixel position would drift the
        // parallax the wrong way once the window is letterboxed.
        m_backdrop.setCursor(p);
        m_avatarHovered = -1;
        for (std::size_t i = 0; i < m_avatarSlots.size(); ++i) {
            if (m_avatarSlots[i].contains(p)) m_avatarHovered = static_cast<int>(i);
        }
    } else if (event.type == sf::Event::MouseButtonPressed &&
               event.mouseButton.button == sf::Mouse::Left) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
        const auto& pool = Avatars::pool();
        for (std::size_t i = 0; i < m_avatarSlots.size() && i < pool.size(); ++i) {
            if (!m_avatarSlots[i].contains(p)) continue;
            // Clicking the one already chosen clears the pick, which hands the
            // face back to whichever doctrine the run is built on.
            const std::string picked = Avatars::chosen();
            const bool already = !picked.empty() &&
                                 Avatars::label(pool[i]) == Avatars::label(picked);
            Avatars::choose(already ? std::string() : pool[i]);
            return;
        }

        if (m_startButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<RoleSelectState>(m_stateManager, m_font));
        } else if (m_deckButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<DeckListState>(m_stateManager, m_font));
        } else if (m_settingsButton.contains(p)) {
            m_stateManager.pushState(std::make_unique<SettingsState>(m_stateManager, m_font));
        } else if (m_quitButton.contains(p)) {
            m_stateManager.requestQuit();
        }
    } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        m_stateManager.pushState(std::make_unique<SettingsState>(m_stateManager, m_font));
    } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter) {
        m_stateManager.changeState(std::make_unique<RoleSelectState>(m_stateManager, m_font));
    }
}

void MenuState::update(float dt) {
    m_time += dt;
    m_backdrop.update(dt);
    const float pulse = 0.5f + 0.5f * std::sin(m_time * 1.35f);
    // Breathes between cool white and a faint crimson wash, picking up the red
    // in the artwork instead of the gold the old dark background wanted.
    m_titleText.setFillColor(sf::Color(246,
                                       static_cast<sf::Uint8>(214 + 34 * pulse),
                                       static_cast<sf::Uint8>(218 + 34 * pulse)));
}

void MenuState::render(sf::RenderTarget& target) {
    if (m_backdrop.hasArt()) {
        m_backdrop.render(target);
    } else {
        sf::RectangleShape fallback({ 1280.0f, 720.0f });
        fallback.setFillColor(sf::Color(15, 12, 22));
        target.draw(fallback);
    }
    target.draw(m_scrim);
    target.draw(m_titleText);
    target.draw(m_subtitleText);
    target.draw(m_titleRule);
    for (const auto& line : m_taglineLines) target.draw(line);
    m_startButton.render(target);
    m_deckButton.render(target);
    m_settingsButton.render(target);
    m_quitButton.render(target);
    renderAvatars(target);
    target.draw(m_footerText);
}

// =============================================================================
// RoleSelectState implementation
// =============================================================================

RoleSelectState::RoleSelectState(StateManager& sm, const sf::Font& font, Then then)
    : m_stateManager(sm), m_font(font), m_then(then) {
    layoutTiles();
    m_confirmButton.setup(font,
                          then == Then::EditDeck ? "EDIT THIS DECK" : "COMMIT LOADOUT",
                          { 640.0f, 660.0f },
                          { 320.0f, 48.0f }, sf::Color(236, 190, 74), 18);
    m_backButton.setup(font, "BACK", { 130.0f, 44.0f },
                       { 160.0f, 38.0f }, sf::Color(160, 150, 136), 14);
    m_compBar = { 640.0f - 210.0f, 588.0f, 420.0f, 16.0f };
    for (int i = 0; i < kMechRoleCount; ++i) {
        for (const CardData& card : DeckBuilder::cardsForRole(static_cast<MechRole>(i))) {
            if (card.tier != CardTier::Tier3) continue;
            m_titan[static_cast<size_t>(i)] = card;
            m_hasTitan[static_cast<size_t>(i)] = true;
            break;
        }
    }
    refreshPreview();
    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMenu);
}

void RoleSelectState::layoutTiles() {
    // Two rows of three, centred in the 1280x720 design space.
    constexpr float kTileW = 340.0f;
    // 190, not 168: the tile carries a Titan line now. Choosing a primary core
    // is what unlocks a Titan, and the screen used to say nothing at all about
    // which one you were unlocking.
    constexpr float kTileH = 190.0f;
    constexpr float kGapX = 24.0f;
    constexpr float kGapY = 16.0f;
    const float startX = (1280.0f - (kTileW * 3.0f + kGapX * 2.0f)) / 2.0f;
    const float startY = 168.0f;

    for (int i = 0; i < kMechRoleCount; ++i) {
        const int col = i % 3;
        const int row = i / 3;
        m_tiles[static_cast<size_t>(i)] = {
            startX + static_cast<float>(col) * (kTileW + kGapX),
            startY + static_cast<float>(row) * (kTileH + kGapY),
            kTileW, kTileH
        };
    }
}

void RoleSelectState::refreshPreview() {
    // What a run would actually field: the player's own deck for this pair when
    // they have built one, otherwise the generated deck. Otherwise the bar and
    // the tooltip would describe a deck the run is not going to use.
    m_preview = DeckStore::configurationFor(m_primary, m_secondary);
    m_confirmButton.enabled = m_preview.isValidDeck();
    const bool editing = m_then == Then::EditDeck;
    m_confirmButton.setText(!m_preview.isValidDeck() ? "LOADOUT INCOMPLETE"
                            : editing ? "EDIT THIS DECK" : "COMMIT LOADOUT");
}

void RoleSelectState::commit() {
    if (m_then == Then::EditDeck) {
        m_stateManager.changeState(
            std::make_unique<DeckEditState>(m_stateManager, m_font, m_primary, m_secondary));
        return;
    }
    g_run.startNewRun(m_primary, m_secondary);
    m_stateManager.changeState(std::make_unique<MapState>(m_stateManager, m_font));
}

void RoleSelectState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_mouse = p;
        m_hovered = -1;
        for (int i = 0; i < kMechRoleCount; ++i) {
            if (m_tiles[static_cast<size_t>(i)].contains(p)) m_hovered = i;
        }
        m_confirmButton.setHovered(m_confirmButton.contains(p));
        m_backButton.setHovered(m_backButton.contains(p));
    } else if (event.type == sf::Event::MouseButtonPressed &&
               event.mouseButton.button == sf::Mouse::Left) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });

        for (int i = 0; i < kMechRoleCount; ++i) {
            if (!m_tiles[static_cast<size_t>(i)].contains(p)) continue;
            const MechRole picked = static_cast<MechRole>(i);
            if (m_pickingPrimary) {
                m_primary = picked;
                // The splash cannot be the same doctrine; slide it aside.
                if (m_secondary == m_primary) {
                    m_secondary = static_cast<MechRole>((i + 1) % kMechRoleCount);
                }
                m_pickingPrimary = false;
            } else if (picked != m_primary) {
                m_secondary = picked;
            }
            refreshPreview();
        }

        if (m_confirmButton.contains(p)) {
            commit();
        } else if (m_backButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<MenuState>(m_stateManager, m_font));
        }
    } else if (event.type == sf::Event::KeyPressed) {
        if (event.key.code == sf::Keyboard::Tab) {
            m_pickingPrimary = !m_pickingPrimary;
        } else if (event.key.code == sf::Keyboard::Enter) {
            if (m_confirmButton.enabled) commit();
        }
    }
}

void RoleSelectState::update(float dt) { m_time += dt; }

void RoleSelectState::drawDoctrineGlyph(sf::RenderTarget& target, MechRole role,
                                        sf::Vector2f c, float r, sf::Color colour) const {
    auto poly = [&](std::initializer_list<sf::Vector2f> pts) {
        sf::ConvexShape shape;
        shape.setPointCount(pts.size());
        std::size_t i = 0;
        for (sf::Vector2f pt : pts) shape.setPoint(i++, { c.x + pt.x * r, c.y + pt.y * r });
        shape.setFillColor(colour);
        target.draw(shape);
    };
    auto disc = [&](sf::Vector2f at, float rad, bool hollow) {
        sf::CircleShape circle(rad * r);
        circle.setOrigin(rad * r, rad * r);
        circle.setPosition(c.x + at.x * r, c.y + at.y * r);
        circle.setFillColor(hollow ? sf::Color::Transparent : colour);
        if (hollow) {
            circle.setOutlineThickness(0.13f * r);
            circle.setOutlineColor(colour);
        }
        target.draw(circle);
    };
    auto bar = [&](sf::Vector2f at, float w, float h, float deg) {
        sf::RectangleShape rect({ w * r, h * r });
        rect.setOrigin(w * r * 0.5f, h * r * 0.5f);
        rect.setPosition(c.x + at.x * r, c.y + at.y * r);
        rect.setRotation(deg);
        rect.setFillColor(colour);
        target.draw(rect);
    };

    switch (role) {
    case MechRole::Vanguard:            // tower shield
        poly({ {0.0f,-1.0f}, {0.74f,-0.62f}, {0.74f,0.28f}, {0.0f,1.0f},
               {-0.74f,0.28f}, {-0.74f,-0.62f} });
        break;
    case MechRole::Paladin:             // overcharge core: a sun
        disc({ 0.0f, 0.0f }, 0.40f, false);
        for (int i = 0; i < 8; ++i) {
            bar({ 0.0f, 0.0f }, 0.16f, 1.95f, i * 22.5f);
        }
        break;
    case MechRole::Valkyrie:            // a wing, three tapered feathers
        for (int i = 0; i < 3; ++i) {
            const float lift = -0.34f * i;
            const float reach = 1.0f - 0.14f * i;
            poly({ {-0.9f, 0.42f + lift}, {reach, -0.22f + lift},
                   {reach * 0.86f, 0.10f + lift}, {-0.82f, 0.66f + lift} });
        }
        break;
    case MechRole::Dragoon:             // a lance
        bar({ 0.10f, 0.10f }, 1.55f, 0.15f, 38.0f);
        poly({ {0.92f,-0.86f}, {1.06f,-0.30f}, {0.58f,-0.46f}, {0.70f,-0.98f} });
        break;
    case MechRole::Siege:               // barrel over a track wheel
        bar({ 0.06f, -0.30f }, 1.45f, 0.40f, -18.0f);
        disc({ 0.78f, -0.55f }, 0.24f, true);
        disc({ -0.30f, 0.50f }, 0.42f, true);
        break;
    case MechRole::Inquisitor:          // a watching lens
        poly({ {-1.0f,0.0f}, {-0.42f,-0.62f}, {0.42f,-0.62f}, {1.0f,0.0f},
               {0.42f,0.62f}, {-0.42f,0.62f} });
        disc({ 0.0f, 0.0f }, 0.30f, true);
        break;
    }
}

void RoleSelectState::drawRoleTile(sf::RenderTarget& target, MechRole role, int slot) const {
    const sf::FloatRect bounds = m_tiles[static_cast<size_t>(slot)];

    CardData probe;
    probe.role = role;
    const sf::Color accent = CardArt::accentFor(probe);

    const bool isPrimary = role == m_primary;
    const bool isSecondary = role == m_secondary;
    const bool hovered = slot == m_hovered;
    const bool blocked = !m_pickingPrimary && isPrimary;

    sf::Color fill(20, 22, 28, 235);
    if (isPrimary)        fill = sf::Color(accent.r / 5 + 24, accent.g / 5 + 22, accent.b / 6 + 20, 245);
    else if (isSecondary) fill = sf::Color(accent.r / 9 + 20, accent.g / 9 + 20, accent.b / 10 + 22, 240);
    else if (hovered)     fill = sf::Color(30, 32, 38, 240);

    sf::Color outline(74, 78, 86, 190);
    if (isPrimary)        outline = accent;
    else if (isSecondary) outline = sf::Color(accent.r, accent.g, accent.b, 170);
    else if (hovered)     outline = sf::Color(accent.r, accent.g, accent.b, 130);

    // The primary core decides the passive AND is the only slot that may field
    // a Titan. It should not look like the splash with a different word on it,
    // so the whole tile carries a breathing halo in its own doctrine colour.
    if (isPrimary) {
        const float pulse = 0.5f + 0.5f * std::sin(m_time * 2.4f);
        for (int ring = 3; ring >= 1; --ring) {
            const float pad = static_cast<float>(ring) * 2.6f;
            sf::RectangleShape halo({ bounds.width + pad * 2.0f, bounds.height + pad * 2.0f });
            halo.setPosition(bounds.left - pad, bounds.top - pad);
            halo.setFillColor(sf::Color::Transparent);
            halo.setOutlineThickness(1.6f);
            const float fade = (1.0f - ring / 4.0f) * (0.45f + 0.55f * pulse);
            halo.setOutlineColor(sf::Color(accent.r, accent.g, accent.b,
                                           static_cast<sf::Uint8>(150.0f * fade)));
            target.draw(halo);
        }
    }

    drawPanel(target, bounds, fill, outline);

    // The emblem, sunk into the tile rather than laid on it. Drawn before the
    // text so a long passive line always wins.
    drawDoctrineGlyph(target, role,
                      { bounds.left + bounds.width - 62.0f, bounds.top + 108.0f }, 38.0f,
                      sf::Color(accent.r, accent.g, accent.b, blocked ? 22 : 40));

    // A thicker bar down the left edge reads as the doctrine colour at a glance.
    sf::RectangleShape stripe({ 5.0f, bounds.height - 2.0f });
    stripe.setPosition(bounds.left + 1.0f, bounds.top + 1.0f);
    stripe.setFillColor(blocked ? sf::Color(70, 70, 74) : accent);
    target.draw(stripe);

    const sf::Color textColour = blocked ? sf::Color(120, 118, 116) : sf::Color(238, 232, 222);
    const float x = bounds.left + 22.0f;

    drawLabel(target, displayName(role), { x, bounds.top + 16.0f }, 21,
              blocked ? sf::Color(120, 118, 116) : accent, 2.0f, true);
    drawLabel(target, roleTitle(role), { x, bounds.top + 46.0f }, 13,
              sf::Color(158, 156, 154));

    drawLabel(target, rolePassiveName(role), { x, bounds.top + 76.0f }, 14,
              textColour, 1.0f, true);

    // The passive text is one long sentence; wrap it by hand to the tile width.
    const std::string passive = rolePassiveText(role);
    std::string line;
    float y = bounds.top + 98.0f;
    std::istringstream words(passive);
    std::string word;
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (candidate.size() > 44) {
            drawLabel(target, line, { x, y }, 12, sf::Color(176, 172, 168));
            y += 17.0f;
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) drawLabel(target, line, { x, y }, 12, sf::Color(176, 172, 168));

    // The Titan. Choosing a primary core is the decision that unlocks one, and
    // the screen used to name every passive but never the thing the passive is
    // building toward - so the choice was made half blind.
    sf::RectangleShape rule({ bounds.width - 44.0f, 1.0f });
    rule.setPosition(x, bounds.top + 152.0f);
    rule.setFillColor(sf::Color(accent.r, accent.g, accent.b, blocked ? 40 : 90));
    target.draw(rule);

    const size_t index = static_cast<size_t>(role);
    if (m_hasTitan[index]) {
        const CardData& titan = m_titan[index];
        std::ostringstream titanLine;
        titanLine << "TITAN  " << titan.name << "   " << titan.manaCost << " energy   "
                  << titan.attack << "/" << titan.health;
        drawLabel(target, titanLine.str(), { x, bounds.top + 162.0f }, 11,
                  blocked ? sf::Color(104, 102, 100)
                          : sf::Color(accent.r, accent.g, accent.b, 230), 1.2f, true);
    } else {
        drawLabel(target, "NO TITAN IN THIS CORE", { x, bounds.top + 162.0f }, 11,
                  sf::Color(104, 102, 100), 1.2f);
    }

    // Slot badge. Two different objects, not one word swapped: the primary is a
    // commitment, the splash is a garnish, and they should not read alike.
    if (isPrimary || isSecondary) {
        const char* tag = isPrimary ? "PRIMARY CORE" : "TACTICAL SPLASH";
        const float w = isPrimary ? 106.0f : 118.0f;
        sf::FloatRect badge { bounds.left + bounds.width - w - 12.0f, bounds.top + 12.0f,
                              w, 22.0f };
        if (isPrimary) {
            drawPanel(target, badge,
                      sf::Color(accent.r / 3 + 18, accent.g / 3 + 16, accent.b / 4 + 14, 248),
                      accent);
            // A filled pip in front of the word, so the badge reads as "live".
            sf::CircleShape pip(3.0f);
            pip.setOrigin(3.0f, 3.0f);
            pip.setPosition(badge.left + 11.0f, badge.top + badge.height / 2.0f);
            pip.setFillColor(accent);
            target.draw(pip);
            drawLabel(target, tag, { badge.left + 19.0f, badge.top + 5.0f }, 10, accent, 1.2f, true);
        } else {
            // Silver and hollow: a splash is not a commitment.
            drawPanel(target, badge, sf::Color(12, 13, 17, 200), sf::Color(150, 156, 168, 190));
            drawLabel(target, tag, { badge.left + 9.0f, badge.top + 5.0f }, 10,
                      sf::Color(178, 184, 196), 1.2f);
        }
    }
}

void RoleSelectState::drawCompositionBar(sf::RenderTarget& target) const {
    CardData probeP, probeS;
    probeP.role = m_primary;
    probeS.role = m_secondary;
    const sf::Color colourP = CardArt::accentFor(probeP);
    const sf::Color colourS = CardArt::accentFor(probeS);

    const int countP = m_preview.countFor(m_primary);
    const int countS = m_preview.countFor(m_secondary);
    const int total = std::max(1, countP + countS);

    drawPanel(target, m_compBar, sf::Color(12, 13, 18, 235), sf::Color(70, 74, 84, 180));

    // Two segments, sized by share. The numbers were already on screen as text;
    // what was missing was the RATIO, which is the thing that decides how often
    // the splash actually turns up in a hand.
    const float inner = m_compBar.width - 4.0f;
    const float widthP = inner * static_cast<float>(countP) / static_cast<float>(total);

    sf::RectangleShape segP({ widthP, m_compBar.height - 4.0f });
    segP.setPosition(m_compBar.left + 2.0f, m_compBar.top + 2.0f);
    segP.setFillColor(colourP);
    target.draw(segP);

    sf::RectangleShape segS({ inner - widthP, m_compBar.height - 4.0f });
    segS.setPosition(m_compBar.left + 2.0f + widthP, m_compBar.top + 2.0f);
    segS.setFillColor(sf::Color(colourS.r, colourS.g, colourS.b, 205));
    target.draw(segS);

    if (!m_compBar.contains(m_mouse)) return;

    // Hovering asks the other question: not who the cards belong to, but what
    // they DO - which is what decides whether the deck can hold a line.
    int units = 0, spells = 0, counters = 0;
    for (const CardData& card : m_preview.cards) {
        if (card.category == CardCategory::Spell) ++spells;
        else if (card.category == CardCategory::Trap) ++counters;
        else ++units;
    }
    std::ostringstream tip;
    tip << units << " Units    " << spells << " Spells    " << counters << " Counters";

    const sf::FloatRect box{ m_compBar.left + m_compBar.width / 2.0f - 120.0f,
                             m_compBar.top - 32.0f, 240.0f, 26.0f };
    drawPanel(target, box, sf::Color(10, 11, 16, 246), sf::Color(150, 132, 84, 210));
    sf::Text tipText;
    tipText.setFont(Fonts::ui());
    tipText.setString(tip.str());
    tipText.setCharacterSize(12);
    tipText.setFillColor(sf::Color(232, 224, 208));
    TextUtils::centerBoth(tipText);
    tipText.setPosition(box.left + box.width / 2.0f, box.top + box.height / 2.0f);
    target.draw(tipText);
}

void RoleSelectState::render(sf::RenderTarget& target) {
    sf::RectangleShape backdrop({ 1280.0f, 720.0f });
    backdrop.setFillColor(sf::Color(12, 13, 17));
    target.draw(backdrop);

    drawLabel(target, "DUAL-CORE PROTOCOL", { 640.0f - 150.0f, 54.0f }, 26,
              sf::Color(236, 214, 178), 4.0f, true);

    std::ostringstream brief;
    brief << "Choose a primary core (" << DeckRules::kMinPrimary << "+ cards, may field its Titan)"
          << "  and a secondary splash (up to " << DeckRules::kMaxSecondary
          << " cards, no Titan).  " << DeckRules::kDeckSize << " cards total.";
    drawLabel(target, brief.str(), { 210.0f, 94.0f }, 13, sf::Color(150, 148, 146));

    drawLabel(target,
              m_pickingPrimary ? "> SELECTING PRIMARY CORE" : "> SELECTING SECONDARY CORE",
              { 210.0f, 118.0f }, 13, sf::Color(236, 190, 74), 2.0f, true);
    drawLabel(target, "TAB switches slot", { 960.0f, 118.0f }, 12,
              sf::Color(120, 118, 116));

    for (int i = 0; i < kMechRoleCount; ++i) {
        drawRoleTile(target, static_cast<MechRole>(i), i);
    }
    // The loadout, as a ratio and as a sentence. The bar answers "how much of
    // my deck is the splash" at a glance; the line underneath keeps the exact
    // counts, because a bar cannot be counted.
    drawCompositionBar(target);

    std::ostringstream summary;
    summary << displayName(m_primary) << " " << m_preview.countFor(m_primary)
            << "   /   " << displayName(m_secondary) << " " << m_preview.countFor(m_secondary);
    if (!m_preview.isValidDeck()) summary << "   -   " << toString(m_preview.validate());

    sf::Text line;
    line.setFont(m_font);
    line.setString(summary.str());
    line.setCharacterSize(16);
    line.setLetterSpacing(1.6f);
    line.setFillColor(m_preview.isValidDeck() ? sf::Color(214, 208, 198) : sf::Color(214, 120, 110));
    TextUtils::centerBoth(line);
    line.setPosition(640.0f, 616.0f);
    target.draw(line);

    // A ready button should look ready. The halo only appears once the pair
    // actually builds a legal deck.
    if (m_confirmButton.enabled) {
        const float pulse = 0.5f + 0.5f * std::sin(m_time * 3.1f);
        sf::FloatRect box = m_confirmButton.box.getGlobalBounds();
        for (int ring = 2; ring >= 1; --ring) {
            const float pad = static_cast<float>(ring) * 3.0f;
            sf::RectangleShape halo({ box.width + pad * 2.0f, box.height + pad * 2.0f });
            halo.setPosition(box.left - pad, box.top - pad);
            halo.setFillColor(sf::Color::Transparent);
            halo.setOutlineThickness(1.5f);
            halo.setOutlineColor(sf::Color(236, 190, 74,
                static_cast<sf::Uint8>((1.0f - ring / 3.0f) * (60.0f + 110.0f * pulse))));
            target.draw(halo);
        }
    }

    m_confirmButton.render(target);
    m_backButton.render(target);
    if (!m_confirmButton.enabled) {
        drawLabel(target, toString(m_preview.validate()),
                  { 640.0f - 110.0f, 690.0f }, 11, sf::Color(206, 122, 112), 1.6f);
    }
}


// =============================================================================
// DeckEditState implementation
// =============================================================================

void DeckEditState::buildPool() {
    // The pool is the primary core in full plus the secondary minus its Titan.
    // Filtering here rather than at click time means an illegal card is never
    // on screen to be clicked in the first place.
    for (const CardData& card : DeckBuilder::cardsForRole(m_primary)) m_pool.push_back(card);
    if (m_secondary != m_primary) {
        for (const CardData& card : DeckBuilder::cardsForRole(m_secondary)) {
            if (DeckBuilder::allowedAsSecondary(card)) m_pool.push_back(card);
        }
    }
    std::sort(m_pool.begin(), m_pool.end(), [](const CardData& a, const CardData& b) {
        if (a.manaCost != b.manaCost) return a.manaCost < b.manaCost;
        return a.name < b.name;
    });
    applyFilter();
}

void DeckEditState::setupChrome() {
    m_backButton.setup(m_font, "BACK", { 110.0f, 30.0f }, { 150.0f, 34.0f },
                       sf::Color(160, 150, 136), 14);
    m_saveButton.setup(m_font, "SAVE DECK", { 1074.0f, 646.0f }, { 200.0f, 40.0f },
                       sf::Color(236, 190, 74), 16);
    m_autoButton.setup(m_font, "AUTO-FILL", { 952.0f, 600.0f }, { 106.0f, 28.0f },
                       sf::Color(150, 170, 200), 12);
    m_clearButton.setup(m_font, "CLEAR", { 1068.0f, 600.0f }, { 92.0f, 28.0f },
                        sf::Color(178, 132, 126), 12);
    refreshButtons();
    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMenu);
}

void DeckEditState::applyFilter() {
    m_shown.clear();
    // Case-insensitive substring on the name. Nothing cleverer: the catalogue
    // is seventy-odd cards and a player typing "bast" wants Bastion, not a
    // ranked search.
    std::string needle = m_search;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (int i = 0; i < static_cast<int>(m_pool.size()); ++i) {
        const CardData& card = m_pool[static_cast<size_t>(i)];
        if (m_show == Show::Units && card.category != CardCategory::Unit) continue;
        if (m_show == Show::Spells && card.category != CardCategory::Spell) continue;
        if (m_show == Show::Counters && card.category != CardCategory::Trap) continue;
        if (m_roleFilter == 0 && card.role != m_primary) continue;
        if (m_roleFilter == 1 && card.role != m_secondary) continue;
        if (!needle.empty()) {
            std::string hay = card.name;
            std::transform(hay.begin(), hay.end(), hay.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (hay.find(needle) == std::string::npos) continue;
        }
        m_shown.push_back(i);
    }

    // A filter that leaves fewer rows than the current scroll would show an
    // empty grid over real results.
    const int rows = (static_cast<int>(m_shown.size()) + kCols - 1) / kCols;
    m_scroll = std::clamp(m_scroll, 0, std::max(0, rows - kRows));
    m_hoverCard = -1;
}

DeckEditState::DeckEditState(StateManager& sm, const sf::Font& font,
                             MechRole primary, MechRole secondary)
    : m_stateManager(sm), m_font(font), m_primary(primary), m_secondary(secondary) {
    m_deckName = DeckStore::suggestName(primary, secondary);
    buildPool();
    // Open on whatever the player would get anyway: their newest deck for this
    // pair if they have one, otherwise the generated deck. A blank grid would
    // make the screen look like work before it looks like a choice.
    setFromConfiguration(DeckStore::configurationFor(primary, secondary));
    setupChrome();
}

DeckEditState::DeckEditState(StateManager& sm, const sf::Font& font,
                             const DeckStore::SavedDeck& deck)
    : m_stateManager(sm), m_font(font),
      m_primary(deck.primary), m_secondary(deck.secondary),
      m_deckId(deck.id), m_deckName(deck.name) {
    buildPool();
    setFromConfiguration(DeckStore::configurationOf(deck));
    setupChrome();
}

int DeckEditState::total() const {
    int sum = 0;
    for (const auto& entry : m_counts) sum += entry.second;
    return sum;
}

int DeckEditState::countForRole(MechRole role) const {
    int sum = 0;
    for (const auto& entry : m_counts) {
        const CardData* card = DataLoader::findCard(entry.first);
        if (card && card->role == role) sum += entry.second;
    }
    return sum;
}

int DeckEditState::copiesOf(const std::string& id) const {
    auto found = m_counts.find(id);
    return found == m_counts.end() ? 0 : found->second;
}

std::string DeckEditState::refuseReason(const CardData& card) const {
    if (total() >= DeckRules::kDeckSize) {
        return "Deck is full at " + std::to_string(DeckRules::kDeckSize) + " cards";
    }
    if (copiesOf(card.id) >= card.deckCount) {
        return card.name + " allows " + std::to_string(card.deckCount)
             + (card.deckCount == 1 ? " copy" : " copies");
    }
    if (card.role == m_secondary && m_secondary != m_primary &&
        countForRole(m_secondary) >= DeckRules::kMaxSecondary) {
        return std::string(displayName(m_secondary)) + " splash caps at "
             + std::to_string(DeckRules::kMaxSecondary) + " cards";
    }
    return {};
}

void DeckEditState::addCard(const CardData& card) {
    const std::string reason = refuseReason(card);
    if (!reason.empty()) { say(reason); return; }
    if (copiesOf(card.id) == 0) m_order.push_back(card.id);
    m_counts[card.id] += 1;
    refreshButtons();
}

void DeckEditState::removeCard(const std::string& id) {
    auto found = m_counts.find(id);
    if (found == m_counts.end()) return;
    if (--found->second <= 0) {
        m_counts.erase(found);
        m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
    }
    refreshButtons();
}

void DeckEditState::setFromConfiguration(const DeckConfiguration& config) {
    m_counts.clear();
    m_order.clear();
    for (const CardData& card : config.cards) {
        if (m_counts[card.id]++ == 0) m_order.push_back(card.id);
    }
    // Keep the list in the same order the grid is in, so a card is where the
    // player expects it after an auto-fill.
    std::sort(m_order.begin(), m_order.end(), [](const std::string& a, const std::string& b) {
        const CardData* ca = DataLoader::findCard(a);
        const CardData* cb = DataLoader::findCard(b);
        if (!ca || !cb) return a < b;
        if (ca->manaCost != cb->manaCost) return ca->manaCost < cb->manaCost;
        return ca->name < cb->name;
    });
}

DeckConfiguration DeckEditState::asConfiguration() const {
    DeckConfiguration config;
    config.primaryRole = m_primary;
    config.secondaryRole = m_secondary;
    for (const std::string& id : m_order) {
        const CardData* card = DataLoader::findCard(id);
        if (!card) continue;
        for (int copy = 0; copy < copiesOf(id); ++copy) config.cards.push_back(*card);
    }
    return config;
}

void DeckEditState::refreshButtons() {
    const DeckConfiguration config = asConfiguration();
    m_saveButton.enabled = config.isValidDeck();
    m_clearButton.enabled = total() > 0;
}

sf::FloatRect DeckEditState::cellAt(int visibleIndex) const {
    const int col = visibleIndex % kCols;
    const int row = visibleIndex / kCols;
    return { kGridX + col * (kCardW + kGapX), kGridY + row * (kCardH + kGapY), kCardW, kCardH };
}

int DeckEditState::cardUnder(sf::Vector2f point) const {
    const int first = m_scroll * kCols;
    for (int i = 0; i < kCols * kRows; ++i) {
        const int slot = first + i;
        if (slot >= static_cast<int>(m_shown.size())) break;
        if (cellAt(i).contains(point)) return m_shown[static_cast<size_t>(slot)];
    }
    return -1;
}

sf::FloatRect DeckEditState::rowRect(int index) const {
    const sf::FloatRect panel = listPanel();
    return { panel.left + 10.0f, panel.top + 60.0f + index * 21.0f, panel.width - 20.0f, 20.0f };
}

int DeckEditState::rowUnder(sf::Vector2f point) const {
    for (int i = 0; i < static_cast<int>(m_order.size()); ++i) {
        if (rowRect(i).contains(point)) return i;
    }
    return -1;
}

void DeckEditState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    // Typing goes to whichever field is focused. One field is focused at a
    // time and clicking elsewhere drops focus, so a keystroke can never end up
    // in two places.
    if (event.type == sf::Event::TextEntered) {
        const sf::Uint32 ch = event.text.unicode;
        std::string& field = m_renaming ? m_deckName : m_search;
        const bool searching = !m_renaming;
        if (ch == 8) {                                   // backspace
            if (!field.empty()) field.pop_back();
            if (searching) applyFilter();
            return;
        }
        if (ch == 13) { m_renaming = false; return; }    // enter commits a rename
        if (ch == 27) {                                  // escape clears
            if (m_renaming) { m_renaming = false; return; }
            if (!m_search.empty()) { m_search.clear(); applyFilter(); return; }
        }
        if (ch >= 32 && ch < 127 && field.size() < (m_renaming ? 28u : 20u)) {
            field.push_back(static_cast<char>(ch));
            if (searching) applyFilter();
        }
        return;
    }

    if (event.type == sf::Event::MouseMoved) {
        m_mouse = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_hoverCard = cardUnder(m_mouse);
        m_hoverRow = rowUnder(m_mouse);
        m_backButton.setHovered(m_backButton.contains(m_mouse));
        m_saveButton.setHovered(m_saveButton.contains(m_mouse));
        m_autoButton.setHovered(m_autoButton.contains(m_mouse));
        m_clearButton.setHovered(m_clearButton.contains(m_mouse));

    } else if (event.type == sf::Event::MouseWheelScrolled) {
        const int rows = (static_cast<int>(m_shown.size()) + kCols - 1) / kCols;
        const int maxScroll = std::max(0, rows - kRows);
        m_scroll = std::clamp(m_scroll - static_cast<int>(event.mouseWheelScroll.delta),
                              0, maxScroll);
        m_hoverCard = cardUnder(m_mouse);

    } else if (event.type == sf::Event::MouseButtonPressed) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });

        if (event.mouseButton.button == sf::Mouse::Right) {
            // Right-click takes a copy back out, wherever you are pointing.
            const int card = cardUnder(p);
            if (card >= 0) removeCard(m_pool[static_cast<size_t>(card)].id);
            return;
        }
        if (event.mouseButton.button != sf::Mouse::Left) return;

        const int card = cardUnder(p);
        if (card >= 0) { addCard(m_pool[static_cast<size_t>(card)]); return; }

        const int row = rowUnder(p);
        if (row >= 0) { removeCard(m_order[static_cast<size_t>(row)]); return; }

        // Focus follows the click, and lands nowhere by default.
        m_renaming = nameField().contains(p);
        if (m_renaming) return;

        if (searchField().contains(p)) { return; }
        for (int chip = 0; chip < 4; ++chip) {
            if (!chipRect(chip).contains(p)) continue;
            m_show = static_cast<Show>(chip);
            applyFilter();
            return;
        }
        for (int chip = 0; chip < 3; ++chip) {
            if (!roleChipRect(chip).contains(p)) continue;
            m_roleFilter = chip - 1;    // 0 -> both, 1 -> primary, 2 -> secondary
            applyFilter();
            return;
        }

        if (m_backButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<DeckListState>(m_stateManager, m_font));
        } else if (m_saveButton.contains(p)) {
            // Saving keeps the id, so editing a deck twice does not leave two
            // copies behind; a deck that has never been saved mints one.
            m_deckId = DeckStore::store(m_deckId, m_deckName, m_primary, m_secondary,
                                        asConfiguration().cards);
            m_stateManager.changeState(std::make_unique<DeckListState>(m_stateManager, m_font));
        } else if (m_autoButton.contains(p)) {
            setFromConfiguration(DeckBuilder::build(m_primary, m_secondary));
            refreshButtons();
            say("Filled with the generated deck");
        } else if (m_clearButton.contains(p)) {
            m_counts.clear();
            m_order.clear();
            refreshButtons();
        }

    } else if (event.type == sf::Event::KeyPressed &&
               event.key.code == sf::Keyboard::Escape) {
        if (m_renaming) { m_renaming = false; return; }
        if (!m_search.empty()) { m_search.clear(); applyFilter(); return; }
        m_stateManager.changeState(std::make_unique<DeckListState>(m_stateManager, m_font));
    }
}

void DeckEditState::update(float dt) {
    m_time += dt;
    if (m_noticeTimer > 0.0f) m_noticeTimer -= dt;
}


void DeckEditState::renderHeader(sf::RenderTarget& target) {
    sf::RectangleShape bar({ 1280.0f, 54.0f });
    bar.setFillColor(sf::Color(16, 18, 24, 248));
    target.draw(bar);
    sf::RectangleShape edge({ 1280.0f, 1.0f });
    edge.setPosition(0.0f, 54.0f);
    edge.setFillColor(sf::Color(84, 88, 98, 200));
    target.draw(edge);

    drawLabel(target, "DECK BUILDER", { 560.0f, 8.0f }, 17, sf::Color(236, 214, 178), 3.4f, true);

    CardData probeP, probeS;
    probeP.role = m_primary;
    probeS.role = m_secondary;
    drawLabel(target, displayName(m_primary), { 560.0f, 30.0f }, 11,
              CardArt::accentFor(probeP), 1.6f, true);
    drawLabel(target, "/", { 560.0f + 80.0f, 30.0f }, 11, sf::Color(120, 120, 128));
    drawLabel(target, displayName(m_secondary), { 560.0f + 92.0f, 30.0f }, 11,
              CardArt::accentFor(probeS), 1.6f);

    // ---- the filter strip -------------------------------------------------
    // Two rows of chips would cost a row of cards, so the core filter sits on
    // the left of the same strip as the search box and the kind filter.
    const char* roleNames[3] = { "BOTH", displayName(m_primary), displayName(m_secondary) };
    for (int chip = 0; chip < 3; ++chip) {
        const sf::FloatRect box = roleChipRect(chip);
        const bool on = m_roleFilter == chip - 1;
        CardData probe;
        probe.role = chip == 2 ? m_secondary : m_primary;
        const sf::Color accent = chip == 0 ? sf::Color(170, 176, 190)
                                           : CardArt::accentFor(probe);
        drawPanel(target, box,
                  on ? sf::Color(accent.r / 4 + 22, accent.g / 4 + 20, accent.b / 5 + 18, 245)
                     : sf::Color(18, 20, 26, 225),
                  on ? accent : sf::Color(74, 78, 88, 190));
        drawLabel(target, roleNames[chip], { box.left + 7.0f, box.top + 7.0f }, 10,
                  on ? accent : sf::Color(150, 154, 164), 1.2f, on);
    }

    const sf::FloatRect search = searchField();
    const bool typing = !m_renaming;
    drawPanel(target, search, sf::Color(16, 18, 24, 245),
              typing && !m_search.empty() ? sf::Color(200, 176, 110)
                                          : sf::Color(78, 82, 92, 200));
    drawLabel(target, m_search.empty() ? "SEARCH BY NAME" : m_search,
              { search.left + 9.0f, search.top + 7.0f }, 11,
              m_search.empty() ? sf::Color(104, 108, 118) : sf::Color(232, 226, 214), 1.2f);
    if (typing) {
        // A caret only where the typing would actually land.
        const float blink = std::fmod(m_time, 1.0f) < 0.55f ? 1.0f : 0.0f;
        if (blink > 0.0f && !m_search.empty()) {
            sf::Text probe;
            probe.setFont(Fonts::ui());
            probe.setString(m_search);
            probe.setCharacterSize(11);
            probe.setLetterSpacing(1.2f);
            sf::RectangleShape caret({ 1.0f, 13.0f });
            caret.setPosition(search.left + 11.0f + probe.getLocalBounds().width,
                              search.top + 7.0f);
            caret.setFillColor(sf::Color(236, 214, 178));
            target.draw(caret);
        }
    }

    const char* chips[4] = { "ALL", "UNITS", "SPELLS", "COUNTERS" };
    for (int chip = 0; chip < 4; ++chip) {
        const sf::FloatRect box = chipRect(chip);
        const bool on = static_cast<int>(m_show) == chip;
        drawPanel(target, box,
                  on ? sf::Color(34, 38, 48, 245) : sf::Color(18, 20, 26, 225),
                  on ? sf::Color(226, 190, 120) : sf::Color(74, 78, 88, 190));
        drawLabel(target, chips[chip], { box.left + 7.0f, box.top + 7.0f }, 10,
                  on ? sf::Color(240, 214, 160) : sf::Color(150, 154, 164), 1.2f, on);
    }

    // Scroll position, so a pool that runs past one screen says so.
    const int rows = (static_cast<int>(m_shown.size()) + kCols - 1) / kCols;
    if (rows > kRows) {
        std::ostringstream page;
        page << (m_scroll + 1) << " / " << (rows - kRows + 1);
        drawLabel(target, page.str(), { 1196.0f, 16.0f }, 11, sf::Color(130, 134, 144), 2.0f);
    }

    if (m_noticeTimer > 0.0f && !m_notice.empty()) {
        const float fade = std::min(1.0f, m_noticeTimer / 0.6f);
        drawLabel(target, m_notice, { kGridX, 678.0f }, 12,
                  sf::Color(232, 150, 120, static_cast<sf::Uint8>(235 * fade)), 1.2f, true);
    }
}

void DeckEditState::renderGrid(sf::RenderTarget& target) {
    if (m_shown.empty()) {
        drawLabel(target, "No card matches that filter",
                  { kGridX + 10.0f, kGridY + 40.0f }, 15, sf::Color(130, 134, 144), 1.6f);
        return;
    }
    const int first = m_scroll * kCols;
    for (int i = 0; i < kCols * kRows; ++i) {
        const int slot = first + i;
        if (slot >= static_cast<int>(m_shown.size())) break;
        const int index = m_shown[static_cast<size_t>(slot)];

        const CardData& card = m_pool[static_cast<size_t>(index)];
        const sf::FloatRect cell = cellAt(i);
        const int copies = copiesOf(card.id);
        const bool maxed = copies >= card.deckCount || !refuseReason(card).empty();
        const bool hot = index == m_hoverCard;

        CardArt::drawCard(target, m_font, card,
                          { cell.left + cell.width / 2.0f, cell.top + cell.height / 2.0f },
                          { cell.width, cell.height }, 0.0f, !maxed, hot);

        // Copies, as a strip of pips along the bottom edge. A number would be
        // read; pips are counted at a glance, which is what you want when the
        // question is "have I got room for one more".
        const float pipW = 13.0f;
        const float span = card.deckCount * pipW + (card.deckCount - 1) * 3.0f;
        float px = cell.left + (cell.width - span) / 2.0f;
        for (int pip = 0; pip < card.deckCount; ++pip) {
            sf::RectangleShape mark({ pipW, 5.0f });
            mark.setPosition(px, cell.top + cell.height - 9.0f);
            mark.setFillColor(pip < copies ? sf::Color(240, 200, 96)
                                           : sf::Color(30, 32, 40, 235));
            mark.setOutlineThickness(1.0f);
            mark.setOutlineColor(sf::Color(96, 92, 86, 200));
            target.draw(mark);
            px += pipW + 3.0f;
        }

        if (maxed) {
            sf::RectangleShape veil({ cell.width, cell.height });
            veil.setPosition(cell.left, cell.top);
            veil.setFillColor(sf::Color(6, 8, 14, 150));
            target.draw(veil);
        }
    }
}

void DeckEditState::renderList(sf::RenderTarget& target) {
    // The deck's name is a field, not a caption: a list of decks is only useful
    // if the decks can be told apart, and "Vanguard / Siege 3" tells you
    // nothing you did not already know.
    const sf::FloatRect field = nameField();
    drawPanel(target, field, sf::Color(16, 18, 24, 246),
              m_renaming ? sf::Color(236, 190, 74) : sf::Color(84, 88, 100, 200));
    drawLabel(target, m_deckName.empty() ? "UNNAMED DECK" : m_deckName,
              { field.left + 10.0f, field.top + 8.0f }, 12,
              m_deckName.empty() ? sf::Color(110, 114, 124) : sf::Color(234, 226, 210),
              1.4f, true);
    if (m_renaming && std::fmod(m_time, 1.0f) < 0.55f) {
        sf::Text probe;
        probe.setFont(Fonts::ui());
        probe.setString(m_deckName);
        probe.setCharacterSize(12);
        probe.setLetterSpacing(1.4f);
        sf::RectangleShape caret({ 1.0f, 14.0f });
        caret.setPosition(field.left + 12.0f + probe.getLocalBounds().width, field.top + 8.0f);
        caret.setFillColor(sf::Color(236, 214, 178));
        target.draw(caret);
    } else if (!m_renaming) {
        drawLabel(target, "CLICK TO RENAME",
                  { field.left + field.width - 96.0f, field.top + 9.0f }, 9,
                  sf::Color(96, 100, 110), 1.2f);
    }

    const sf::FloatRect panel = listPanel();
    drawPanel(target, panel, sf::Color(12, 14, 20, 242), sf::Color(92, 96, 108, 200));

    const DeckConfiguration config = asConfiguration();
    const int size = total();
    const int primaryCount = countForRole(m_primary);
    const int secondaryCount = countForRole(m_secondary);

    drawLabel(target, "YOUR DECK", { panel.left + 14.0f, panel.top + 12.0f }, 13,
              sf::Color(226, 214, 190), 3.0f, true);

    std::ostringstream count;
    count << size << " / " << DeckRules::kDeckSize;
    sf::Text tally;
    tally.setFont(Fonts::ui());
    tally.setString(count.str());
    tally.setCharacterSize(15);
    tally.setStyle(sf::Text::Bold);
    tally.setFillColor(size == DeckRules::kDeckSize ? sf::Color(150, 226, 160)
                                                    : sf::Color(226, 190, 120));
    const sf::FloatRect tb = tally.getLocalBounds();
    tally.setPosition(panel.left + panel.width - 14.0f - tb.width, panel.top + 10.0f);
    target.draw(tally);

    sf::RectangleShape rule({ panel.width - 28.0f, 1.0f });
    rule.setPosition(panel.left + 14.0f, panel.top + 36.0f);
    rule.setFillColor(sf::Color(80, 84, 94, 190));
    target.draw(rule);

    if (m_order.empty()) {
        drawLabel(target, "Click a card on the left to add it",
                  { panel.left + 14.0f, panel.top + 50.0f }, 12, sf::Color(120, 124, 134));
    }

    for (int i = 0; i < static_cast<int>(m_order.size()); ++i) {
        const std::string& id = m_order[static_cast<size_t>(i)];
        const CardData* card = DataLoader::findCard(id);
        if (!card) continue;
        const sf::FloatRect row = rowRect(i);
        // Stop well clear of the footer block: the list is allowed to run out
        // of room, but it may not run underneath the buttons.
        if (row.top + row.height > panel.top + panel.height - 138.0f) break;

        const sf::Color accent = CardArt::accentFor(*card);
        if (i == m_hoverRow) {
            sf::RectangleShape hi({ row.width, row.height });
            hi.setPosition(row.left, row.top);
            hi.setFillColor(sf::Color(accent.r / 5 + 26, accent.g / 5 + 24, accent.b / 6 + 22, 220));
            target.draw(hi);
        }

        // Cost gem, name, copies. The gem is the thing scanned when checking a
        // curve, so it leads.
        sf::CircleShape gem(8.0f);
        gem.setOrigin(8.0f, 8.0f);
        gem.setPosition(row.left + 12.0f, row.top + row.height / 2.0f);
        gem.setFillColor(sf::Color(accent.r / 2 + 30, accent.g / 2 + 26, accent.b / 2 + 20));
        gem.setOutlineThickness(1.0f);
        gem.setOutlineColor(accent);
        target.draw(gem);
        drawLabel(target, std::to_string(card->manaCost),
                  { row.left + (card->manaCost >= 10 ? 6.0f : 9.0f), row.top + 3.0f }, 11,
                  sf::Color(244, 238, 226), 1.0f, true);

        drawLabel(target, card->name, { row.left + 28.0f, row.top + 4.0f }, 12,
                  card->role == m_primary ? sf::Color(228, 224, 216)
                                          : sf::Color(178, 182, 192));

        const int copies = copiesOf(id);
        if (copies > 1) {
            drawLabel(target, "x" + std::to_string(copies),
                      { row.left + row.width - 24.0f, row.top + 4.0f }, 12,
                      sf::Color(236, 200, 120), 1.0f, true);
        }
    }

    // The two rules that are not obvious from the tally, stated as facts rather
    // than discovered by the save button refusing.
    const float footY = panel.top + panel.height - 128.0f;
    std::ostringstream mix;
    mix << displayName(m_primary) << " " << primaryCount
        << "   /   " << displayName(m_secondary) << " " << secondaryCount;
    drawLabel(target, mix.str(), { panel.left + 14.0f, footY }, 11,
              sf::Color(170, 176, 186), 1.2f);

    const bool primaryShort = primaryCount < DeckRules::kMinPrimary;
    std::ostringstream limits;
    limits << "primary min " << DeckRules::kMinPrimary
           << "    splash max " << DeckRules::kMaxSecondary;
    drawLabel(target, limits.str(), { panel.left + 14.0f, footY + 16.0f }, 10,
              primaryShort ? sf::Color(226, 140, 120) : sf::Color(124, 128, 138), 1.2f);

    if (!config.isValidDeck()) {
        drawLabel(target, toString(config.validate()),
                  { panel.left + 14.0f, footY + 32.0f }, 10,
                  sf::Color(212, 126, 116), 1.2f);
    }

    renderRowPreview(target);
}

void DeckEditState::renderRowPreview(sf::RenderTarget& target) const {
    // The deck has to be a text list: twenty-four cards will not fit on screen
    // as pictures, and the list is read as a curve rather than looked at. So
    // the picture arrives on hover instead of replacing the list.
    if (m_hoverRow < 0 || m_hoverRow >= static_cast<int>(m_order.size())) return;
    const CardData* card = DataLoader::findCard(m_order[static_cast<size_t>(m_hoverRow)]);
    if (!card) return;

    const sf::Vector2f size{ 246.0f, 338.0f };
    const sf::FloatRect panel = listPanel();
    // Left of the panel, and vertically level with the row being pointed at,
    // so the eye does not have to travel to find what it asked for.
    // Kept below the filter strip and above the screen edge. Without the upper
    // bound the preview climbed over the chips it is nothing to do with.
    const float topLimit = kGridY + size.y / 2.0f - 4.0f;
    const float centreY = std::clamp(rowRect(m_hoverRow).top + 10.0f,
                                     topLimit, 720.0f - size.y / 2.0f - 8.0f);
    const sf::Vector2f centre{ panel.left - size.x / 2.0f - 18.0f, centreY };

    // A shadow plate so the card never has to compete with the grid behind it.
    sf::RectangleShape shade({ size.x + 18.0f, size.y + 18.0f });
    shade.setOrigin(shade.getSize().x / 2.0f, shade.getSize().y / 2.0f);
    shade.setPosition(centre);
    shade.setFillColor(sf::Color(6, 7, 11, 232));
    shade.setOutlineThickness(1.0f);
    shade.setOutlineColor(sf::Color(120, 112, 96, 200));
    target.draw(shade);

    CardArt::drawCard(target, m_font, *card, centre, size, 0.0f, true, true);
}

void DeckEditState::render(sf::RenderTarget& target) {
    sf::RectangleShape backdrop({ 1280.0f, 720.0f });
    backdrop.setFillColor(sf::Color(11, 12, 16));
    target.draw(backdrop);

    renderGrid(target);
    renderList(target);
    renderHeader(target);

    m_backButton.render(target);
    m_autoButton.render(target);
    m_clearButton.render(target);
    if (m_saveButton.enabled) {
        const float pulse = 0.5f + 0.5f * std::sin(m_time * 3.1f);
        const sf::FloatRect box = m_saveButton.box.getGlobalBounds();
        sf::RectangleShape halo({ box.width + 6.0f, box.height + 6.0f });
        halo.setPosition(box.left - 3.0f, box.top - 3.0f);
        halo.setFillColor(sf::Color::Transparent);
        halo.setOutlineThickness(1.5f);
        halo.setOutlineColor(sf::Color(236, 190, 74,
            static_cast<sf::Uint8>(70.0f + 120.0f * pulse)));
        target.draw(halo);
    }
    m_saveButton.render(target);
}


// =============================================================================
// DeckListState implementation
// =============================================================================

DeckListState::DeckListState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font) {
    m_decks = DeckStore::all();   // a copy: the store is rewritten under us on delete
    m_backButton.setup(font, "BACK", { 110.0f, 30.0f }, { 150.0f, 34.0f },
                       sf::Color(160, 150, 136), 14);
    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMenu);
}

sf::FloatRect DeckListState::tileAt(int slot) const {
    const int col = slot % kCols;
    const int row = slot / kCols;
    return { kGridX + col * (kTileW + kGapX), kGridY + row * (kTileH + kGapY), kTileW, kTileH };
}

sf::FloatRect DeckListState::deleteAt(int slot) const {
    const sf::FloatRect tile = tileAt(slot);
    return { tile.left + tile.width - 26.0f, tile.top + 6.0f, 20.0f, 20.0f };
}

int DeckListState::slotUnder(sf::Vector2f point) const {
    const int slots = static_cast<int>(m_decks.size()) + 1;
    for (int slot = 0; slot < slots; ++slot) {
        if (tileAt(slot).contains(point)) return slot;
    }
    return -1;
}

bool DeckListState::isInUse(const DeckStore::SavedDeck& deck) const {
    const DeckStore::SavedDeck* newest = DeckStore::newestFor(deck.primary, deck.secondary);
    return newest && newest->id == deck.id;
}

void DeckListState::drawNewTile(sf::RenderTarget& target) const {
    const sf::FloatRect tile = tileAt(0);
    const bool hot = m_hovered == 0;
    drawPanel(target, tile,
              hot ? sf::Color(26, 32, 44, 245) : sf::Color(15, 18, 26, 235),
              hot ? sf::Color(226, 190, 120) : sf::Color(86, 92, 104, 200));

    // A plus sign, drawn rather than typed: a glyph at this size sits on the
    // baseline and reads as a letter.
    const sf::Vector2f centre{ tile.left + tile.width / 2.0f, tile.top + tile.height / 2.0f - 12.0f };
    const sf::Color ink = hot ? sf::Color(240, 214, 160) : sf::Color(150, 156, 168);
    for (int arm = 0; arm < 2; ++arm) {
        sf::RectangleShape bar(arm == 0 ? sf::Vector2f{ 46.0f, 5.0f } : sf::Vector2f{ 5.0f, 46.0f });
        bar.setOrigin(bar.getSize().x / 2.0f, bar.getSize().y / 2.0f);
        bar.setPosition(centre);
        bar.setFillColor(ink);
        target.draw(bar);
    }
    drawLabel(target, "NEW DECK",
              { tile.left + 14.0f, tile.top + tile.height - 44.0f }, 15, ink, 3.0f, true);
    drawLabel(target, "pick two cores, then build",
              { tile.left + 14.0f, tile.top + tile.height - 24.0f }, 10,
              sf::Color(112, 118, 130), 1.2f);
}

void DeckListState::drawDeckTile(sf::RenderTarget& target, int index) const {
    const DeckStore::SavedDeck& deck = m_decks[static_cast<size_t>(index)];
    const int slot = index + 1;
    const sf::FloatRect tile = tileAt(slot);
    const bool hot = m_hovered == slot;

    CardData probeP, probeS;
    probeP.role = deck.primary;
    probeS.role = deck.secondary;
    const sf::Color accentP = CardArt::accentFor(probeP);
    const sf::Color accentS = CardArt::accentFor(probeS);

    drawPanel(target, tile,
              hot ? sf::Color(accentP.r / 7 + 24, accentP.g / 7 + 26, accentP.b / 8 + 32, 246)
                  : sf::Color(14, 16, 22, 238),
              hot ? accentP : sf::Color(86, 92, 104, 200));

    // A broad band of the primary colour across the top, a thin one of the
    // secondary under it: the tile is identifiable across the room, which is
    // the job a deck tile actually has.
    sf::RectangleShape band({ tile.width - 2.0f, 46.0f });
    band.setPosition(tile.left + 1.0f, tile.top + 1.0f);
    band.setFillColor(sf::Color(accentP.r / 3 + 14, accentP.g / 3 + 14, accentP.b / 3 + 16, 240));
    target.draw(band);
    sf::RectangleShape under({ tile.width - 2.0f, 4.0f });
    under.setPosition(tile.left + 1.0f, tile.top + 47.0f);
    under.setFillColor(accentS);
    target.draw(under);

    drawLabel(target, displayName(deck.primary), { tile.left + 12.0f, tile.top + 10.0f }, 13,
              accentP, 1.8f, true);
    drawLabel(target, std::string("+ ") + displayName(deck.secondary),
              { tile.left + 12.0f, tile.top + 28.0f }, 10,
              sf::Color(accentS.r, accentS.g, accentS.b, 225), 1.2f);

    // The doctrine emblem, large and faint, as the tile's "art".
    {
        sf::ConvexShape dummy;   // keep the glyph helper's scope local
        (void)dummy;
    }

    drawLabel(target, deck.name, { tile.left + 12.0f, tile.top + 76.0f }, 14,
              sf::Color(234, 228, 216), 1.2f, true);

    // What is actually in it, counted from the catalogue rather than trusted
    // from the file.
    int units = 0, spells = 0, counters = 0;
    for (const std::string& id : deck.cardIds) {
        const CardData* card = DataLoader::findCard(id);
        if (!card) continue;
        if (card->category == CardCategory::Spell) ++spells;
        else if (card->category == CardCategory::Trap) ++counters;
        else ++units;
    }
    const int total = units + spells + counters;

    std::ostringstream mix;
    mix << units << " units   " << spells << " spells   " << counters << " counters";
    drawLabel(target, mix.str(), { tile.left + 12.0f, tile.top + 104.0f }, 10,
              sf::Color(158, 164, 176), 1.2f);

    std::ostringstream size;
    size << total << " / " << DeckRules::kDeckSize;
    drawLabel(target, size.str(), { tile.left + 12.0f, tile.top + 126.0f }, 16,
              total == DeckRules::kDeckSize ? sf::Color(150, 226, 160) : sf::Color(226, 150, 120),
              1.4f, true);

    // A stacked bar of the two cores, the same device the core-select screen
    // uses, so the two screens describe a deck the same way.
    int primaryCount = 0;
    for (const std::string& id : deck.cardIds) {
        const CardData* card = DataLoader::findCard(id);
        if (card && card->role == deck.primary) ++primaryCount;
    }
    const sf::FloatRect bar{ tile.left + 12.0f, tile.top + 156.0f, tile.width - 24.0f, 10.0f };
    drawPanel(target, bar, sf::Color(10, 12, 16, 235), sf::Color(64, 68, 78, 180));
    if (total > 0) {
        const float inner = bar.width - 4.0f;
        const float w = inner * static_cast<float>(primaryCount) / static_cast<float>(total);
        sf::RectangleShape segP({ w, bar.height - 4.0f });
        segP.setPosition(bar.left + 2.0f, bar.top + 2.0f);
        segP.setFillColor(accentP);
        target.draw(segP);
        sf::RectangleShape segS({ inner - w, bar.height - 4.0f });
        segS.setPosition(bar.left + 2.0f + w, bar.top + 2.0f);
        segS.setFillColor(sf::Color(accentS.r, accentS.g, accentS.b, 205));
        target.draw(segS);
    }

    if (isInUse(deck)) {
        const sf::FloatRect badge{ tile.left + 12.0f, tile.top + tile.height - 36.0f, 74.0f, 20.0f };
        drawPanel(target, badge, sf::Color(28, 40, 28, 245), sf::Color(140, 210, 150));
        drawLabel(target, "IN USE", { badge.left + 10.0f, badge.top + 4.0f }, 10,
                  sf::Color(160, 226, 172), 1.4f, true);
    } else {
        drawLabel(target, "a newer deck on these cores is in use",
                  { tile.left + 12.0f, tile.top + tile.height - 30.0f }, 9,
                  sf::Color(110, 116, 126), 1.0f);
    }

    // Delete, armed by the first click and done by the second. A deck is a lot
    // of work to lose to a stray cursor.
    const sf::FloatRect x = deleteAt(slot);
    const bool armed = m_deleteArmed == slot;
    if (hot || armed) {
        drawPanel(target, x, armed ? sf::Color(120, 30, 30, 246) : sf::Color(26, 24, 28, 235),
                  armed ? sf::Color(240, 140, 130) : sf::Color(130, 120, 120, 200));
        drawLabel(target, "X", { x.left + 7.0f, x.top + 4.0f }, 11,
                  armed ? sf::Color(255, 208, 200) : sf::Color(196, 186, 186), 1.0f, true);
    }
    if (armed) {
        drawLabel(target, "CLICK AGAIN TO DELETE",
                  { tile.left + 12.0f, tile.top + 56.0f }, 10,
                  sf::Color(244, 150, 140), 1.4f, true);
    }
}

void DeckListState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        m_mouse = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        const int was = m_hovered;
        m_hovered = slotUnder(m_mouse);
        // Leaving a tile disarms its delete, so an armed X cannot be triggered
        // later by a click meant for something else.
        if (m_hovered != was && m_deleteArmed != m_hovered) m_deleteArmed = -1;
        m_backButton.setHovered(m_backButton.contains(m_mouse));

    } else if (event.type == sf::Event::MouseButtonPressed &&
               event.mouseButton.button == sf::Mouse::Left) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });

        if (m_backButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<MenuState>(m_stateManager, m_font));
            return;
        }

        const int slot = slotUnder(p);
        if (slot < 0) { m_deleteArmed = -1; return; }

        if (slot > 0 && deleteAt(slot).contains(p)) {
            if (m_deleteArmed == slot) {
                DeckStore::remove(m_decks[static_cast<size_t>(slot - 1)].id);
                m_decks = DeckStore::all();
                m_deleteArmed = -1;
                m_hovered = -1;
            } else {
                m_deleteArmed = slot;
            }
            return;
        }
        m_deleteArmed = -1;

        if (slot == 0) {
            m_stateManager.changeState(std::make_unique<RoleSelectState>(
                m_stateManager, m_font, RoleSelectState::Then::EditDeck));
        } else {
            m_stateManager.changeState(std::make_unique<DeckEditState>(
                m_stateManager, m_font, m_decks[static_cast<size_t>(slot - 1)]));
        }

    } else if (event.type == sf::Event::KeyPressed &&
               event.key.code == sf::Keyboard::Escape) {
        m_stateManager.changeState(std::make_unique<MenuState>(m_stateManager, m_font));
    }
}

void DeckListState::render(sf::RenderTarget& target) {
    sf::RectangleShape backdrop({ 1280.0f, 720.0f });
    backdrop.setFillColor(sf::Color(11, 12, 16));
    target.draw(backdrop);

    sf::RectangleShape bar({ 1280.0f, 54.0f });
    bar.setFillColor(sf::Color(16, 18, 24, 248));
    target.draw(bar);
    sf::RectangleShape edge({ 1280.0f, 1.0f });
    edge.setPosition(0.0f, 54.0f);
    edge.setFillColor(sf::Color(84, 88, 98, 200));
    target.draw(edge);

    drawLabel(target, "YOUR DECKS", { 566.0f, 18.0f }, 19, sf::Color(236, 214, 178), 3.4f, true);

    std::ostringstream count;
    count << m_decks.size() << (m_decks.size() == 1 ? " deck saved" : " decks saved");
    drawLabel(target, count.str(), { 300.0f, 22.0f }, 11, sf::Color(130, 134, 144), 1.6f);

    drawLabel(target,
              "A run uses the newest deck built on the two cores you pick, or the "
              "generated deck when you have built none.",
              { kGridX, 84.0f }, 11, sf::Color(126, 132, 144), 1.2f);

    drawNewTile(target);
    for (int i = 0; i < static_cast<int>(m_decks.size()); ++i) {
        // Four across, two rows deep. Beyond eight decks the rest are off the
        // bottom - a scroll bar here is work for a case that does not exist yet.
        if (i + 1 >= kCols * 2) break;
        drawDeckTile(target, i);
    }
    if (static_cast<int>(m_decks.size()) + 1 > kCols * 2) {
        std::ostringstream more;
        more << (static_cast<int>(m_decks.size()) + 1 - kCols * 2) << " more not shown";
        drawLabel(target, more.str(), { kGridX, 690.0f }, 11, sf::Color(150, 130, 110), 1.4f);
    }

    m_backButton.render(target);
}

// =============================================================================
// MapState implementation
// =============================================================================

MapState::MapState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font) {
    m_marchButton.setup(font, "DEPLOY TO BATTLE", { 640.0f, 664.0f },
                        { 340.0f, 52.0f }, sf::Color(236, 190, 74), 19);
    m_settingsButton.setup(font, "SETTINGS", { 1160.0f, 44.0f },
                           { 180.0f, 38.0f }, sf::Color(160, 150, 136), 14);
    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMap);
    countDeck();
}

void MapState::countDeck() {
    m_units = m_spells = m_traps = 0;
    for (const CardData& card : g_run.getDeck()) {
        switch (card.category) {
        case CardCategory::Unit:  ++m_units;  break;
        case CardCategory::Spell: ++m_spells; break;
        case CardCategory::Trap:  ++m_traps;  break;
        }
    }
}

void MapState::drawGauge(sf::RenderTarget& target, sf::FloatRect box, float fill,
                         sf::Color colour, const std::string& caption) const {
    sf::RectangleShape track({ box.width, box.height });
    track.setPosition(box.left, box.top);
    track.setFillColor(sf::Color(30, 27, 34, 240));
    track.setOutlineThickness(1.0f);
    track.setOutlineColor(sf::Color(70, 64, 76));
    target.draw(track);

    const float clamped = std::clamp(fill, 0.0f, 1.0f);
    if (clamped > 0.0f) {
        sf::RectangleShape bar({ (box.width - 2.0f) * clamped, box.height - 2.0f });
        bar.setPosition(box.left + 1.0f, box.top + 1.0f);
        bar.setFillColor(colour);
        target.draw(bar);
    }

    sf::Text text;
    text.setFont(Fonts::ui());
    text.setString(caption);
    text.setCharacterSize(13);
    text.setStyle(sf::Text::Bold);
    text.setFillColor(sf::Color(244, 238, 228));
    text.setOutlineColor(sf::Color(0, 0, 0, 190));
    text.setOutlineThickness(1.5f);
    TextUtils::centerBoth(text);
    text.setPosition(box.left + box.width / 2.0f, box.top + box.height / 2.0f - 1.0f);
    target.draw(text);
}

void MapState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_marchButton.setHovered(m_marchButton.contains(p));
        m_settingsButton.setHovered(m_settingsButton.contains(p));
    } else if (event.type == sf::Event::MouseButtonPressed &&
               event.mouseButton.button == sf::Mouse::Left) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
        if (m_marchButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<DuelState>(m_stateManager, m_font));
        } else if (m_settingsButton.contains(p)) {
            m_stateManager.pushState(std::make_unique<SettingsState>(m_stateManager, m_font));
        }
    } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
        m_stateManager.pushState(std::make_unique<SettingsState>(m_stateManager, m_font));
    } else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter) {
        m_stateManager.changeState(std::make_unique<DuelState>(m_stateManager, m_font));
    }
}

void MapState::renderPath(sf::RenderTarget& target) const {
    const auto& path = RunState::path();
    const float startX = 210.0f;
    const float gap = 215.0f;
    const float nodeY = 176.0f;

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const bool walked = static_cast<int>(i) < g_run.getEncounterIndex();
        sf::RectangleShape link({ gap - 88.0f, 2.0f });
        link.setPosition(startX + i * gap + 44.0f, nodeY - 1.0f);
        link.setFillColor(walked ? sf::Color(196, 160, 90, 220) : sf::Color(56, 52, 62, 220));
        target.draw(link);
    }

    for (size_t i = 0; i < path.size(); ++i) {
        const bool done = static_cast<int>(i) < g_run.getEncounterIndex();
        const bool current = static_cast<int>(i) == g_run.getEncounterIndex();
        const float x = startX + i * gap;

        // The node you are standing on breathes, so the eye lands on it without
        // having to read five names to find out where you are.
        if (current) {
            const float pulse = 0.5f + 0.5f * std::sin(m_time * 2.3f);
            sf::CircleShape halo(34.0f + 7.0f * pulse);
            halo.setOrigin(halo.getRadius(), halo.getRadius());
            halo.setPosition(x, nodeY);
            halo.setFillColor(sf::Color::Transparent);
            halo.setOutlineThickness(2.0f);
            halo.setOutlineColor(sf::Color(247, 209, 106,
                static_cast<sf::Uint8>(120 - 80 * pulse)));
            target.draw(halo);
        }

        sf::CircleShape node(current ? 30.0f : 22.0f);
        node.setOrigin(node.getRadius(), node.getRadius());
        node.setPosition(x, nodeY);
        node.setFillColor(done ? sf::Color(52, 44, 26, 240)
                              : current ? sf::Color(60, 46, 18, 245) : sf::Color(24, 22, 30, 240));
        node.setOutlineThickness(current ? 3.0f : 1.5f);
        node.setOutlineColor(done ? sf::Color(184, 152, 88)
                                  : current ? sf::Color(247, 209, 106) : sf::Color(74, 68, 80));
        target.draw(node);

        sf::Text number;
        number.setFont(m_font);
        number.setString(done ? "+" : std::to_string(i + 1));
        number.setCharacterSize(current ? 22 : 16);
        number.setStyle(sf::Text::Bold);
        number.setFillColor(done ? sf::Color(210, 186, 130)
                                 : current ? sf::Color(250, 226, 150) : sf::Color(120, 114, 126));
        TextUtils::centerBoth(number);
        number.setPosition(x, nodeY - 1.0f);
        target.draw(number);

        sf::Text name;
        name.setFont(Fonts::ui());
        name.setString(path[i].name);
        name.setCharacterSize(12);
        name.setFillColor(current ? sf::Color(238, 226, 198)
                          : done   ? sf::Color(150, 134, 104)
                                   : sf::Color(110, 105, 116));
        TextUtils::centerBoth(name);
        name.setPosition(x, nodeY + 48.0f);
        target.draw(name);
    }
}

void MapState::renderYourPanel(sf::RenderTarget& target) const {
    const sf::FloatRect panel(96.0f, 288.0f, 480.0f, 316.0f);
    drawPanel(target, panel, sf::Color(18, 16, 23, 238), sf::Color(74, 78, 94, 200));

    drawLabel(target, "YOUR COMMANDER", { panel.left + 24.0f, panel.top + 18.0f }, 12,
              sf::Color(150, 162, 182), 3.0f);

    // The face the player chose on the menu, shown where the run can see it.
    const sf::FloatRect face(panel.left + 24.0f, panel.top + 46.0f, 92.0f, 92.0f);
    CardArt::drawAvatar(target, Avatars::forPlayer(g_run.getPrimaryRole()), face,
                        sf::Color(150, 162, 182));

    const float textX = face.left + face.width + 20.0f;

    sf::Text title;
    title.setFont(m_font);
    title.setString(roleTitle(g_run.getPrimaryRole()));
    title.setCharacterSize(20);
    title.setStyle(sf::Text::Bold);
    title.setFillColor(sf::Color(238, 232, 222));
    title.setPosition(textX, panel.top + 48.0f);
    target.draw(title);

    drawLabel(target,
              std::string(displayName(g_run.getPrimaryRole())) + "  /  "
                  + displayName(g_run.getSecondaryRole()),
              { textX, panel.top + 78.0f }, 14, sf::Color(168, 176, 192), 1.6f);

    const int hp = g_run.getCommanderHp();
    drawGauge(target, { textX, panel.top + 104.0f, 300.0f, 22.0f },
              static_cast<float>(hp) / static_cast<float>(RunState::playerReactorCap()),
              sf::Color(74, 132, 104, 235),
              "REACTOR  " + std::to_string(hp) + " / "
                  + std::to_string(RunState::playerReactorCap()));

    // --- deck composition, as a stacked bar plus chips ------------------------
    drawLabel(target, "DECK", { panel.left + 24.0f, panel.top + 164.0f }, 12,
              sf::Color(150, 162, 182), 3.0f);
    drawLabel(target, std::to_string(g_run.deckSize()) + " CARDS",
              { panel.left + panel.width - 104.0f, panel.top + 164.0f }, 12,
              sf::Color(198, 192, 182), 2.0f);

    const float barY = panel.top + 190.0f;
    const float barW = panel.width - 48.0f;
    const int total = std::max(1, m_units + m_spells + m_traps);

    struct Slice { int count; sf::Color colour; const char* label; };
    const Slice slices[] = {
        { m_units,  sf::Color(120, 158, 210), "UNITS" },
        { m_spells, sf::Color(206, 160, 84),  "SPELLS" },
        { m_traps,  sf::Color(176, 122, 214), "COUNTERS" },
    };

    float x = panel.left + 24.0f;
    for (const Slice& slice : slices) {
        const float w = barW * static_cast<float>(slice.count) / static_cast<float>(total);
        if (w <= 0.5f) continue;
        sf::RectangleShape seg({ w - 2.0f, 14.0f });
        seg.setPosition(x, barY);
        seg.setFillColor(slice.colour);
        target.draw(seg);
        x += w;
    }

    // Chips under the bar name the colours, so the bar is readable without a
    // legend somewhere else on the screen.
    float chipX = panel.left + 24.0f;
    for (const Slice& slice : slices) {
        sf::CircleShape dot(5.0f);
        dot.setPosition(chipX, barY + 26.0f);
        dot.setFillColor(slice.colour);
        target.draw(dot);

        sf::Text label;
        label.setFont(Fonts::ui());
        label.setString(std::to_string(slice.count) + "  " + slice.label);
        label.setCharacterSize(12);
        label.setLetterSpacing(1.4f);
        label.setFillColor(sf::Color(186, 182, 176));
        label.setPosition(chipX + 16.0f, barY + 22.0f);
        target.draw(label);
        chipX += 156.0f;
    }

    drawLabel(target,
              "Scrap one card after every win - the deck you finish with is the one you built.",
              { panel.left + 24.0f, panel.top + 258.0f }, 11, sf::Color(120, 124, 136), 1.0f);
}

void MapState::renderNextPanel(sf::RenderTarget& target) const {
    const Encounter& next = g_run.currentEncounter();
    const sf::FloatRect panel(608.0f, 288.0f, 576.0f, 316.0f);
    drawPanel(target, panel, sf::Color(22, 15, 18, 240), sf::Color(126, 74, 62, 210));

    drawLabel(target,
              "ENCOUNTER " + std::to_string(g_run.getEncounterNumber()) + " OF "
                  + std::to_string(RunState::kEncounters),
              { panel.left + 24.0f, panel.top + 18.0f }, 12, sf::Color(198, 128, 110), 3.0f);

    // The enemy has a face here too. It falls back to the shared portrait, and
    // to nothing at all, without a hole in the layout.
    const sf::FloatRect face(panel.left + 24.0f, panel.top + 46.0f, 92.0f, 92.0f);
    std::string portrait = next.portrait;
    if (!ResourceManager::exists(portrait)) portrait = "assets/portraits/eclipse.png";
    CardArt::drawAvatar(target, portrait, face, sf::Color(196, 96, 84));

    const float textX = face.left + face.width + 20.0f;

    sf::Text name;
    name.setFont(m_font);
    name.setString(next.name);
    name.setCharacterSize(24);
    name.setStyle(sf::Text::Bold);
    name.setFillColor(sf::Color(242, 214, 140));
    name.setPosition(textX, panel.top + 44.0f);
    target.draw(name);

    sf::Text line;
    line.setFont(Fonts::ui());
    line.setString(next.subtitle);
    line.setCharacterSize(13);
    line.setFillColor(sf::Color(172, 164, 158));
    line.setPosition(textX, panel.top + 78.0f);
    target.draw(line);

    drawGauge(target, { textX, panel.top + 104.0f, 300.0f, 22.0f }, 1.0f,
              sf::Color(150, 58, 54, 235),
              "REACTOR  " + std::to_string(RunState::reactorFor(next)));

    // --- what you are walking into, as facts rather than a sentence ----------
    drawLabel(target, "THREAT", { panel.left + 24.0f, panel.top + 164.0f }, 12,
              sf::Color(198, 128, 110), 3.0f);

    struct Fact { std::string key; std::string value; };
    const Fact facts[] = {
        { "DOCTRINES", std::string(displayName(next.primary)) + " / "
                           + displayName(next.secondary) },
        { "FIELDS",    roleTitle(next.primary) },
        { "TITANS",    next.extraTitans > 0
                           ? std::to_string(next.extraTitans + 1) + " on the field"
                           : std::string("none") },
    };

    float y = panel.top + 192.0f;
    for (const Fact& fact : facts) {
        drawLabel(target, fact.key, { panel.left + 24.0f, y }, 11,
                  sf::Color(132, 118, 116), 2.0f);
        sf::Text value;
        value.setFont(Fonts::ui());
        value.setString(fact.value);
        value.setCharacterSize(14);
        value.setFillColor(sf::Color(224, 214, 202));
        value.setPosition(panel.left + 148.0f, y - 3.0f);
        target.draw(value);
        y += 30.0f;
    }
}

void MapState::render(sf::RenderTarget& target) {
    sf::RectangleShape bg({ 1280.0f, 720.0f });
    bg.setFillColor(sf::Color(13, 11, 18));
    target.draw(bg);

    // A faint band behind the path, so the row of nodes reads as a road across
    // the screen rather than five circles floating on a flat rectangle.
    sf::VertexArray road(sf::TriangleStrip, 6);
    const sf::Color edge(26, 22, 34, 0);
    const sf::Color core(30, 26, 40, 230);
    road[0] = sf::Vertex({ 0.0f, 96.0f }, edge);
    road[1] = sf::Vertex({ 1280.0f, 96.0f }, edge);
    road[2] = sf::Vertex({ 0.0f, 176.0f }, core);
    road[3] = sf::Vertex({ 1280.0f, 176.0f }, core);
    road[4] = sf::Vertex({ 0.0f, 256.0f }, edge);
    road[5] = sf::Vertex({ 1280.0f, 256.0f }, edge);
    target.draw(road);

    drawLabel(target, "THE CAMPAIGN", { 96.0f, 46.0f }, 26,
              sf::Color(240, 206, 120), 6.0f, false);

    renderPath(target);
    renderYourPanel(target);
    renderNextPanel(target);

    m_marchButton.render(target);
    m_settingsButton.render(target);
}

// =============================================================================
// DuelState implementation
// =============================================================================

DuelState::DuelState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font), m_vfx({ 640.0f, 360.0f }) {
    m_floating.setFont(font);
    m_combat.setFont(font);
    m_spotlight.setFont(font);
    m_endScreen.setFont(font);
    m_retryButton.setup(font, "RETRY BATTLE", { 500.0f, 620.0f },
                        { 260.0f, 50.0f }, sf::Color(236, 190, 74), 17);
    m_menuButton.setup(font, "ABANDON RUN", { 790.0f, 620.0f },
                       { 260.0f, 50.0f }, sf::Color(150, 142, 134), 17);
    AudioManager::get().playMusicCue(AudioManager::Cue::MusicBattle);

    const Encounter& encounter = g_run.currentEncounter();

    DuelistSetup you;
    you.deck = g_run.buildBattleDeck();
    you.primary = g_run.getPrimaryRole();
    you.secondary = g_run.getSecondaryRole();
    you.hp = g_run.getCommanderHp();
    you.augments = g_run.getAugments();
    you.name = roleTitle(you.primary);

    DuelistSetup foe;
    foe.deck = g_run.buildOpponentDeck();
    foe.primary = encounter.primary;
    foe.secondary = encounter.secondary;
    // Difficulty scales the enemy's reactor rather than the rules: a setting
    // that changed what cards do would have the player learning a different
    // game on each one. The rule itself lives in RunState so the duel, the map
    // preview and the balance simulation cannot drift apart.
    foe.hp = RunState::reactorFor(encounter);
    foe.name = encounter.name;

    m_duel.startDuel(std::move(you), std::move(foe));


    m_endTurnButton.setup(font, "END TURN", { 1122.0f, 320.0f },
                          { 236.0f, 48.0f }, sf::Color(236, 190, 74), 18);

    // Each commander may bring its own portrait; the shared Eclipse face is
    // the fallback so a missing file never shows as a magenta placeholder.
    std::string enemyArt = encounter.portrait;
    if (!ResourceManager::exists(enemyArt)) enemyArt = "assets/portraits/eclipse.png";

    // The player's face is their pick from assets/avatars/, or the one named
    // after their primary core if they never chose.
    const std::string playerArt = Avatars::forPlayer(g_run.getPrimaryRole());
    const sf::Texture& player = ResourceManager::get().getTexture(playerArt);
    m_playerArtPath = playerArt;
    m_enemyArtPath = enemyArt;
    const sf::Texture& enemy = ResourceManager::get().getTexture(enemyArt);
    if (player.getSize().x > 1) {
        m_playerPortrait.setTexture(player, true);
        const float s = 74.0f / static_cast<float>(player.getSize().x);
        m_playerPortrait.setScale(s, s);
        m_hasPortraits = true;
    }
    if (enemy.getSize().x > 1) {
        m_enemyPortrait.setTexture(enemy, true);
        const float s = 74.0f / static_cast<float>(enemy.getSize().x);
        m_enemyPortrait.setScale(s, s);
    }

    showBanner("YOUR TURN");
    consumeEvents();
}

// ---- geometry helpers -------------------------------------------------------

sf::Vector2f DuelState::handCardCentre(int index, int count) const {
    if (count <= 0) return { 640.0f, 620.0f };
    // Cards overlap into a shallow arc, the way a held hand actually sits.
    const float spacing = std::min(108.0f, 760.0f / static_cast<float>(std::max(1, count)));
    const float offset = static_cast<float>(index) - (count - 1) / 2.0f;
    // The arc lifts the outer cards rather than dropping them: a downward arc
    // put the outermost card's bottom edge off a 720px screen. The centre card
    // is the lowest, at 640 + half of a 146px card = 713, which leaves the stat
    // badges clear of the screen edge and 25px of board between the hand and
    // the reactor card above it.
    float y = 640.0f - std::abs(offset) * 5.0f;
    if (index == m_hoverCardIndex) y -= kHoverLift;
    return { 640.0f + offset * spacing, y };
}

float DuelState::handCardTilt(int index, int count) const {
    // A held hand fans; a hand laid out flat is a row of tiles. The outermost
    // card leans four degrees, and everything between is proportional, so the
    // fan opens with the hand rather than snapping wider at some threshold.
    //
    // The hovered card is straightened, because it is being read rather than
    // held - a tilted card at 1.35x is harder to read than an upright one.
    if (count <= 1 || index == m_hoverCardIndex) return 0.0f;
    const float offset = static_cast<float>(index) - (count - 1) / 2.0f;
    const float extreme = (count - 1) / 2.0f;
    return (offset / extreme) * 4.0f;
}

int DuelState::handCardAt(sf::Vector2f point) const {
    const auto& hand = m_duel.commander(Side::Player).getHand();
    const int count = static_cast<int>(hand.size());
    const sf::Vector2f size = handCardSize();
    // Later cards are drawn on top, so hit-test from the right.
    for (int i = count - 1; i >= 0; --i) {
        const sf::Vector2f c = handCardCentre(i, count);
        sf::FloatRect box(c.x - size.x / 2.0f, c.y - size.y / 2.0f, size.x, size.y);
        if (box.contains(point)) return i;
    }
    return -1;
}

sf::FloatRect DuelState::rectOf(const Unit& unit) const {
    const UnitLocation where = m_duel.board().locate(unit.instanceId);
    if (!where.valid()) return {};

    // Fixed cells: the slot a frame occupies is its lane, and the lane is now
    // load-bearing for both reach and Guard.
    return Layout::unitRect(where.side, where.line, where.slot);
}

Unit* DuelState::unitAt(sf::Vector2f point) {
    for (Side side : { Side::Player, Side::Opponent }) {
        for (BoardLine line : { BoardLine::Frontline, BoardLine::Support }) {
            for (int slot = 0; slot < Board::kLineSlots; ++slot) {
                Unit* unit = m_duel.board().at(side, line, slot);
                if (unit && Layout::unitRect(side, line, slot).contains(point)) return unit;
            }
        }
    }
    return nullptr;
}

bool DuelState::rowAt(sf::Vector2f point, Side& side, BoardLine& line) const {
    for (Side s : { Side::Player, Side::Opponent }) {
        for (BoardLine l : { BoardLine::Frontline, BoardLine::Support }) {
            if (Layout::rowBand(s, l).contains(point)) { side = s; line = l; return true; }
        }
    }
    return false;
}

bool DuelState::trapZoneHit(Side side, sf::Vector2f point) const {
    return Layout::trapZone(side).contains(point);
}

const TrapCard* DuelState::ownTrapAt(sf::Vector2f point) const {
    const auto& traps = m_duel.board().traps(Side::Player);
    for (int i = 0; i < static_cast<int>(traps.size()); ++i) {
        if (Layout::trapSlot(Side::Player, i).contains(point)) {
            return &traps[static_cast<size_t>(i)];
        }
    }
    return nullptr;
}

bool DuelState::commanderHit(Side side, sf::Vector2f point) const {
    // The HQ card is the commander's body on the board; the side panel is a
    // readout, not a target.
    return Layout::hqCard(side).contains(point);
}

void DuelState::openInspector(const Unit& unit) {
    m_stateManager.pushState(std::make_unique<CardInspectState>(
        m_stateManager, m_font, unit.data, CardArt::statusesFor(unit)));
}

void DuelState::openInspector(const CardData& card) {
    m_stateManager.pushState(std::make_unique<CardInspectState>(m_stateManager, m_font, card));
}

bool DuelState::canPlayCard(const CardData& card) const {
    const Commander& me = m_duel.commander(Side::Player);
    if (card.category == CardCategory::Trap) {
        return me.canAfford(DuelEngine::kSetTrapCost) && !m_duel.board().trapZoneFull(Side::Player);
    }
    if (card.category == CardCategory::Spell) return me.canAfford(card.manaCost);
    // A unit is playable if any tribute count brings it within reach.
    const int owned = m_duel.board().unitCount(Side::Player);
    for (int t = 0; t <= std::min(card.tributeRequirement(), owned); ++t) {
        if (me.canAfford(m_duel.summonCost(card, t))) return true;
    }
    return false;
}

// ---- input ------------------------------------------------------------------

void DuelState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    // Once the duel is decided the board stops taking input - the only live
    // controls are the two buttons phase 4 slides in, and they are not
    // clickable until they are visible.
    if (m_duel.isOver()) {
        if (m_endScreen.outcome() != EndScreen::Outcome::Defeat) return;
        if (m_endScreen.uiReveal() < 0.9f) return;

        if (event.type == sf::Event::MouseMoved) {
            const sf::Vector2f p =
                window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
            m_retryButton.setHovered(m_retryButton.contains(p));
            m_menuButton.setHovered(m_menuButton.contains(p));
        } else if (event.type == sf::Event::MouseButtonPressed &&
                   event.mouseButton.button == sf::Mouse::Left) {
            const sf::Vector2f p =
                window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
            if (m_retryButton.contains(p)) {
                // The run's reactor is untouched until finishDuel() writes it
                // back, so replaying the encounter simply rebuilds the duel.
                m_stateManager.changeState(
                    std::make_unique<DuelState>(m_stateManager, m_font));
            } else if (m_menuButton.contains(p)) {
                m_stateManager.changeState(
                    std::make_unique<MenuState>(m_stateManager, m_font));
            }
        }
        return;
    }

    if (event.type == sf::Event::MouseMoved) {
        m_mousePos = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        if (m_mode == Interaction::Idle) {
            const int over = handCardAt(m_mousePos);
            if (over != m_hoverCandidate) {
                m_hoverCandidate = over;
                m_hoverDwell = 0.0f;
                m_hoverCardIndex = -1;   // drop the old one at once
            }
            const Unit* hovered = unitAt(m_mousePos);
            m_hoverUnitId = hovered ? hovered->instanceId : -1;
        } else {
            const sf::Vector2f delta = m_mousePos - m_pressPos;
            if (std::abs(delta.x) + std::abs(delta.y) > kDragThreshold) m_dragMoved = true;
        }
        m_endTurnButton.setHovered(m_endTurnButton.contains(m_mousePos));
        return;
    }

    // Settings must open even while the enemy is moving, so it is handled
    // before the "not your turn" guard below.
    if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape &&
        m_mode != Interaction::SelectingTributes) {
        m_stateManager.pushState(std::make_unique<SettingsState>(m_stateManager, m_font));
        return;
    }

    if (m_duel.isOver() || !isPlayerTurn()) return;

    if (event.type == sf::Event::MouseButtonPressed) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
        m_mousePos = p;

        if (event.mouseButton.button == sf::Mouse::Right) {
            if (m_mode == Interaction::SelectingTributes) { cancelPending(); return; }
            const int card = handCardAt(p);
            if (card >= 0) {
                const auto& hand = m_duel.commander(Side::Player).getHand();
                openInspector(hand[static_cast<size_t>(card)]);
            } else if (const TrapCard* trap = ownTrapAt(p)) {
                openInspector(trap->data);
            } else if (const Unit* unit = unitAt(p)) {
                openInspector(*unit);
            }
            return;
        }
        if (event.mouseButton.button != sf::Mouse::Left) return;

        if (m_mode == Interaction::SelectingTributes) { handleTributeClick(p); return; }
        if (Settings::get().showBattleLog && Layout::logButton().contains(p)) {
            m_logOpen = !m_logOpen;
            return;
        }
        if (m_endTurnButton.contains(p)) { m_duel.endTurn(); consumeEvents(); return; }
        // A set counter cannot be picked up or retargeted, so a plain left click
        // on one has nothing else to mean: open it. Right click still works too,
        // which is the gesture every other card on the board answers to.
        if (const TrapCard* trap = ownTrapAt(p)) { openInspector(trap->data); return; }
        beginDrag(p);
        return;
    }

    if (event.type == sf::Event::MouseButtonReleased) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
        if (event.mouseButton.button == sf::Mouse::Left) releaseDrag(p);
        return;
    }

    if (event.type == sf::Event::KeyPressed) {
        if (event.key.code == sf::Keyboard::Space) { m_duel.endTurn(); consumeEvents(); }
        else if (event.key.code == sf::Keyboard::Escape) {
            // Escape backs out of a tribute first; otherwise it opens settings
            // over the duel, which stays exactly as it was underneath.
            if (m_mode == Interaction::SelectingTributes) cancelPending();
            else m_stateManager.pushState(std::make_unique<SettingsState>(m_stateManager, m_font));
        }
    }
}

void DuelState::beginDrag(sf::Vector2f point) {
    m_pressPos = point;
    m_dragMoved = false;

    const int card = handCardAt(point);
    if (card >= 0) {
        m_mode = Interaction::DraggingCard;
        m_dragCardIndex = card;
        return;
    }

    Unit* unit = unitAt(point);
    if (unit) {
        const UnitLocation where = m_duel.board().locate(unit->instanceId);
        if (where.valid() && where.side == Side::Player) {
            m_mode = Interaction::DraggingUnit;
            m_dragUnitId = unit->instanceId;
        } else if (unit) {
            // An enemy unit cannot be dragged, but it can be read.
            m_mode = Interaction::Idle;
            m_dragUnitId = unit->instanceId;
        }
    }
}

void DuelState::releaseDrag(sf::Vector2f point) {
    const Interaction mode = m_mode;
    const int cardIndex = m_dragCardIndex;
    const int unitId = m_dragUnitId;
    const bool wasClick = !m_dragMoved;
    m_mode = Interaction::Idle;
    m_dragCardIndex = -1;
    m_dragUnitId = -1;
    m_dragMoved = false;

    // A press that never travelled is a request to read the card, not to play it.
    if (wasClick) {
        if (cardIndex >= 0) {
            const auto& hand = m_duel.commander(Side::Player).getHand();
            if (cardIndex < static_cast<int>(hand.size())) {
                openInspector(hand[static_cast<size_t>(cardIndex)]);
            }
            return;
        }
        if (unitId >= 0) {
            if (const Unit* unit = m_duel.board().findById(unitId)) {
                openInspector(*unit);
            }
            return;
        }
        return;
    }

    if (mode == Interaction::DraggingCard) {
        auto& hand = m_duel.commander(Side::Player).getHand();
        if (cardIndex < 0 || cardIndex >= static_cast<int>(hand.size())) return;
        const CardData card = hand[static_cast<size_t>(cardIndex)];

        if (card.category == CardCategory::Trap) {
            if (!trapZoneHit(Side::Player, point)) return;
            const ActionResult result = m_duel.setTrap(Side::Player, static_cast<size_t>(cardIndex));
            if (result != ActionResult::Ok) showBanner(toString(result));
            consumeEvents();
            return;
        }

        if (card.category == CardCategory::Spell) {
            int targetId = -1;
            if (Unit* target = unitAt(point)) targetId = target->instanceId;
            const ActionResult result =
                m_duel.castSpell(Side::Player, static_cast<size_t>(cardIndex), targetId);
            if (result != ActionResult::Ok) showBanner(toString(result));
            consumeEvents();
            return;
        }

        // A unit only needs the right row; the engine picks the free cell.
        Side side; BoardLine line;
        if (!rowAt(point, side, line) || side != Side::Player) return;

        ActionResult result = m_duel.summonFromHand(Side::Player, static_cast<size_t>(cardIndex),
                                                    line, -1);
        if (result == ActionResult::Ok) { consumeEvents(); return; }

        if (result == ActionResult::NotEnoughMana && card.tributeRequirement() > 0) {
            // Fall back to the tribute ritual: pick bodies to offer.
            m_pendingCard = card;
            m_pendingHandIndex = cardIndex;
            m_pendingLine = line;
            m_pendingSlot = -1;
            m_tributes.clear();
            m_pendingNeeded = 0;
            const Commander& me = m_duel.commander(Side::Player);
            for (int t = 1; t <= card.tributeRequirement(); ++t) {
                if (me.canAfford(m_duel.summonCost(card, t))) { m_pendingNeeded = t; break; }
            }
            if (m_pendingNeeded == 0) { showBanner("Not enough mana, even with tributes"); return; }
            if (m_duel.board().unitCount(Side::Player) < m_pendingNeeded) {
                showBanner("Not enough units to tribute");
                return;
            }
            m_mode = Interaction::SelectingTributes;
            showBanner("Choose " + std::to_string(m_pendingNeeded) + " unit(s) to tribute");
            return;
        }
        showBanner(toString(result));
        return;
    }

    if (mode == Interaction::DraggingUnit) {
        Unit* unit = m_duel.board().findById(unitId);
        if (!unit) return;

        // Dropping on your own empty frontline advances the unit.
        Side side; BoardLine line;
        if (rowAt(point, side, line) && side == Side::Player &&
            line == BoardLine::Frontline && !unitAt(point)) {
            const ActionResult result = m_duel.advanceUnit(Side::Player, unitId);
            if (result != ActionResult::Ok) showBanner(toString(result));
            consumeEvents();
            return;
        }

        int targetId = -2;
        if (Unit* target = unitAt(point)) targetId = target->instanceId;
        else if (commanderHit(Side::Opponent, point)) targetId = -1;
        if (targetId == -2) return;

        const ActionResult result = m_duel.declareAttack(Side::Player, unitId, targetId);
        if (result != ActionResult::Ok) showBanner(toString(result));
        consumeEvents();
    }
}

void DuelState::handleTributeClick(sf::Vector2f point) {
    Unit* unit = unitAt(point);
    if (!unit) return;
    const UnitLocation where = m_duel.board().locate(unit->instanceId);
    if (!where.valid() || where.side != Side::Player) return;

    auto it = std::find(m_tributes.begin(), m_tributes.end(), unit->instanceId);
    if (it != m_tributes.end()) m_tributes.erase(it);
    else if (static_cast<int>(m_tributes.size()) < m_pendingNeeded) {
        m_tributes.push_back(unit->instanceId);
    }

    if (static_cast<int>(m_tributes.size()) == m_pendingNeeded) tryPendingSummon();
}

void DuelState::tryPendingSummon() {
    const ActionResult result =
        m_duel.summonFromHand(Side::Player, static_cast<size_t>(m_pendingHandIndex),
                              m_pendingLine, m_pendingSlot, m_tributes);
    if (result != ActionResult::Ok) showBanner(toString(result));
    cancelPending();
    consumeEvents();
}

void DuelState::cancelPending() {
    m_mode = Interaction::Idle;
    m_pendingHandIndex = -1;
    m_pendingNeeded = 0;
    m_tributes.clear();
}

// ---- update -----------------------------------------------------------------

void DuelState::pushLog(const std::string& line) {
    if (line.empty()) return;
    m_log.push_back(line);
    // Deep enough to be a history now that it lives in a panel of its own,
    // rather than the five-line column that used to run down the board edge.
    while (m_log.size() > 18) m_log.pop_front();
}

void DuelState::refreshRectCache() {
    for (Side side : { Side::Player, Side::Opponent }) {
        const auto& traps = m_duel.board().traps(side);
        for (size_t i = 0; i < traps.size(); ++i) {
            m_lastTrapRect[traps[i].instanceId] =
                Layout::trapSlot(side, static_cast<int>(i));
        }
    }

    for (Side side : { Side::Player, Side::Opponent }) {
        for (BoardLine line : { BoardLine::Frontline, BoardLine::Support }) {
            for (int slot = 0; slot < Board::kLineSlots; ++slot) {
                if (const Unit* unit = m_duel.board().at(side, line, slot)) {
                    m_lastRect[unit->instanceId] = Layout::unitRect(side, line, slot);
                }
            }
        }
    }
}

sf::Vector2f DuelState::centreOf(int unitId) const {
    auto it = m_lastRect.find(unitId);
    if (it == m_lastRect.end()) return { 640.0f, 360.0f };
    return { it->second.left + it->second.width / 2.0f,
             it->second.top + it->second.height / 2.0f };
}

sf::Vector2f DuelState::scrapCentre(Side side) const {
    const sf::FloatRect pile = Layout::scrapPile(side);
    return { pile.left + pile.width / 2.0f, pile.top + pile.height / 2.0f };
}

sf::Color DuelState::accentOf(int unitId) const {
    if (const Unit* unit = m_duel.board().findById(unitId)) {
        return CardArt::accentFor(unit->data);
    }
    return sf::Color(210, 190, 160);
}

void DuelState::updateLiveArrow() {
    // The attack arrow follows the cursor while a frame is being dragged onto a
    // target, and turns dull red over anything it cannot legally hit.
    if (m_mode != Interaction::DraggingUnit || m_dragUnitId < 0) {
        m_combat.setLiveArrow(false);
        return;
    }

    bool valid = false;
    const auto legal = m_duel.legalTargets(m_dragUnitId);
    if (const Unit* hovered = unitAt(m_mousePos)) {
        valid = std::find(legal.begin(), legal.end(), hovered->instanceId) != legal.end();
    } else if (commanderHit(Side::Opponent, m_mousePos)) {
        valid = std::find(legal.begin(), legal.end(), -1) != legal.end();
    }

    m_combat.setLiveArrow(true, centreOf(m_dragUnitId), m_mousePos,
                          valid ? sf::Color(236, 190, 74) : sf::Color(150, 96, 96), valid);
}

/**
 * Turn this batch's CardDrawn events into flights.
 *
 * The whole batch is needed at once, not one event at a time: by the time the
 * events are read the engine has already appended every drawn card, so the only
 * way to know which hand slot a given draw belongs to is to count the batch and
 * work back from the end of the hand.
 */
void DuelState::launchDraws(const std::vector<DuelEvent>& batch) {
    for (Side side : { Side::Player, Side::Opponent }) {
        std::vector<const DuelEvent*> drawn;
        for (const DuelEvent& event : batch) {
            if (event.type == DuelEvent::Type::CardDrawn && event.side == side) {
                drawn.push_back(&event);
            }
        }
        if (drawn.empty()) continue;

        const int held = static_cast<int>(m_duel.commander(side).getHand().size());
        const int first = held - static_cast<int>(drawn.size());
        if (first < 0) continue;   // something else ate the hand; skip the flourish

        const sf::FloatRect pile = Layout::deckPile(side);
        const sf::Vector2f from(pile.left + pile.width / 2.0f,
                                pile.top + pile.height / 2.0f);

        for (int i = 0; i < static_cast<int>(drawn.size()); ++i) {
            const CardData* card = DataLoader::findCard(drawn[static_cast<size_t>(i)]->cardId);
            if (!card) continue;
            const int slot = first + i;

            sf::Vector2f to;
            sf::Vector2f size;
            if (side == Side::Player) {
                to = handCardCentre(slot, held);
                size = handCardSize();
            } else {
                const sf::FloatRect box = enemyHandCardBox(slot, held);
                to = { box.left + box.width / 2.0f, box.top + box.height / 2.0f };
                size = { box.width, box.height };
            }
            // Staggered, because an opening hand of five leaving the pile on
            // the same frame arrives as one shape rather than as five cards.
            m_drawFlight.launch(*card, side, slot, from, to, size,
                                static_cast<float>(i) * 0.11f);
        }
    }
}

void DuelState::consumeEvents() {
    const std::vector<DuelEvent> batch = m_duel.drainEvents();
    launchDraws(batch);
    for (const DuelEvent& event : batch) {
        pushLog(event.text);

        switch (event.type) {
        case DuelEvent::Type::UnitDamaged: {
            const sf::Vector2f at = centreOf(event.instanceId);
            m_floating.spawnDamage(event.amount, { at.x, at.y - 26.0f });

            // Wash the frame red, and throw debris away from whatever hit it.
            m_combat.flash(event.instanceId, sf::Color(236, 84, 74, 190), 0.30f);
            const sf::Vector2f from = m_lastAttackerId >= 0 ? centreOf(m_lastAttackerId)
                                                            : sf::Vector2f(at.x, at.y - 60.0f);
            m_combat.impact(at, at - from, accentOf(event.instanceId));
            if (event.amount >= 5 && Settings::get().screenShake) {
                m_vfx.triggerScreenShake(0.16f, 5.0f);
            }
            break;
        }
        case DuelEvent::Type::UnitHealed: {
            const sf::Vector2f at = centreOf(event.instanceId);
            m_floating.spawnHeal(event.amount, { at.x, at.y - 26.0f });
            m_combat.flash(event.instanceId, sf::Color(120, 226, 170, 150), 0.36f);
            m_combat.shockwave(at, sf::Color(150, 220, 195), 46.0f, 0.5f);
            break;
        }

        case DuelEvent::Type::UnitDestroyed: {
            // The frame is already off the board, so this draws from the cache.
            const sf::Vector2f at = centreOf(event.instanceId);
            m_combat.explosion(at, sf::Color(210, 150, 90));
            m_floating.spawnBuff("SCRAPPED", { at.x, at.y - 10.0f });
            AudioManager::get().playCue(AudioManager::Cue::Death,
                                        DataLoader::findCard(event.cardId), 0.95f);
            if (Settings::get().screenShake) m_vfx.triggerScreenShake(0.26f, 9.0f);
            m_lastRect.erase(event.instanceId);
            break;
        }
        case DuelEvent::Type::CommanderDamaged: {
            const sf::FloatRect hq = Layout::hqCard(event.side);
            const sf::Vector2f at(hq.left + hq.width / 2.0f, hq.top + hq.height / 2.0f);
            m_floating.spawnPlayerHit(event.amount,
                                      { hq.left + hq.width / 2.0f, hq.top + 12.0f });
            m_combat.impact(at, { 0.0f, event.side == Side::Player ? 1.0f : -1.0f },
                            sf::Color(236, 96, 84));
            m_combat.shockwave(at, sf::Color(236, 96, 84), 90.0f, 0.58f);
            if (event.amount >= 6 && Settings::get().screenShake) {
                m_vfx.triggerScreenShake(0.22f, 8.0f);
            }
            break;
        }
        case DuelEvent::Type::CommanderHealed: {
            const sf::FloatRect hq = Layout::hqCard(event.side);
            m_floating.spawnHeal(event.amount,
                                 { hq.left + hq.width / 2.0f, hq.top + 12.0f });
            break;
        }
        case DuelEvent::Type::TrapFlipped: {
            AudioManager::get().playCue(AudioManager::Cue::TrapFlip);
            showBanner("COUNTER-PROTOCOL");

            const sf::FloatRect zone = Layout::trapZone(event.side);
            const sf::Vector2f origin(zone.left + zone.width / 2.0f, zone.top + zone.height / 2.0f);
            m_floating.spawnBuff("COUNTER", { origin.x, origin.y - 20.0f });

            const sf::Color violet(175, 120, 225);

            // A flipped counter is always news - both sides see the card, since
            // it is face-up the moment it resolves.
            if (const CardData* card = DataLoader::findCard(event.cardId)) {
                auto slot = m_lastTrapRect.find(event.instanceId);
                if (slot != m_lastTrapRect.end()) {
                    m_combat.trapFlip(slot->second, *card, event.side, violet);
                    m_lastTrapRect.erase(slot);
                }
                // Held until the card has finished turning; showing both at
                // once would put the same card on screen twice.
                m_queuedCard = *card;
                m_queuedSide = event.side;
                m_queuedKind = CardSpotlight::Kind::Counter;
                m_queuedDelay = 0.42f;
            }

            m_combat.shockwave(origin, violet, 150.0f, 0.72f);
            m_combat.sparks(origin, violet);

            // Trace it to whatever set it off, so the cause is visible.
            if (event.targetId >= 0) {
                const sf::Vector2f victim = centreOf(event.targetId);
                m_combat.beam(origin, victim, violet, 0.5f);
                m_combat.flash(event.targetId, sf::Color(175, 120, 225, 200), 0.44f);
                m_combat.sparks(victim, violet);
            }
            if (Settings::get().screenShake) m_vfx.triggerScreenShake(0.2f, 7.0f);
            break;
        }

        case DuelEvent::Type::TrapSet: {
            AudioManager::get().playCue(AudioManager::Cue::TrapSet,
                                        DataLoader::findCard(event.cardId), 0.9f);

            // The flare goes on the slot the card actually landed in, so it
            // reads as that card arming rather than as the zone glowing.
            const auto& traps = m_duel.board().traps(event.side);
            sf::FloatRect slot = Layout::trapSlot(event.side,
                                                  std::max(0, static_cast<int>(traps.size()) - 1));
            for (int i = 0; i < static_cast<int>(traps.size()); ++i) {
                if (traps[static_cast<size_t>(i)].instanceId == event.instanceId) {
                    slot = Layout::trapSlot(event.side, i);
                    break;
                }
            }
            m_combat.armFlare(slot, CardArt::armedBorder());
            break;
        }

        case DuelEvent::Type::SpellCast: {
            // A card's own sound first, then the doctrine's, then the shared
            // spell sting - so `pl_smite.wav` would beat `Paladin_Deploy.mp3`,
            // which beats `Spell_Activate.mp3`.
            AudioManager::get().playCue(AudioManager::Cue::Spell,
                                        DataLoader::findCard(event.cardId), 1.0f);
            m_lastAttackerId = -1;   // ordnance has no attacker to spray from
            if (const CardData* card = DataLoader::findCard(event.cardId)) {
                // Operations resolve out of sight - the card is in the scrap
                // yard before its effect is visible - so both sides get the
                // spotlight, not just the enemy's.
                m_spotlight.show(*card, event.side, CardSpotlight::Kind::Operation,
                                 scrapCentre(event.side));
                const sf::Color accent = CardArt::accentFor(*card);
                const sf::FloatRect hq = Layout::hqCard(event.side);
                const sf::Vector2f from(hq.left + hq.width / 2.0f, hq.top + hq.height / 2.0f);
                const sf::FloatRect band = Layout::rowBand(other(event.side), BoardLine::Frontline);
                const sf::Vector2f to(band.left + band.width / 2.0f, band.top + band.height / 2.0f);

                m_combat.shockwave(from, accent, 120.0f, 0.6f);
                if (card->spell == SpellKind::DamageEnemyFrontline ||
                    card->spell == SpellKind::WipeLowHealthUnits ||
                    card->spell == SpellKind::DestroyHighAttackEnemy) {
                    m_combat.beam(from, to, accent, 0.52f);
                    m_combat.shockwave(to, accent, 170.0f, 0.72f);
                    if (Settings::get().screenShake) m_vfx.triggerScreenShake(0.2f, 6.0f);
                }
            }
            break;
        }
        case DuelEvent::Type::AttackDeclared: {
            if (const Unit* swinging = m_duel.board().findById(event.instanceId)) {
                AudioManager::get().playCue(AudioManager::Cue::Attack, &swinging->data);
            }
            m_lastAttackerId = event.instanceId;

            const sf::Vector2f from = centreOf(event.instanceId);
            sf::Vector2f to;
            if (event.targetId >= 0) {
                to = centreOf(event.targetId);
            } else {
                const sf::FloatRect hq = Layout::hqCard(other(event.side));
                to = { hq.left + hq.width / 2.0f, hq.top + hq.height / 2.0f };
            }

            // The arrow is replayed for both sides. The player already saw one
            // while dragging, but the enemy's choice of target is otherwise
            // invisible - a frame simply loses health with no stated cause.
            m_combat.arrow(from, to,
                           event.side == Side::Player ? sf::Color(236, 190, 74)
                                                      : sf::Color(226, 120, 110),
                           0.9f);
            m_combat.lunge(event.instanceId, to - from, 26.0f);
            if (event.targetId >= 0) m_combat.crosshair(to, 0.55f);

            // Ranged frames fire rather than close, so they get a beam instead
            // of a lunge distance that would carry them across the board.
            if (const Unit* attacker = m_duel.board().findById(event.instanceId)) {
                if (attacker->hasKeyword(Keyword::Ranged)) {
                    m_combat.lunge(event.instanceId, from - to, 9.0f);   // recoil
                    m_combat.beam(from, to, accentOf(event.instanceId), 0.44f);
                }
            }
            break;
        }
        case DuelEvent::Type::UnitSummoned: {
            if (const CardData* card = DataLoader::findCard(event.cardId)) {
                // A titan landing is its own event: the doctrines that have a
                // titan sample want it heard instead of the ordinary deploy.
                AudioManager::get().playCue(card->tier == CardTier::Tier3
                                                ? AudioManager::Cue::TitanDeploy
                                                : AudioManager::Cue::Deploy,
                                            card);
            }
            refreshRectCache();
            const sf::Vector2f at = centreOf(event.instanceId);
            // The beats every frame shares: it hits, it squashes, it flashes.
            m_combat.flash(event.instanceId, sf::Color(255, 255, 255, 140), 0.4f);
            m_combat.slam(event.instanceId);
            m_combat.dropMarker(at, accentOf(event.instanceId));
            // Then whatever this doctrine does that no other one does. The
            // shared shockwave and dust moved in there: a Valkyrie settling and
            // a Siege fortress arriving should not throw the same cloud.
            if (const CardData* landed = DataLoader::findCard(event.cardId)) {
                // The recipe asks for a shake; whether one happens is this
                // layer's call, because the player can switch them off.
                const DeploySignature::Shake shake =
                    DeploySignature::play(m_combat, *landed, at, event.instanceId);
                if (shake.wanted() && Settings::get().screenShake) {
                    m_vfx.triggerScreenShake(shake.seconds, shake.amplitude);
                }
            } else {
                m_combat.shockwave(at, accentOf(event.instanceId), 96.0f, 0.62f);
                m_combat.smoke(at, 14);
            }

            if (const Unit* landed = m_duel.board().findById(event.instanceId)) {
                if (landed->data.tier == CardTier::Tier3) {
                    m_spotlight.show(landed->data, event.side,
                                     CardSpotlight::Kind::Titan, scrapCentre(event.side));
                    if (Settings::get().screenShake) m_vfx.triggerScreenShake(0.2f, 8.0f);
                }
            }

            break;
        }
        case DuelEvent::Type::TurnStarted:
            showBanner(event.side == Side::Player ? "YOUR TURN" : "ENEMY TURN");
            m_lastAttackerId = -1;
            break;
        default:
            break;
        }
    }
    // The next batch of events will read positions from this cache, so it has
    // to describe the board as it stands now, not as it stood last frame.
    refreshRectCache();
}

void DuelState::update(float dt) {
    m_uiClock += dt;
    m_floating.update(dt);
    m_vfx.update(dt);
    m_combat.update(dt);
    m_spotlight.update(dt);
    m_drawFlight.update(dt);
    if (m_drawFlight.consumeFlip()) {
        // A dedicated stem first, then whatever whoosh the author has; if
        // neither is there the animation simply plays silent.
        AudioManager::get().playNamed({ "card_flip", "card_draw", "whoosh", "draw" }, 1.0f);
    }
    if (m_queuedDelay > 0.0f) {
        m_queuedDelay -= dt;
        if (m_queuedDelay <= 0.0f) {
            m_spotlight.show(m_queuedCard, m_queuedSide, m_queuedKind,
                             scrapCentre(m_queuedSide));
        }
    }
    refreshRectCache();

    // Promote a resting cursor into an actual hover.
    if (m_hoverCandidate >= 0 && m_mode == Interaction::Idle) {
        m_hoverDwell += dt;
        if (m_hoverDwell >= kHoverDelay) m_hoverCardIndex = m_hoverCandidate;
    } else {
        m_hoverDwell = 0.0f;
        m_hoverCardIndex = -1;
    }
    updateLiveArrow();
    if (m_bannerTimer > 0.0f) m_bannerTimer -= dt;

    if (m_duel.isOver()) {
        m_resultTimer += dt;
        updateEndSequence(dt);
        return;
    }

    m_endTurnButton.enabled = isPlayerTurn();

    // The AI acts one step at a time so the player can follow what happened.
    if (m_duel.activeSide() == Side::Opponent) {
        // The pacing pause runs *through* a reveal rather than after it - the
        // reveal is already the beat. What the reveal blocks is the next
        // action, so the AI cannot play over its own announcement.
        m_enemyTimer -= dt;
        if (m_enemyTimer <= 0.0f && !m_spotlight.busy()
            && m_queuedDelay <= 0.0f && !m_combat.flipping()) {
            if (!m_ai.step(m_duel)) {
                m_duel.endTurn();
            }
            consumeEvents();
            // The pause between enemy actions is a player-facing pacing option.
            // 1.05s, not 0.62: at the old rate the enemy played three cards
            // before the first deploy ring had finished expanding, and a turn
            // was over before it could be read.
            m_enemyTimer = 1.05f / std::max(0.5f, Settings::get().enemyTurnSpeed);
        }
    }
}

void DuelState::beginEndSequence() {
    // A beat of held time before anything moves. The reactor hitting zero is
    // the loudest thing that happens in a duel and it used to pass in a single
    // frame; stopping the clock for a fifth of a second is what makes it land.
    m_hitStop = 0.20f;
    const bool won = m_duel.winner() == Side::Player;
    const Side loser = won ? Side::Opponent : Side::Player;
    const sf::FloatRect hq = Layout::hqCard(loser);
    const sf::Vector2f at(hq.left + hq.width / 2.0f, hq.top + hq.height / 2.0f);

    if (won) {
        m_endScreen.begin(EndScreen::Outcome::Victory, "CORE BREACHED",
                          m_duel.commander(Side::Opponent).getName() + " IS OFFLINE");
    } else {
        m_endScreen.begin(EndScreen::Outcome::Defeat, "SYSTEM CRITICAL",
                          "COMMANDER MECH DESTROYED");
    }

    // The portrait that comes apart is the one that lost: your own commander on
    // a defeat, theirs on a win.
    std::string stem;
    if (won) {
        stem = g_run.currentEncounter().portrait;
        if (!ResourceManager::exists(stem)) stem = "assets/portraits/eclipse.png";
    } else {
        stem = Avatars::forPlayer(g_run.getPrimaryRole());
    }
    if (m_portraitRig.load(stem)) {
        // Always Shatter. The portrait on this screen belongs to whoever just
        // lost their reactor, so it is the thing coming apart - Ascend eased the
        // pieces out and then settled them back together at full opacity, which
        // on a win meant the enemy you had just destroyed visibly healed.
        m_portraitRig.begin({ 640.0f, 268.0f }, 260.0f);
    }

    m_endVfx.begin(won ? EndGameVFX::Outcome::Victory : EndGameVFX::Outcome::Defeat, at);

    // Phase 1 opens on the reactor actually coming apart.
    m_combat.explosion(at, won ? sf::Color(240, 196, 92) : sf::Color(238, 92, 70));
    // The reactor that failed belongs to a doctrine, so it dies in that
    // doctrine's voice rather than a generic bang.
    AudioManager::get().playRoleCue(
        AudioManager::Cue::Death,
        won ? g_run.currentEncounter().primary : g_run.getPrimaryRole(), 0.9f);
    m_sparkClock = 0.0f;
}

void DuelState::updateEndSequence(float dt) {
    if (!m_endScreen.active()) beginEndSequence();

    // Time dilation, not a freeze: at a flat stop the sparks already in the air
    // hang motionless and the pause reads as a stutter. Running everything at a
    // sixth of speed keeps them crawling, which is what sells the held beat.
    if (m_hitStop > 0.0f) {
        m_hitStop -= dt;
        dt *= 0.16f;
    }

    m_endScreen.update(dt);
    m_endVfx.update(dt);
    m_portraitRig.update(dt);

    const bool won = m_duel.winner() == Side::Player;
    const Side loser = won ? Side::Opponent : Side::Player;
    const sf::FloatRect hq = Layout::hqCard(loser);
    const sf::Vector2f at(hq.left + hq.width / 2.0f, hq.top + hq.height / 2.0f);

    // Phase 1: keep throwing sparks off the failing reactor for half a second.
    if (m_endScreen.sparking()) {
        m_sparkClock -= dt;
        if (m_sparkClock <= 0.0f) {
            m_sparkClock = 0.055f;
            const sf::Vector2f jitter(Rng::rangeF(-52.0f, 52.0f), Rng::rangeF(-26.0f, 26.0f));
            m_combat.sparks(at + jitter, won ? sf::Color(250, 214, 120)
                                             : sf::Color(120, 190, 255));
        }
    }

    // Phase 3: the verdict lands. One shockwave out of the wreck, and the
    // camera punch is already coming from EndScreen.
    if (m_endScreen.consumeImpact()) {
        m_combat.shockwave({ 640.0f, 334.0f },
                           won ? sf::Color(240, 196, 92) : sf::Color(238, 62, 58),
                           520.0f, 0.75f);
        AudioManager::get().stopMusic();
        AudioManager::get().playCue(won ? AudioManager::Cue::Victory
                                        : AudioManager::Cue::Defeat);
    }

    // Phase 4: a win rolls straight on to the reward. A loss waits, because
    // "retry or abandon" is the player's call, not the game's.
    if (won && m_endScreen.elapsed() > EndScreen::kSlamEnd + 0.9f) {
        finishDuel();
    }
}

void DuelState::finishDuel() {
    const bool won = m_duel.winner() == Side::Player;
    g_run.setCommanderHp(m_duel.commander(Side::Player).getHp());

    if (!won) {
        m_stateManager.changeState(std::make_unique<RunOverState>(m_stateManager, m_font, false));
        return;
    }
    m_stateManager.changeState(std::make_unique<RewardState>(m_stateManager, m_font));
}

// ---- rendering --------------------------------------------------------------

/**
 * The board, as furniture.
 *
 * The battlefield illustration is a bright silver mandala across the whole
 * centre of the screen, and the board was drawn as nothing at all - empty
 * ground stayed empty, and a cell only appeared while a card was being dragged.
 * The result was that the busiest thing on screen was decoration and the parts
 * a player actually has to read were invisible until they were already
 * committing to a move.
 *
 * Two changes, both here: the art is pushed back under a veil weighted toward
 * the middle where the mandala is brightest, and all sixteen unit cells plus
 * the six counter cells are drawn as standing slots with metal edges.
 *
 * They are drawn *under* the rows, so an occupied cell is a frame sitting in a
 * socket rather than a frame with a box around it.
 */
void DuelState::renderBoardGrid(sf::RenderTarget& target) {
    // A veil, not a flat wash: heaviest over the centre column, where the
    // mandala is, and lifting toward the edges so the corners of the artwork
    // still show. Four bands is enough for a gradient this shallow.
    const struct { float x0, x1; float alpha; } bands[] = {
        {   0.0f,  240.0f,  56.0f },
        { 240.0f,  520.0f, 104.0f },
        { 520.0f,  760.0f, 132.0f },
        { 760.0f, 1040.0f, 104.0f },
        { 1040.0f, 1280.0f, 56.0f },
    };
    for (const auto& band : bands) {
        sf::VertexArray veil(sf::TriangleStrip, 4);
        const sf::Color c(8, 8, 13, static_cast<sf::Uint8>(band.alpha));
        veil[0] = sf::Vertex({ band.x0, 0.0f }, c);
        veil[1] = sf::Vertex({ band.x1, 0.0f }, c);
        veil[2] = sf::Vertex({ band.x0, 720.0f }, c);
        veil[3] = sf::Vertex({ band.x1, 720.0f }, c);
        target.draw(veil);
    }

    // Sockets. The player's are warm and the enemy's cool, so which half of the
    // board you are looking at is answerable without reading a single number.
    for (Side side : { Side::Opponent, Side::Player }) {
        const bool mine = (side == Side::Player);
        const sf::Color edge = mine ? sf::Color(150, 128, 86, 150)
                                    : sf::Color(126, 96, 138, 140);

        for (BoardLine line : { BoardLine::Frontline, BoardLine::Support }) {
            // The frontline is where the fighting happens, so it carries the
            // stronger edge: a glance has to separate the two rows.
            const float weight = (line == BoardLine::Frontline) ? 1.0f : 0.62f;

            for (int slot = 0; slot < Board::kLineSlots; ++slot) {
                const sf::FloatRect cell = Layout::unitRect(side, line, slot);
                drawPanel(target, cell, sf::Color(10, 10, 16, 96),
                          sf::Color(edge.r, edge.g, edge.b,
                                    static_cast<sf::Uint8>(edge.a * weight)));

                // Corner ticks rather than a full second border: they mark the
                // cell as a socket without adding another closed rectangle to a
                // screen that already has a lot of them.
                const float tick = 9.0f;
                const sf::Color bright(edge.r, edge.g, edge.b,
                                       static_cast<sf::Uint8>(220 * weight));
                sf::VertexArray corners(sf::Lines, 16);
                const sf::Vector2f p[4] = {
                    { cell.left, cell.top },
                    { cell.left + cell.width, cell.top },
                    { cell.left, cell.top + cell.height },
                    { cell.left + cell.width, cell.top + cell.height },
                };
                const float sx[4] = {  1.0f, -1.0f,  1.0f, -1.0f };
                const float sy[4] = {  1.0f,  1.0f, -1.0f, -1.0f };
                for (int i = 0; i < 4; ++i) {
                    corners[i * 4 + 0] = sf::Vertex(p[i], bright);
                    corners[i * 4 + 1] = sf::Vertex(p[i] + sf::Vector2f(tick * sx[i], 0.0f), bright);
                    corners[i * 4 + 2] = sf::Vertex(p[i], bright);
                    corners[i * 4 + 3] = sf::Vertex(p[i] + sf::Vector2f(0.0f, tick * sy[i]), bright);
                }
                target.draw(corners);
            }

            // Which row is which, once per row, in the empty left margin.
            const sf::FloatRect band = Layout::rowBand(side, line);
            drawLabel(target,
                      line == BoardLine::Frontline ? "FRONTLINE" : "SUPPORT",
                      { 296.0f, band.top + band.height / 2.0f - 5.0f }, 9,
                      sf::Color(edge.r, edge.g, edge.b, 170), 2.0f);
        }
    }
}

void DuelState::renderRow(sf::RenderTarget& target, Side side, BoardLine line) {
    const int occupied = static_cast<int>(m_duel.board().unitsIn(side, line).size());

    for (int i = 0; i < Board::kLineSlots; ++i) {
        Unit* unit = m_duel.board().at(side, line, i);
        if (!unit) continue;
        sf::FloatRect box = Layout::unitRect(side, line, i);

        // A lunging frame is drawn shifted; everything derived from the slot
        // rectangle moves with it so the highlight does not tear away.
        const sf::Vector2f shove = m_combat.offsetFor(unit->instanceId);
        box.left += shove.x;
        box.top += shove.y;

        // A freshly landed frame compresses and springs back. It is squashed
        // about its own base, not its centre, so the feet stay on the deck and
        // only the top of the chassis dips.
        const float squash = m_combat.squashFor(unit->instanceId);
        if (squash != 1.0f) {
            const float bottom = box.top + box.height;
            box.height *= squash;
            box.top = bottom - box.height;
        }

        const bool mine = (side == Side::Player);
        const bool selectable = mine && isPlayerTurn() && unit->canAct();
        const bool selected = (m_dragUnitId == unit->instanceId) ||
                              (std::find(m_tributes.begin(), m_tributes.end(),
                                         unit->instanceId) != m_tributes.end());
        bool targeted = false;
        if (m_mode == Interaction::DraggingUnit && !mine) {
            const auto legal = m_duel.legalTargets(m_dragUnitId);
            targeted = std::find(legal.begin(), legal.end(), unit->instanceId) != legal.end();
        }
        if (m_mode == Interaction::SelectingTributes && mine) targeted = !selected;

        CardArt::drawUnit(target, m_font, *unit, box, selectable, selected, targeted);

        // The badges flare as the frame comes online. Drawn here rather than
        // inside drawUnit so CardArt stays free of combat timing.
        const float pulse = m_combat.badgePulseFor(unit->instanceId);
        if (pulse > 0.0f) {
            const float artH = box.height * 0.70f;
            const float badgeY = box.top + artH + (box.height - artH) / 2.0f;
            for (int corner = 0; corner < 2; ++corner) {
                const float x = corner == 0 ? box.left + 16.0f
                                            : box.left + box.width - 16.0f;
                sf::CircleShape ring(13.0f + 9.0f * (1.0f - pulse), 18);
                ring.setOrigin(ring.getRadius(), ring.getRadius());
                ring.setPosition(x, badgeY);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineThickness(2.0f);
                ring.setOutlineColor(sf::Color(255, 246, 214,
                                               static_cast<sf::Uint8>(pulse * 225.0f)));
                target.draw(ring);
            }
        }

        const sf::Color wash = m_combat.overlayFor(unit->instanceId);
        if (wash.a > 0) {
            sf::RectangleShape overlay({ box.width, box.height });
            overlay.setPosition(box.left, box.top);
            overlay.setFillColor(wash);
            target.draw(overlay);
        }
    }

    // Empty ground stays empty. A drop zone only appears while a card is in
    // the air, which is what keeps the board from looking like a spreadsheet.
    if (m_mode == Interaction::DraggingCard && side == Side::Player &&
        occupied < Board::kLineSlots) {
        const bool over = Layout::rowBand(side, line).contains(m_mousePos);
        const sf::FloatRect ghost =
            Layout::ghostRect(side, line, m_duel.board().freeSlot(side, line));
        sf::RectangleShape cell({ ghost.width, ghost.height });
        cell.setPosition(ghost.left, ghost.top);
        cell.setFillColor(over ? sf::Color(96, 76, 30, 150) : sf::Color(48, 42, 34, 70));
        cell.setOutlineThickness(over ? 2.0f : 1.0f);
        cell.setOutlineColor(over ? sf::Color(240, 198, 96) : sf::Color(120, 104, 72, 130));
        target.draw(cell);
    }
}

void DuelState::renderFrontLine(sf::RenderTarget& target) {
    // Where the two frontlines meet.
    //
    // This was a row of hard red dashes, 13x3 at alpha 190. Against the current
    // battlefield art - a cool silver-blue moon mandala on near-black - it was
    // the only warm thing on screen and it sawed straight through the centre of
    // the illustration. Two things changed: the colour now belongs to the same
    // palette as the art, and the alpha is shaped so the rule has no hard stop
    // at either end and thins out over the middle, letting the moon read whole.
    const float y = Layout::kFrontLineY;
    const float x0 = 190.0f;
    const float x1 = 1090.0f;
    const float mid = (x0 + x1) / 2.0f;
    const float half = (x1 - x0) / 2.0f;

    // Sampled from the artwork's own line-work rather than picked by eye.
    const sf::Color ink(150, 172, 205);

    auto alphaAt = [&](float x) {
        // Fade to nothing at both ends: a rule that simply stops looks cut off.
        const float toEdge = 1.0f - std::min(1.0f, std::abs(x - mid) / half);
        const float ends = std::min(1.0f, toEdge * 3.2f);
        // ...and thin over the brightest part of the mandala. Narrow and shallow
        // on purpose: the lanes are centred here too (they span 374..906), so a
        // wide, deep notch would fade the rule out exactly where the two
        // frontlines actually meet. It eases over the moon's core, no further.
        const float d = (x - mid) / 96.0f;
        const float notch = 1.0f - 0.42f * std::exp(-d * d);
        return ends * notch;
    };

    // Two passes: a soft wide bloom under a one-pixel core, which is how the
    // hairlines in the background art read.
    const struct { float thickness; float scale; } passes[] = {
        { 3.0f, 0.20f },
        { 1.0f, 0.85f },
    };

    for (const auto& pass : passes) {
        sf::VertexArray strip(sf::TriangleStrip);
        for (float x = x0; x <= x1; x += 10.0f) {
            const sf::Uint8 a =
                static_cast<sf::Uint8>(std::clamp(alphaAt(x) * pass.scale, 0.0f, 1.0f) * 255.0f);
            const sf::Color c(ink.r, ink.g, ink.b, a);
            strip.append(sf::Vertex({ x, y - pass.thickness / 2.0f }, c));
            strip.append(sf::Vertex({ x, y + pass.thickness / 2.0f }, c));
        }
        target.draw(strip);
    }

    // Ticks on the lane boundaries. The dashes used to imply the divisions by
    // accident; marking the real ones says the same thing and says it correctly.
    for (int i = 0; i <= Board::kLineSlots; ++i) {
        const float x = Layout::kRowX + i * (Layout::kUnitW + Layout::kUnitGap)
                      - (i == Board::kLineSlots ? Layout::kUnitGap : 0.0f);
        const sf::Uint8 a = static_cast<sf::Uint8>(
            std::clamp(alphaAt(x) * 0.75f, 0.0f, 1.0f) * 255.0f);
        sf::RectangleShape tick({ 1.0f, 9.0f });
        tick.setOrigin(0.5f, 4.5f);
        tick.setPosition(x, y);
        tick.setFillColor(sf::Color(ink.r, ink.g, ink.b, a));
        target.draw(tick);
    }
}

void DuelState::renderHq(sf::RenderTarget& target, Side side) {
    const sf::FloatRect box = Layout::hqCard(side);
    const Commander& cmd = m_duel.commander(side);
    const bool isEnemy = (side == Side::Opponent);

    const bool targetable = (m_mode == Interaction::DraggingUnit && isEnemy && [&] {
        const auto legal = m_duel.legalTargets(m_dragUnitId);
        return std::find(legal.begin(), legal.end(), -1) != legal.end();
    }());

    const sf::Color accent = isEnemy ? sf::Color(198, 96, 198) : sf::Color(236, 190, 74);

    sf::RectangleShape card({ box.width, box.height });
    card.setPosition(box.left, box.top);
    card.setFillColor(sf::Color(24, 20, 16, 246));
    card.setOutlineThickness(targetable ? 3.0f : 1.5f);
    card.setOutlineColor(targetable ? sf::Color(255, 96, 84) : accent);
    target.draw(card);

    // No portrait and no name on this card any more. It used to carry both,
    // which was right when it floated in open ground at the far end of the
    // board; now that it is pinned to the commander strip, the portrait and the
    // name are already an inch away and the card was showing them twice.
    //
    // A short bracket ties it to the strip instead, so the two read as one
    // block rather than as two things that happen to be adjacent.
    const float tieY = isEnemy ? box.top : box.top + box.height;
    sf::VertexArray tie(sf::Lines, 4);
    const sf::Color tieInk(accent.r, accent.g, accent.b, 150);
    tie[0] = sf::Vertex({ box.left + 10.0f, tieY }, tieInk);
    tie[1] = sf::Vertex({ box.left + 10.0f, tieY + (isEnemy ? -14.0f : 14.0f) }, tieInk);
    tie[2] = sf::Vertex({ box.left + box.width - 10.0f, tieY }, tieInk);
    tie[3] = sf::Vertex({ box.left + box.width - 10.0f, tieY + (isEnemy ? -14.0f : 14.0f) }, tieInk);
    target.draw(tie);

    const float textLeft = box.left + 12.0f;
    drawLabel(target, "REACTOR CORE", { textLeft, box.top + 8.0f }, 9,
              sf::Color(accent.r, accent.g, accent.b, 220), 2.6f, true);

    // The bar gets the width the portrait and name were using.
    const float ratio = static_cast<float>(cmd.getHp()) /
                        static_cast<float>(std::max(1, cmd.getMaxHp()));
    drawBar(target, { textLeft, box.top + 26.0f, box.width - 110.0f, 14.0f },
            ratio, ratio < 0.3f ? sf::Color(216, 52, 48) : sf::Color(176, 60, 62),
            sf::Color(38, 30, 30, 235));

    // The number itself, right-aligned in its own well
    sf::Text hp;
    hp.setFont(m_font);
    hp.setString(std::to_string(cmd.getHp()));
    hp.setCharacterSize(24);
    hp.setStyle(sf::Text::Bold);
    hp.setFillColor(cmd.getHp() <= 8 ? sf::Color(244, 96, 88) : sf::Color(244, 230, 204));
    TextUtils::centerBoth(hp);
    hp.setPosition(box.left + box.width - 32.0f, box.top + box.height / 2.0f - 2.0f);
    target.draw(hp);

    if (targetable) {
        drawLabel(target, "STRIKE THE CORE",
                  { box.left + box.width / 2.0f - 46.0f, box.top + box.height + 4.0f }, 11,
                  sf::Color(255, 130, 118), 2.0f);
    }
}

void DuelState::renderCommanderPanel(sf::RenderTarget& target, Side side) {
    const sf::FloatRect panel = Layout::commanderPanel(side);
    const Commander& cmd = m_duel.commander(side);
    const bool isEnemy = (side == Side::Opponent);
    const sf::Color accent = isEnemy ? sf::Color(198, 120, 198) : sf::Color(214, 178, 108);

    // Portrait with a rank plate beside it, KARDS-style, no boxed-in panel.
    const sf::FloatRect frame(panel.left, panel.top, 62.0f, 62.0f);
    CardArt::drawAvatar(target, isEnemy ? m_enemyArtPath : m_playerArtPath, frame, accent);

    sf::Text name;
    name.setFont(m_font);
    name.setString(cmd.getName());
    name.setCharacterSize(16);
    name.setStyle(sf::Text::Bold);
    name.setLetterSpacing(1.4f);
    name.setFillColor(sf::Color(236, 228, 214));
    // The longest faction title at 16px overran the strip and ended up behind
    // the player's own hand. Shrink until it fits the space it actually has.
    while (name.getCharacterSize() > 10 &&
           name.getLocalBounds().width > panel.width - 76.0f) {
        name.setCharacterSize(name.getCharacterSize() - 1);
    }
    name.setPosition(panel.left + 72.0f, panel.top + 4.0f);
    target.draw(name);

    const std::string cores = std::string(displayName(cmd.getPrimaryRole())) + " / "
                            + displayName(cmd.getSecondaryRole());
    drawLabel(target, cores,
              { panel.left + 72.0f, panel.top + 26.0f }, 10,
              sf::Color(accent.r, accent.g, accent.b, 210), 2.0f);

    // Energy as a row of cells rather than a stamped "2 | 2".
    //
    // The number was accurate and useless: deciding whether a 3-cost card is
    // affordable meant reading two digits and subtracting, every time. Ten
    // cells answer it at a glance - the lit ones are what is left, and the
    // outlined ones are the ceiling this turn.
    const int spent = cmd.getMana();
    const int cap = cmd.getManaCap();
    const float pipY = panel.top + 44.0f;
    const float pipW = 11.0f;
    const float pipGap = 2.0f;

    for (int i = 0; i < 10; ++i) {
        const sf::FloatRect cell(panel.left + 72.0f + i * (pipW + pipGap), pipY, pipW, 15.0f);
        const bool inCap = i < cap;
        const bool charged = i < spent;

        sf::RectangleShape body({ cell.width, cell.height });
        body.setPosition(cell.left, cell.top);
        // An empty cell inside the cap has to read as EMPTY. The first pass
        // filled it dark olive under a gold outline, which at this size looked
        // charged - a 0/4 turn showed four apparently lit cells. The fill is
        // now near-black for both empty states and only the outline says
        // whether the cell is in this turn's cap.
        body.setFillColor(charged ? sf::Color(248, 196, 70) : sf::Color(18, 17, 22, 230));
        body.setOutlineThickness(1.0f);
        body.setOutlineColor(inCap ? sf::Color(186, 144, 60, 235)
                                   : sf::Color(58, 54, 62, 160));
        target.draw(body);

        // A charged cell gets a highlight down its middle, so a lit row is
        // legible as cells and not as one solid gold bar.
        if (charged) {
            sf::RectangleShape spark({ 2.0f, cell.height - 6.0f });
            spark.setPosition(cell.left + cell.width / 2.0f - 1.0f, cell.top + 3.0f);
            spark.setFillColor(sf::Color(255, 244, 196));
            target.draw(spark);
        }
    }

    // The exact figure, small, under the cells: the cells answer "can I afford
    // this", the number answers "how much exactly".
    std::stringstream energy;
    energy << spent << " / " << cap << "  ENERGY";
    drawLabel(target, energy.str(),
              { panel.left + 72.0f, pipY + 18.0f }, 9,
              sf::Color(180, 156, 110), 1.8f);

    // The overcharge core sits beside the energy cells. A Paladin always shows
    // it, empty or not, because a resource that only appears once it has
    // something in it is a resource the player never learns they have.
    if (cmd.getOvercharge() > 0 || cmd.getPrimaryRole() == MechRole::Paladin) {
        const sf::FloatRect core(panel.left + 202.0f, pipY - 1.0f, 44.0f, 17.0f);
        drawPanel(target, core, sf::Color(14, 24, 30, 245), sf::Color(96, 206, 226));
        sf::Text charge;
        charge.setFont(Fonts::ui());
        charge.setString(std::to_string(cmd.getOvercharge()));
        charge.setCharacterSize(13);
        charge.setStyle(sf::Text::Bold);
        charge.setFillColor(sf::Color(126, 226, 244));
        TextUtils::centerBoth(charge);
        charge.setPosition(core.left + 13.0f, core.top + 9.0f);
        target.draw(charge);
        drawLabel(target, "OC", { core.left + 24.0f, core.top + 4.0f }, 9,
                  sf::Color(96, 166, 186), 1.4f);
    }

    renderPiles(target, side);
}

/// Forwards to the shared painter so a hidden card looks the same in the draw
/// piles, the enemy's hand and the counter zone.
void DuelState::drawCardBack(sf::RenderTarget& target, sf::FloatRect box, Side owner,
                             sf::Color accent, float alpha) const {
    CardArt::drawCardBack(target, box, owner, accent, alpha);
}

/// Deck and scrap, drawn as stacks whose depth tracks the count.
void DuelState::renderPiles(sf::RenderTarget& target, Side side) {
    const Commander& cmd = m_duel.commander(side);
    const bool isEnemy = (side == Side::Opponent);
    const sf::Color accent = isEnemy ? sf::Color(198, 120, 198) : sf::Color(214, 178, 108);

    struct Pile {
        sf::FloatRect box;
        int count;
        const char* label;
        sf::Color tint;
    };
    const Pile piles[2] = {
        { Layout::deckPile(side),  static_cast<int>(cmd.getDeck().size()),  "DECK",  accent },
        { Layout::scrapPile(side), static_cast<int>(cmd.getCrypt().size()), "SCRAP",
          sf::Color(126, 120, 130) },
    };

    for (const Pile& pile : piles) {
        // Up to four leaves, one per ten cards, offset back and up.
        const int leaves = std::min(4, std::max(pile.count > 0 ? 1 : 0, pile.count / 10));
        for (int i = leaves; i >= 1; --i) {
            sf::FloatRect leaf = pile.box;
            leaf.left -= static_cast<float>(i) * 2.0f;
            leaf.top -= static_cast<float>(i) * 2.0f;
            drawCardBack(target, leaf, side, pile.tint, 0.55f);
        }
        if (pile.count > 0) {
            drawCardBack(target, pile.box, side, pile.tint, 1.0f);
        } else {
            sf::RectangleShape empty({ pile.box.width, pile.box.height });
            empty.setPosition(pile.box.left, pile.box.top);
            empty.setFillColor(sf::Color(14, 13, 18, 190));
            empty.setOutlineThickness(1.0f);
            empty.setOutlineColor(sf::Color(70, 66, 76, 160));
            target.draw(empty);
        }

        // The count rides on the pile itself, so it is legible without a click;
        // hovering adds the full name underneath.
        sf::Text n;
        n.setFont(m_font);
        n.setString(std::to_string(pile.count));
        n.setCharacterSize(18);
        n.setStyle(sf::Text::Bold);
        n.setFillColor(sf::Color(238, 232, 222));
        n.setOutlineColor(sf::Color(0, 0, 0, 220));
        n.setOutlineThickness(2.0f);
        TextUtils::centerBoth(n);
        n.setPosition(pile.box.left + pile.box.width / 2.0f,
                      pile.box.top + pile.box.height / 2.0f);
        target.draw(n);

        const bool hot = pile.box.contains(m_mousePos);
        drawLabel(target, pile.label,
                  { pile.box.left + 1.0f, pile.box.top + pile.box.height + 3.0f }, 9,
                  hot ? sf::Color(226, 214, 196) : sf::Color(126, 120, 128), 1.8f);
        if (hot) {
            // Spelling it out on hover, since the pile itself only ever shows
            // a bare number.
            const std::string detail = std::string(pile.label) + ": "
                                     + std::to_string(pile.count) + " cards";
            drawLabel(target, detail,
                      { pile.box.left, pile.box.top - 16.0f }, 11,
                      sf::Color(238, 226, 206), 1.2f);
        }
    }
}

bool DuelState::outOfMoves() const {
    if (!isPlayerTurn()) return false;
    for (const CardData& card : m_duel.commander(Side::Player).getHand()) {
        if (canPlayCard(card)) return false;
    }
    // A frame that can still swing is a move too, so a board full of ready
    // attackers must not read as a spent turn.
    for (const Unit* unit : m_duel.board().unitsIn(Side::Player, BoardLine::Frontline)) {
        if (unit && unit->canAct()) return false;
    }
    for (const Unit* unit : m_duel.board().unitsIn(Side::Player, BoardLine::Support)) {
        if (unit && unit->canAct()) return false;
    }
    return true;
}

/// The End Turn control, plus what it has to say about the turn.
void DuelState::renderEndTurn(sf::RenderTarget& target) {
    const bool spent = outOfMoves();
    if (spent) {
        // A slow pulse in green rather than the standing gold. Nothing else on
        // the board is green, so it reads at the edge of vision - which is the
        // point, since the player is looking at their hand, not at this corner.
        const float pulse = 0.5f + 0.5f * std::sin(m_uiClock * 4.2f);
        const sf::FloatRect box = m_endTurnButton.box.getGlobalBounds();
        for (int ring = 3; ring >= 1; --ring) {
            const float grow = static_cast<float>(ring) * 3.0f;
            sf::RectangleShape glow({ box.width + grow * 2.0f, box.height + grow * 2.0f });
            glow.setPosition(box.left - grow, box.top - grow);
            glow.setFillColor(sf::Color::Transparent);
            glow.setOutlineThickness(1.5f);
            glow.setOutlineColor(sf::Color(120, 226, 150,
                static_cast<sf::Uint8>((70.0f / ring) * (0.45f + 0.55f * pulse))));
            target.draw(glow);
        }
    }

    m_endTurnButton.render(target);

    // The shortcut, spelled out under the control. Space already ended the
    // turn; nothing on screen said so.
    const sf::FloatRect box = m_endTurnButton.box.getGlobalBounds();
    sf::Text hint;
    hint.setFont(Fonts::ui());
    hint.setString("[ SPACE ]");
    hint.setCharacterSize(9);
    hint.setLetterSpacing(2.4f);
    hint.setFillColor(m_endTurnButton.enabled
                          ? (spent ? sf::Color(150, 226, 170) : sf::Color(148, 138, 122))
                          : sf::Color(92, 88, 84));
    TextUtils::centerBoth(hint);
    hint.setPosition(box.left + box.width / 2.0f, box.top + box.height + 11.0f);
    target.draw(hint);
}

void DuelState::renderLogButton(sf::RenderTarget& target) {
    if (!Settings::get().showBattleLog) return;
    const sf::FloatRect box = Layout::logButton();
    const bool hot = box.contains(m_mousePos) || m_logOpen;

    drawPanel(target, box,
              hot ? sf::Color(38, 34, 28, 246) : sf::Color(22, 20, 26, 232),
              hot ? sf::Color(226, 190, 110) : sf::Color(96, 90, 100, 200));

    for (int i = 0; i < 3; ++i) {
        sf::RectangleShape bar({ box.width - 22.0f, 3.0f });
        bar.setPosition(box.left + 11.0f, box.top + 12.0f + static_cast<float>(i) * 9.0f);
        bar.setFillColor(hot ? sf::Color(240, 212, 150) : sf::Color(178, 170, 164));
        target.draw(bar);
    }

    if (hot && !m_logOpen) {
        drawLabel(target, "LOG",
                  { box.left + box.width + 8.0f, box.top + 14.0f }, 11,
                  sf::Color(210, 202, 194), 2.0f);
    }
}

void DuelState::renderLogPanel(sf::RenderTarget& target) {
    if (!m_logOpen || !Settings::get().showBattleLog) return;

    // Stops at 526 so it never covers the player strip that starts at 552.
    const sf::FloatRect panel(84.0f, 132.0f, 330.0f, 394.0f);
    drawPanel(target, panel, sf::Color(12, 11, 16, 246), sf::Color(150, 128, 84, 220));

    drawLabel(target, "BATTLE LOG",
              { panel.left + 14.0f, panel.top + 12.0f }, 13,
              sf::Color(236, 210, 150), 2.6f, true);

    sf::RectangleShape rule({ panel.width - 28.0f, 1.0f });
    rule.setPosition(panel.left + 14.0f, panel.top + 34.0f);
    rule.setFillColor(sf::Color(120, 104, 70, 200));
    target.draw(rule);

    // Newest last, oldest trimmed - the tail is what a player just missed.
    sf::Text body;
    body.setFont(Fonts::ui());
    body.setCharacterSize(12);
    body.setLineSpacing(1.45f);
    body.setFillColor(sf::Color(196, 190, 180));
    std::string joined;
    for (const std::string& line : m_log) {
        joined += TextUtils::wrap(line, m_font, 12, panel.width - 30.0f) + "\n";
    }
    body.setString(joined);
    body.setPosition(panel.left + 14.0f, panel.top + 44.0f);
    target.draw(body);

    drawLabel(target, "CLICK THE BARS TO CLOSE",
              { panel.left + 14.0f, panel.top + panel.height - 22.0f }, 9,
              sf::Color(118, 112, 106), 1.8f);
}

/// The enemy hand, face-down along the top edge.
sf::FloatRect DuelState::enemyHandCardBox(int index, int count) const {
    const sf::FloatRect area = Layout::enemyHandFan();
    const float cardW = 42.0f;
    const float cardH = 60.0f;
    // Cards tighten up as the grip grows rather than running off the screen.
    const float step = count > 1
        ? std::min(24.0f, (area.width - cardW) / static_cast<float>(count - 1))
        : 0.0f;
    const float span = cardW + step * static_cast<float>(count - 1);
    const float startX = area.left + (area.width - span) / 2.0f;
    return { startX + step * static_cast<float>(index), area.top + 2.0f, cardW, cardH };
}

void DuelState::renderEnemyHand(sf::RenderTarget& target) {
    const int count = static_cast<int>(m_duel.commander(Side::Opponent).getHand().size());
    const sf::FloatRect area = Layout::enemyHandFan();
    if (count <= 0) {
        drawLabel(target, "NO CARDS",
                  { area.left + 60.0f, area.top + 24.0f }, 11,
                  sf::Color(120, 110, 124), 2.0f);
        return;
    }

    const float cardH = 60.0f;
    for (int i = 0; i < count; ++i) {
        if (m_drawFlight.hides(Side::Opponent, i)) continue;   // still in the air
        drawCardBack(target, enemyHandCardBox(i, count), Side::Opponent,
                     sf::Color(198, 120, 198), 1.0f);
    }

    sf::Text n;
    n.setFont(Fonts::ui());
    n.setString(std::to_string(count));
    n.setCharacterSize(15);
    n.setStyle(sf::Text::Bold);
    n.setFillColor(sf::Color(240, 214, 240));
    n.setOutlineColor(sf::Color(0, 0, 0, 230));
    n.setOutlineThickness(2.0f);
    TextUtils::centerBoth(n);
    n.setPosition(area.left + area.width / 2.0f, area.top + cardH + 12.0f);
    target.draw(n);
}

void DuelState::renderTraps(sf::RenderTarget& target, Side side) {
    const auto& traps = m_duel.board().traps(side);
    const bool dropHere = (m_mode == Interaction::DraggingCard && side == Side::Player &&
                           m_dragCardIndex >= 0);

    bool draggingTrap = false;
    if (dropHere) {
        const auto& hand = m_duel.commander(Side::Player).getHand();
        if (m_dragCardIndex < static_cast<int>(hand.size())) {
            draggingTrap = hand[static_cast<size_t>(m_dragCardIndex)].category == CardCategory::Trap;
        }
    }

    for (int i = 0; i < Board::kTrapSlots; ++i) {
        const sf::FloatRect box = Layout::trapSlot(side, i);
        const TrapCard* trap =
            i < static_cast<int>(traps.size()) ? &traps[static_cast<size_t>(i)] : nullptr;
        // An empty counter cell used to appear only while a counter was being
        // dragged, which meant the zone existed but nothing said so until you
        // had already picked up the card that goes in it. It stands there now.
        if (!trap) {
            drawPanel(target, box, sf::Color(10, 10, 16, 110),
                      side == Side::Player ? sf::Color(132, 112, 76, 130)
                                           : sf::Color(112, 86, 124, 120));
            // A closed latch: two bars and a keyway, so the empty cell says
            // what it is for rather than being one more blank rectangle.
            const float cx = box.left + box.width / 2.0f;
            const float cy = box.top + box.height / 2.0f;
            const sf::Color ink = side == Side::Player ? sf::Color(150, 128, 86, 130)
                                                       : sf::Color(126, 96, 138, 120);
            sf::RectangleShape bar({ box.width * 0.44f, 2.0f });
            bar.setOrigin(bar.getSize().x / 2.0f, 1.0f);
            bar.setFillColor(ink);
            for (int k = -1; k <= 1; k += 2) {
                bar.setPosition(cx, cy + static_cast<float>(k) * 9.0f);
                target.draw(bar);
            }
            sf::CircleShape keyway(4.0f, 12);
            keyway.setOrigin(4.0f, 4.0f);
            keyway.setPosition(cx, cy);
            keyway.setFillColor(sf::Color::Transparent);
            keyway.setOutlineThickness(1.5f);
            keyway.setOutlineColor(ink);
            target.draw(keyway);
        }
        if (!trap && !draggingTrap) continue;
        const bool highlighted = draggingTrap && !trap && Layout::trapZone(side).contains(m_mousePos);
        CardArt::drawTrapSlot(target, m_font, trap, box, side,
                              side == Side::Player, highlighted);
    }

    drawLabel(target,
              side == Side::Player ? "COUNTER-PROTOCOLS" : "ENEMY COUNTERS",
              { Layout::kTrapX, Layout::trapZone(side).top - 14.0f }, 9,
              traps.empty() ? sf::Color(102, 94, 84) : sf::Color(160, 140, 108), 2.2f);
}

void DuelState::renderHand(sf::RenderTarget& target) {
    const auto& hand = m_duel.commander(Side::Player).getHand();
    const int count = static_cast<int>(hand.size());
    const sf::Vector2f size = handCardSize();

    // Draw left to right so later cards overlap earlier ones, then lift the
    // hovered card clear of the stack.
    for (int i = 0; i < count; ++i) {
        if (i == m_dragCardIndex || i == m_hoverCardIndex) continue;
        if (m_drawFlight.hides(Side::Player, i)) continue;   // still in the air
        const CardData& card = hand[static_cast<size_t>(i)];
        CardArt::drawCard(target, m_font, card, handCardCentre(i, count), size,
                          handCardTilt(i, count),
                          isPlayerTurn() && canPlayCard(card), false);
    }
    // The pointed-at card last, so it is over the whole fan rather than only
    // over the cards drawn before it.
    if (m_hoverCardIndex >= 0 && m_hoverCardIndex < count && m_hoverCardIndex != m_dragCardIndex
        && !m_drawFlight.hides(Side::Player, m_hoverCardIndex)) {
        const CardData& card = hand[static_cast<size_t>(m_hoverCardIndex)];
        CardArt::drawCard(target, m_font, card, handCardCentre(m_hoverCardIndex, count),
                          size * kHoverZoom, 0.0f,
                          isPlayerTurn() && canPlayCard(card), true);
    }
}

/// Cards mid-flight from a pile to a hand. Drawn last of the board layers so a
/// card passing over the rows is not clipped by them.
void DuelState::renderDrawFlights(sf::RenderTarget& target) {
    for (const DrawFlight::Frame& frame : m_drawFlight.frames()) {
        if (frame.faceUp && frame.owner == Side::Player) {
            CardArt::drawCard(target, m_font, *frame.card, frame.centre, frame.size,
                              0.0f, true, false);
        } else {
            // The enemy's cards never turn over, and the player's are a back
            // until the pinch: a face visible on the way up would give away the
            // draw before the flip has anything left to reveal.
            const sf::FloatRect box(frame.centre.x - frame.size.x / 2.0f,
                                    frame.centre.y - frame.size.y / 2.0f,
                                    frame.size.x, frame.size.y);
            drawCardBack(target, box, frame.owner,
                         frame.owner == Side::Player ? sf::Color(214, 178, 108)
                                                     : sf::Color(198, 120, 198),
                         frame.alpha);
        }
    }
}

void DuelState::renderDragOverlay(sf::RenderTarget& target) {
    if (m_mode == Interaction::DraggingCard && m_dragCardIndex >= 0 && m_dragMoved) {
        const auto& hand = m_duel.commander(Side::Player).getHand();
        if (m_dragCardIndex < static_cast<int>(hand.size())) {
            CardArt::drawCard(target, m_font, hand[static_cast<size_t>(m_dragCardIndex)],
                              m_mousePos, handCardSize() * 1.05f, 0.0f, true, true);
        }
    }

    if (m_mode == Interaction::DraggingUnit && m_dragUnitId >= 0 && m_dragMoved) {
        const Unit* unit = m_duel.board().findById(m_dragUnitId);
        if (unit) {
            const sf::FloatRect box = rectOf(*unit);
            const sf::Vector2f from{ box.left + box.width / 2.0f, box.top + box.height / 2.0f };
            const sf::Vector2f delta = m_mousePos - from;
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (length > 4.0f) {
                sf::RectangleShape shaft({ length, 4.0f });
                shaft.setOrigin(0.0f, 2.0f);
                shaft.setPosition(from);
                shaft.setRotation(std::atan2(delta.y, delta.x) * 180.0f / 3.14159265f);
                shaft.setFillColor(sf::Color(255, 96, 84, 220));
                target.draw(shaft);

                sf::CircleShape head(11.0f, 3);
                head.setOrigin(11.0f, 11.0f);
                head.setPosition(m_mousePos);
                head.setRotation(std::atan2(delta.y, delta.x) * 180.0f / 3.14159265f + 90.0f);
                head.setFillColor(sf::Color(255, 120, 100, 235));
                target.draw(head);
            }
        }
    }
}

void DuelState::render(sf::RenderTarget& target) {
    // Shake by nudging the engine's letterboxed view, never by replacing it -
    // replacing it would drop the viewport and stretch the board.
    const sf::View previous = target.getView();
    sf::View shaken = previous;
    shaken.move(m_vfx.getShakeOffset() + m_endScreen.shakeOffset());
    target.setView(shaken);

    // Two stems, because a dropped-in file is as likely to be called one as the
    // other, and exists() only substitutes the EXTENSION - a .jpg named
    // battlefield_background sat in assets/ui doing nothing because the code
    // asked for battlefield_bg. Extension substitution then covers .png/.jpg.
    const char* fieldNames[] = { "assets/ui/battlefield_bg.png",
                                 "assets/ui/battlefield_background.png" };
    const char* field = nullptr;
    for (const char* name : fieldNames) {
        if (ResourceManager::exists(name)) { field = name; break; }
    }

    if (field) {
        const sf::Texture& tex = ResourceManager::get().getTexture(field);
        sf::Sprite sprite;
        coverScreen(sprite, tex);
        target.draw(sprite);
    } else {
        sf::RectangleShape bg({ 1280.0f, 720.0f });
        bg.setFillColor(sf::Color(13, 11, 16));
        target.draw(bg);

        // A faint vignette gives the bare table some depth without competing
        // with the cards for attention.
        sf::VertexArray glow(sf::TriangleStrip, 4);
        glow[0] = sf::Vertex({ 0.0f, 0.0f }, sf::Color(26, 22, 30, 0));
        glow[1] = sf::Vertex({ 1280.0f, 0.0f }, sf::Color(26, 22, 30, 0));
        glow[2] = sf::Vertex({ 0.0f, 720.0f }, sf::Color(30, 24, 18, 120));
        glow[3] = sf::Vertex({ 1280.0f, 720.0f }, sf::Color(30, 24, 18, 120));
        target.draw(glow);
    }

    // The board itself, over the artwork and under everything that moves.
    renderBoardGrid(target);

    // Beams and shockwaves sit under the frames; debris and arrows sit over
    // them, so a shot passes behind its target and its sparks land in front.
    m_combat.renderBelow(target);

    renderHq(target, Side::Opponent);
    renderRow(target, Side::Opponent, BoardLine::Support);
    renderRow(target, Side::Opponent, BoardLine::Frontline);
    renderFrontLine(target);
    renderRow(target, Side::Player, BoardLine::Frontline);
    renderRow(target, Side::Player, BoardLine::Support);
    renderHq(target, Side::Player);

    renderCommanderPanel(target, Side::Opponent);
    renderCommanderPanel(target, Side::Player);
    renderEnemyHand(target);
    renderLogButton(target);
    renderTraps(target, Side::Opponent);
    renderTraps(target, Side::Player);

    // Turn readout, top centre-left, out of the way of the board
    // Between the enemy strip (ends at 256) and the enemy reactor card (starts
    // at 506) - the one strip of the top bar nothing else claims.
    std::stringstream turn;
    turn << "TURN " << m_duel.turnNumber();
    drawLabel(target, turn.str(), { 300.0f, 14.0f }, 15,
              sf::Color(198, 186, 166), 2.6f, true);
    drawLabel(target,
              m_duel.activeSide() == Side::Player ? "YOUR MOVE" : "ENEMY MOVES",
              { 300.0f, 36.0f }, 13,
              m_duel.activeSide() == Side::Player ? sf::Color(240, 208, 128)
                                                  : sf::Color(198, 128, 198), 2.0f);

    renderEndTurn(target);

    renderLogPanel(target);

    renderHand(target);
    renderDrawFlights(target);
    m_combat.renderAbove(target);
    m_floating.render(target);
    renderDragOverlay(target);

    // Tribute prompt
    if (m_mode == Interaction::SelectingTributes) {
        sf::RectangleShape veil({ 1280.0f, 720.0f });
        veil.setFillColor(sf::Color(8, 6, 14, 120));
        target.draw(veil);

        drawPanel(target, { 340.0f, 500.0f, 600.0f, 74.0f },
                  sf::Color(20, 16, 24, 245), sf::Color(236, 190, 74, 220));
        std::stringstream prompt;
        prompt << "TRIBUTE  -  choose " << m_pendingNeeded << " unit(s) to offer   ("
               << m_tributes.size() << "/" << m_pendingNeeded << ")";
        sf::Text text;
        text.setFont(m_font);
        text.setString(prompt.str());
        text.setCharacterSize(17);
        text.setFillColor(sf::Color(244, 224, 176));
        TextUtils::centerBoth(text);
        text.setPosition(640.0f, 525.0f);
        target.draw(text);

        drawLabel(target, "right-click to cancel", { 560.0f, 546.0f }, 12,
                  sf::Color(150, 142, 130));
    }

    // Banner
    if (m_bannerTimer > 0.0f && !m_bannerText.empty()) {
        // A full-width sweep across the middle of the board. The old version
        // was a small plate tucked beside END TURN, which is exactly where a
        // player is not looking when they have just finished their turn - so
        // the handover kept going unnoticed.
        const bool mine = m_duel.activeSide() == Side::Player;
        const sf::Color accent = mine ? sf::Color(240, 206, 120)
                                      : sf::Color(214, 128, 214);

        const float life = m_bannerTimer / 2.1f;             // 1 at the start
        const float age = 1.0f - life;

        // Slide in over the first fifth, hold, then fade out.
        const float entry = std::clamp(age / 0.18f, 0.0f, 1.0f);
        const float ease = 1.0f - (1.0f - entry) * (1.0f - entry) * (1.0f - entry);
        const float alpha = std::clamp(life / 0.30f, 0.0f, 1.0f) * ease;
        const float slide = (1.0f - ease) * (mine ? -240.0f : 240.0f);

        auto fade = [alpha](sf::Color c, float k = 1.0f) {
            return sf::Color(c.r, c.g, c.b,
                             static_cast<sf::Uint8>(std::clamp(alpha * k, 0.0f, 1.0f) * 255.0f));
        };

        const float bandY = Layout::kFrontLineY - 34.0f;
        const float bandH = 62.0f;

        // The band itself: opaque at the centre, transparent at both ends, so
        // it reads as a sweep rather than a bar laid over the board.
        sf::VertexArray band(sf::TriangleStrip, 8);
        const float xs[4] = { 0.0f, 380.0f, 900.0f, 1280.0f };
        const float ks[4] = { 0.0f, 0.92f, 0.92f, 0.0f };
        for (int i = 0; i < 4; ++i) {
            band[i * 2 + 0] = sf::Vertex({ xs[i], bandY },
                                         fade(sf::Color(10, 8, 14), ks[i]));
            band[i * 2 + 1] = sf::Vertex({ xs[i], bandY + bandH },
                                         fade(sf::Color(10, 8, 14), ks[i]));
        }
        target.draw(band);

        for (int edge = 0; edge < 2; ++edge) {
            sf::VertexArray line(sf::TriangleStrip, 8);
            const float y = edge == 0 ? bandY : bandY + bandH - 1.5f;
            for (int i = 0; i < 4; ++i) {
                line[i * 2 + 0] = sf::Vertex({ xs[i], y }, fade(accent, ks[i]));
                line[i * 2 + 1] = sf::Vertex({ xs[i], y + 1.5f }, fade(accent, ks[i]));
            }
            target.draw(line);
        }

        sf::Text banner;
        banner.setFont(m_font);
        banner.setString(m_bannerText);
        banner.setCharacterSize(30);
        banner.setStyle(sf::Text::Bold);
        banner.setLetterSpacing(6.0f);
        banner.setFillColor(fade(mine ? sf::Color(252, 238, 196)
                                      : sf::Color(240, 206, 240)));
        banner.setOutlineColor(fade(sf::Color(0, 0, 0), 0.9f));
        banner.setOutlineThickness(2.5f);
        TextUtils::centerBoth(banner);
        banner.setPosition(640.0f + slide, bandY + bandH / 2.0f);
        target.draw(banner);
    }


    // Over the board and the hand, under the end sequence.
    m_spotlight.render(target);
    // Between the scrim and the verdict: EndScreen lays the scrim down first,
    // so the rig has to be drawn from inside that sandwich.
    // Under the scrim: the wash, the rays and the fracture belong to the board,
    // not to the verdict panel that dims it.
    m_endVfx.renderBelow(target);
    m_endScreen.renderScrim(target);
    m_portraitRig.render(target);
    // Over the portrait, under the lettering: ash, vignette and the seal frame
    // the verdict rather than sitting behind the broken frame.
    m_endVfx.renderAbove(target);

    // The end sequence covers everything, hand included - the duel is over and
    // the cards are no longer the subject.
    m_endScreen.render(target);
    if (m_endScreen.outcome() == EndScreen::Outcome::Defeat) {
        const float reveal = m_endScreen.uiReveal();
        const float slide = m_endScreen.uiSlide();
        m_retryButton.render(target, reveal, slide);
        m_menuButton.render(target, reveal, slide);
    }

    target.setView(previous);
}

// =============================================================================
// RewardState implementation
// =============================================================================

RewardState::RewardState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font) {
    m_offers = g_run.rollRewards();

    m_heading.setFont(font);
    m_heading.setString("REACTOR BREACHED");
    m_heading.setCharacterSize(34);
    m_heading.setStyle(sf::Text::Bold);
    m_heading.setLetterSpacing(4.0f);
    m_heading.setFillColor(sf::Color(247, 212, 122));
    TextUtils::centerBoth(m_heading);
    m_heading.setPosition(640.0f, 120.0f);

    m_subheading.setFont(font);
    m_subheading.setString("Choose one card to carry into the next battle");
    m_subheading.setCharacterSize(16);
    m_subheading.setFillColor(sf::Color(190, 184, 176));
    TextUtils::centerBoth(m_subheading);
    m_subheading.setPosition(640.0f, 164.0f);
}

void RewardState::enterPurgePhase() {
    m_phase = Phase::Purge;
    m_hovered = -1;
    m_skipHovered = false;

    m_heading.setString("SCRAP BAY");
    TextUtils::centerBoth(m_heading);
    m_heading.setPosition(640.0f, 120.0f);

    m_subheading.setString(g_run.canPurge()
        ? "Scrap one card out of the deck, or keep it as it is"
        : "The deck is already at its minimum - nothing can be scrapped");
    TextUtils::centerBoth(m_subheading);
    m_subheading.setPosition(640.0f, 164.0f);
}

bool RewardState::enterAugmentPhase() {
    if (!g_run.augmentOfferDue()) return false;
    m_augmentOffers = Augments::roll(g_run.getAugments());
    if (m_augmentOffers.empty()) return false;

    m_phase = Phase::Augment;
    m_hovered = -1;
    m_heading.setString("CORE AUGMENT");
    TextUtils::centerBoth(m_heading);
    m_heading.setPosition(640.0f, 96.0f);
    m_subheading.setString("One permanent upgrade for the rest of the run");
    TextUtils::centerBoth(m_subheading);
    m_subheading.setPosition(640.0f, 140.0f);
    return true;
}

void RewardState::renderAugments(sf::RenderTarget& target) const {
    for (int i = 0; i < static_cast<int>(m_augmentOffers.size()); ++i) {
        const Augments::Augment& augment = Augments::get(m_augmentOffers[static_cast<size_t>(i)]);
        const sf::FloatRect box = augmentPanel(i);
        const bool hot = i == m_hovered;

        drawPanel(target, box,
                  hot ? sf::Color(30, 34, 44, 246) : sf::Color(16, 18, 24, 238),
                  hot ? sf::Color(236, 190, 74) : sf::Color(88, 94, 106, 200));

        // A hexagonal core, drawn rather than an icon file: six augments would
        // otherwise be six PNGs that do not exist yet.
        sf::CircleShape core(34.0f, 6);
        core.setOrigin(34.0f, 34.0f);
        core.setPosition(box.left + box.width / 2.0f, box.top + 62.0f);
        core.setFillColor(hot ? sf::Color(64, 52, 22, 240) : sf::Color(28, 30, 38, 235));
        core.setOutlineThickness(2.0f);
        core.setOutlineColor(hot ? sf::Color(244, 206, 120) : sf::Color(120, 126, 138));
        target.draw(core);

        sf::Text name;
        name.setFont(m_font);
        name.setString(augment.name);
        name.setCharacterSize(17);
        name.setStyle(sf::Text::Bold);
        name.setFillColor(hot ? sf::Color(246, 226, 190) : sf::Color(216, 210, 200));
        TextUtils::centerBoth(name);
        name.setPosition(box.left + box.width / 2.0f, box.top + 122.0f);
        target.draw(name);

        sf::Text body;
        body.setFont(Fonts::ui());
        body.setString(TextUtils::wrap(augment.text, Fonts::ui(), 13, box.width - 36.0f));
        body.setCharacterSize(13);
        body.setLineSpacing(1.35f);
        body.setFillColor(sf::Color(178, 184, 194));
        TextUtils::centerHorizontally(body);
        body.setPosition(box.left + box.width / 2.0f, box.top + 152.0f);
        target.draw(body);
    }
}

void RewardState::leaveToMap() {
    // The augment screen sits between the scrap bay and the map, and only on
    // the fights that offer one.
    if (m_phase != Phase::Augment && enterAugmentPhase()) return;
    m_stateManager.changeState(std::make_unique<MapState>(m_stateManager, m_font));
}

int RewardState::pickAt(sf::Vector2f point) const {
    if (m_phase == Phase::Offer) {
        for (int i = 0; i < static_cast<int>(m_offers.size()); ++i) {
            const sf::Vector2f c = cardCentre(i);
            const sf::Vector2f s = cardSize();
            if (sf::FloatRect(c.x - s.x / 2.0f, c.y - s.y / 2.0f, s.x, s.y).contains(point)) {
                return i;
            }
        }
        return -1;
    }

    const int count = g_run.deckSize();
    for (int i = 0; i < count; ++i) {
        const sf::Vector2f c = gridCentre(i, count);
        const sf::Vector2f s = gridCardSize();
        if (sf::FloatRect(c.x - s.x / 2.0f, c.y - s.y / 2.0f, s.x, s.y).contains(point)) {
            return i;
        }
    }
    return -1;
}

void RewardState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        if (m_phase == Phase::Augment) {
            m_hovered = -1;
            for (int i = 0; i < static_cast<int>(m_augmentOffers.size()); ++i) {
                if (augmentPanel(i).contains(p)) m_hovered = i;
            }
        } else {
            m_hovered = pickAt(p);
        }
        m_skipHovered = (m_phase == Phase::Purge) && skipButton().contains(p);
        return;
    }

    // Escape and Enter both mean "I am done here", so a player who wants to
    // scrap nothing is never stuck hunting for the button.
    if (event.type == sf::Event::KeyPressed && m_phase == Phase::Purge &&
        (event.key.code == sf::Keyboard::Escape || event.key.code == sf::Keyboard::Enter)) {
        leaveToMap();
        return;
    }

    if (event.type != sf::Event::MouseButtonPressed ||
        event.mouseButton.button != sf::Mouse::Left) {
        return;
    }

    const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });

    if (m_phase == Phase::Offer) {
        const int picked = pickAt(p);
        if (picked < 0) return;

        g_run.winEncounter(m_offers[static_cast<size_t>(picked)]);

        // No scrap bay after the last fight: the deck it would edit is never
        // drawn from again.
        if (g_run.isComplete()) {
            m_stateManager.changeState(
                std::make_unique<RunOverState>(m_stateManager, m_font, true));
        } else {
            enterPurgePhase();
        }
        return;
    }

    if (m_phase == Phase::Augment) {
        for (int i = 0; i < static_cast<int>(m_augmentOffers.size()); ++i) {
            if (!augmentPanel(i).contains(p)) continue;
            g_run.takeAugment(m_augmentOffers[static_cast<size_t>(i)]);
            m_stateManager.changeState(std::make_unique<MapState>(m_stateManager, m_font));
            return;
        }
        return;
    }

    if (skipButton().contains(p)) { leaveToMap(); return; }

    const int picked = pickAt(p);
    if (picked < 0 || !g_run.canPurge()) return;
    g_run.purgeCardAt(static_cast<size_t>(picked));
    leaveToMap();
}

void RewardState::render(sf::RenderTarget& target) {
    sf::RectangleShape bg({ 1280.0f, 720.0f });
    bg.setFillColor(sf::Color(13, 11, 18));
    target.draw(bg);

    target.draw(m_heading);
    target.draw(m_subheading);

    if (m_phase == Phase::Augment) {
        renderAugments(target);
        drawLabel(target, "an augment is permanent - it applies to every fight that follows",
                  { 640.0f - 230.0f, 500.0f }, 12, sf::Color(128, 134, 146), 1.2f);
        return;
    }

    if (m_phase == Phase::Offer) {
        for (int i = 0; i < static_cast<int>(m_offers.size()); ++i) {
            const bool hovered = (i == m_hovered);
            sf::Vector2f centre = cardCentre(i);
            if (hovered) centre.y -= 12.0f;
            CardArt::drawCard(target, m_font, m_offers[static_cast<size_t>(i)], centre,
                              cardSize() * (hovered ? 1.05f : 1.0f), 0.0f, true, hovered);
        }

        std::stringstream footer;
        footer << "Deck: " << g_run.deckSize() << " cards        Commander: "
               << g_run.getCommanderHp() << " HP  (+" << RunState::kHealBetweenFights
               << " after this)";
        sf::Text text;
        text.setFont(Fonts::ui());
        text.setString(footer.str());
        text.setCharacterSize(15);
        text.setFillColor(sf::Color(160, 154, 148));
        TextUtils::centerBoth(text);
        text.setPosition(640.0f, 606.0f);
        target.draw(text);
        return;
    }

    const std::vector<CardData>& deck = g_run.getDeck();
    const bool purgeable = g_run.canPurge();

    for (int i = 0; i < static_cast<int>(deck.size()); ++i) {
        const bool hovered = purgeable && (i == m_hovered);
        sf::Vector2f centre = gridCentre(i, static_cast<int>(deck.size()));
        if (hovered) centre.y -= 8.0f;
        CardArt::drawCard(target, m_font, deck[static_cast<size_t>(i)], centre,
                          gridCardSize() * (hovered ? 1.10f : 1.0f), 0.0f, false, hovered);

        if (hovered) {
            // A red wash, so the click reads as destroy rather than select.
            const sf::Vector2f s = gridCardSize() * 1.10f;
            sf::RectangleShape veil(s);
            veil.setOrigin(s / 2.0f);
            veil.setPosition(centre);
            veil.setFillColor(sf::Color(190, 40, 44, 90));
            target.draw(veil);
        }
    }

    // The name of whatever is under the cursor, at a size the grid cannot show.
    if (purgeable && m_hovered >= 0 && m_hovered < static_cast<int>(deck.size())) {
        const CardData& card = deck[static_cast<size_t>(m_hovered)];
        sf::Text label;
        label.setFont(m_font);
        label.setString("Scrap  " + card.name);
        label.setCharacterSize(18);
        label.setStyle(sf::Text::Bold);
        label.setFillColor(sf::Color(228, 120, 118));
        TextUtils::centerBoth(label);
        label.setPosition(640.0f, 600.0f);
        target.draw(label);
    }

    const sf::FloatRect skip = skipButton();
    sf::RectangleShape button({ skip.width, skip.height });
    button.setPosition(skip.left, skip.top);
    button.setFillColor(m_skipHovered ? sf::Color(44, 40, 54) : sf::Color(26, 23, 32));
    button.setOutlineThickness(1.0f);
    button.setOutlineColor(m_skipHovered ? sf::Color(200, 190, 176) : sf::Color(96, 90, 104));
    target.draw(button);

    sf::Text skipLabel;
    skipLabel.setFont(m_font);
    skipLabel.setString(purgeable ? "Keep the deck as it is" : "Continue");
    skipLabel.setCharacterSize(17);
    skipLabel.setFillColor(m_skipHovered ? sf::Color(240, 236, 230) : sf::Color(186, 180, 172));
    TextUtils::centerBoth(skipLabel);
    skipLabel.setPosition(skip.left + skip.width / 2.0f, skip.top + skip.height / 2.0f);
    target.draw(skipLabel);

    std::stringstream footer;
    footer << "Deck: " << g_run.deckSize() << " cards   (floor "
           << RunState::kMinRunDeck << ")        Commander: " << g_run.getCommanderHp() << " HP";
    sf::Text text;
    text.setFont(Fonts::ui());
    text.setString(footer.str());
    text.setCharacterSize(14);
    text.setFillColor(sf::Color(140, 134, 130));
    TextUtils::centerBoth(text);
    text.setPosition(640.0f, 700.0f);
    target.draw(text);
}

// =============================================================================
// RunOverState implementation
// =============================================================================

RunOverState::RunOverState(StateManager& sm, const sf::Font& font, bool victory)
    : m_stateManager(sm), m_font(font), m_victory(victory) {
    const std::string bgPath = victory ? "assets/ui/victory_bg.png" : "assets/ui/defeat_bg.png";
    if (ResourceManager::exists(bgPath)) {
        const sf::Texture& bg = ResourceManager::get().getTexture(bgPath);
        coverScreen(m_background, bg);
        m_hasBackground = true;
    }

    m_heading.setFont(font);
    m_heading.setString(victory ? "THE LINE IS BROKEN" : "YOUR CORE IS DARK");
    m_heading.setCharacterSize(44);
    m_heading.setStyle(sf::Text::Bold);
    m_heading.setLetterSpacing(3.0f);
    m_heading.setFillColor(victory ? sf::Color(255, 220, 130) : sf::Color(214, 62, 66));
    TextUtils::centerBoth(m_heading);
    m_heading.setPosition(640.0f, 250.0f);

    std::stringstream summary;
    if (victory) {
        summary << "All five commanders have fallen. Your deck ended at "
                << g_run.deckSize() << " cards.";
    } else {
        summary << "You fell at commander " << g_run.getEncounterNumber() << " of "
                << RunState::kEncounters << ", holding a deck of " << g_run.deckSize() << " cards.";
    }
    m_summary.setFont(font);
    m_summary.setString(summary.str());
    m_summary.setCharacterSize(17);
    m_summary.setFillColor(sf::Color(206, 200, 192));
    TextUtils::centerBoth(m_summary);
    m_summary.setPosition(640.0f, 306.0f);

    m_againButton.setup(font, "NEW GAME", { 640.0f, 400.0f }, { 280.0f, 52.0f },
                        victory ? sf::Color(240, 200, 110) : sf::Color(186, 92, 92), 19);
}

void RunOverState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_againButton.setHovered(m_againButton.contains(p));
    } else if (event.type == sf::Event::MouseButtonPressed &&
               event.mouseButton.button == sf::Mouse::Left) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseButton.x, event.mouseButton.y });
        if (m_againButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<RoleSelectState>(m_stateManager, m_font));
        }
    }
}

void RunOverState::render(sf::RenderTarget& target) {
    if (m_hasBackground) {
        target.draw(m_background);
        sf::RectangleShape veil({ 1280.0f, 720.0f });
        veil.setFillColor(sf::Color(8, 6, 12, 120));
        target.draw(veil);
    } else {
        sf::RectangleShape bg({ 1280.0f, 720.0f });
        bg.setFillColor(sf::Color(10, 8, 14));
        target.draw(bg);
    }
    target.draw(m_heading);
    target.draw(m_summary);
    m_againButton.render(target);
}

// =============================================================================

std::unique_ptr<GameState> createInitialMenuState(StateManager& sm, const sf::Font& font) {
    return std::make_unique<IntroState>(sm, font);
}
