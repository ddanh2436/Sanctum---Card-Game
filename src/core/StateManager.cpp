#include "core/StateManager.hpp"
#include "battle/AICommander.hpp"
#include "battle/DuelEngine.hpp"
#include "rendering/CardArt.hpp"
#include "rendering/CardSpotlight.hpp"
#include "rendering/CombatVFX.hpp"
#include "rendering/MenuBackdrop.hpp"
#include "rendering/EndScreen.hpp"
#include "rendering/PortraitRig.hpp"
#include "rendering/FloatingText.hpp"
#include "rendering/HolyVFX.hpp"
#include "run/DeckBuilder.hpp"
#include "run/RunState.hpp"
#include "utils/AudioManager.hpp"
#include "utils/Avatars.hpp"
#include "utils/DataLoader.hpp"
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

void drawPanel(sf::RenderTarget& target, sf::FloatRect bounds, sf::Color fill, sf::Color outline) {
    sf::RectangleShape panel({ bounds.width, bounds.height });
    panel.setPosition(bounds.left, bounds.top);
    panel.setFillColor(fill);
    panel.setOutlineThickness(1.0f);
    panel.setOutlineColor(outline);
    target.draw(panel);
}

void drawLabel(sf::RenderTarget& target, const sf::Font& font, const std::string& text,
               sf::Vector2f pos, unsigned int size, sf::Color colour,
               float letterSpacing = 1.0f, bool bold = false) {
    sf::Text label;
    label.setFont(font);
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

// HQ cards, one per side, sitting behind that side's rows
// Squeezed from 68 to 50 to buy the rows their extra height. The card now
// carries a name, a bar and the number, and drops the second caption line.
constexpr float kHqW = 268.0f;
constexpr float kHqH = 50.0f;
constexpr float kEnemyHqY = 6.0f;
constexpr float kPlayerHqY = 492.0f;

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
    return { kCentreX - kHqW / 2.0f, y, kHqW, kHqH };
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
    return { 20.0f, 336.0f, 54.0f, 44.0f };
}

} // namespace Layout

// =============================================================================
// Forward declarations
// =============================================================================

class IntroState;
class MenuState;
class RoleSelectState;
class MapState;
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
                drawLabel(target, m_font, "A GAME BY DAO DUY ANH", { 640.0f, 250.0f }, 13,
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
            drawLabel(target, m_font, "[ PRESS SPACE TO COMMENCE ]", { 640.0f, 664.0f }, 16,
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
private:
    StateManager& m_stateManager;
    const sf::Font& m_font;

    MechRole m_primary = MechRole::Vanguard;
    MechRole m_secondary = MechRole::Siege;
    /// Which column the next click assigns to.
    bool m_pickingPrimary = true;

    std::array<sf::FloatRect, kMechRoleCount> m_tiles;
    int m_hovered = -1;

    UiButton m_confirmButton;
    UiButton m_backButton;

    void layoutTiles();
    void drawRoleTile(sf::RenderTarget& target, MechRole role, int slot) const;

public:
    RoleSelectState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float dt) override;
    void render(sf::RenderTarget& target) override;
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
    EndScreen m_endScreen;
    PortraitRig m_portraitRig;

    /// Phase 4 of the defeat sequence hands the player the decision instead of
    /// dumping them onto the run-over screen.
    UiButton m_retryButton;
    UiButton m_menuButton;
    float m_sparkClock = 0.0f;

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
    sf::Vector2f handCardSize() const { return { 106.0f, 146.0f }; }
    Unit* unitAt(sf::Vector2f point);
    sf::FloatRect rectOf(const Unit& unit) const;
    bool trapZoneHit(Side side, sf::Vector2f point) const;
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

    void renderRow(sf::RenderTarget& target, Side side, BoardLine line);
    void renderCommanderPanel(sf::RenderTarget& target, Side side);
    void renderPiles(sf::RenderTarget& target, Side side);
    void renderEnemyHand(sf::RenderTarget& target);
    void renderLogButton(sf::RenderTarget& target);
    void renderLogPanel(sf::RenderTarget& target);
    void drawCardBack(sf::RenderTarget& target, sf::FloatRect box, Side owner,
                      sf::Color accent, float alpha) const;
    void renderHq(sf::RenderTarget& target, Side side);
    void renderFrontLine(sf::RenderTarget& target);
    void renderTraps(sf::RenderTarget& target, Side side);
    void renderHand(sf::RenderTarget& target);
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
    enum class Phase { Offer, Purge };

    StateManager& m_stateManager;
    const sf::Font& m_font;
    std::vector<CardData> m_offers;
    Phase m_phase = Phase::Offer;
    int m_hovered = -1;
    bool m_skipHovered = false;
    sf::Text m_heading;
    sf::Text m_subheading;

public:
    RewardState(StateManager& sm, const sf::Font& font);
    void handleEvent(const sf::Event& event, const sf::RenderWindow& window) override;
    void update(float) override {}
    void render(sf::RenderTarget& target) override;

private:
    void enterPurgePhase();
    void leaveToMap();
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
    UiButton m_backButton;
    UiButton m_defaultsButton;
    int m_hoveredSlider = -1;
    int m_hoveredToggle = -1;

    static constexpr float kPanelX = 350.0f;
    static constexpr float kPanelY = 60.0f;
    static constexpr float kPanelW = 580.0f;
    static constexpr float kPanelH = 620.0f;

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

        drawLabel(target, m_font, slider.label,
                  { slider.track.left, slider.track.top - 26.0f }, 15,
                  active ? sf::Color(240, 232, 214) : sf::Color(196, 190, 180));

        sf::Text readout;
        readout.setFont(m_font);
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

        drawLabel(target, m_font, toggle.label,
                  { toggle.box.left + toggle.box.width + 18.0f, toggle.box.top + 4.0f }, 15,
                  active ? sf::Color(240, 232, 214) : sf::Color(196, 190, 180));

        drawLabel(target, m_font, on ? "ON" : "OFF",
                  { kPanelX + kPanelW - 74.0f, toggle.box.top + 5.0f }, 13,
                  on ? sf::Color(214, 180, 116) : sf::Color(120, 114, 126), 2.0f);
    }

    m_defaultsButton.render(target);
    m_backButton.render(target);

    drawLabel(target, m_font, "F11 toggles fullscreen anywhere  -  Esc closes this panel",
              { kPanelX + 34.0f, kPanelY + kPanelH - 102.0f }, 12, sf::Color(126, 120, 132));
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

            drawLabel(target, m_font, stat.label, { sx - 30.0f, stripY + 34.0f }, 11,
                      sf::Color(stat.ink.r, stat.ink.g, stat.ink.b, 210), 2.0f);
            sx += pitch;
        }

        // Rules glossary down the left. Measure first so the panel hugs its
        // contents instead of leaving a slab of empty board below them.
        auto measure = [&](const std::vector<CardArt::GlossaryEntry>& list) {
            float h = 0.0f;
            for (const CardArt::GlossaryEntry& entry : list) {
                sf::Text probe;
                probe.setFont(m_font);
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

            drawLabel(target, m_font, heading, { panel.left + 26.0f, y }, 12,
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
                body.setFont(m_font);
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
            drawLabel(target, m_font, "A plain card with no special rules.",
                      { panel.left + 26.0f, panel.top + 66.0f }, 15, sf::Color(160, 154, 148));
        }

        m_closeButton.render(target);
        drawLabel(target, m_font, "click anywhere to close",
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
    TextUtils::centerBoth(m_titleText);
    m_titleText.setPosition(kColumnX, 178.0f);

    m_subtitleText.setFont(font);
    m_subtitleText.setString("MECHA-CHIVALRY");
    m_subtitleText.setCharacterSize(22);
    m_subtitleText.setLetterSpacing(7.0f);
    m_subtitleText.setFillColor(sf::Color(240, 96, 100));
    TextUtils::centerBoth(m_subtitleText);
    m_subtitleText.setPosition(kColumnX, 234.0f);

    m_titleRule.setSize({ 300.0f, 1.0f });
    m_titleRule.setOrigin(150.0f, 0.5f);
    m_titleRule.setPosition(kColumnX, 268.0f);
    m_titleRule.setFillColor(sf::Color(238, 92, 96, 220));

    const char* tagline[] = {
        "Two doctrines, one reactor. Command the line.",
        "Hold the frontline, set your traps,",
        "and tribute your own frames to call down a titan."
    };
    float y = 306.0f;
    for (const char* line : tagline) {
        sf::Text text;
        text.setFont(font);
        text.setString(line);
        text.setCharacterSize(15);
        text.setFillColor(sf::Color(176, 198, 216));
        TextUtils::centerBoth(text);
        text.setPosition(kColumnX, y);
        m_taglineLines.push_back(text);
        y += 24.0f;
    }

    m_startButton.setup(font, "NEW GAME", { kColumnX, 424.0f },
                        { 320.0f, 54.0f }, sf::Color(214, 60, 66), 20);
    m_settingsButton.setup(font, "SETTINGS", { kColumnX, 486.0f },
                           { 320.0f, 46.0f }, sf::Color(186, 172, 140), 17);
    m_quitButton.setup(font, "DEPART", { kColumnX, 544.0f },
                       { 320.0f, 46.0f }, sf::Color(150, 142, 130), 17);

    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMenu);
    layoutAvatars();

    m_footerText.setFont(font);
    m_footerText.setString("Drag cards to summon  |  drag a unit onto a foe to attack  |  Space ends your turn");
    m_footerText.setCharacterSize(13);
    m_footerText.setFillColor(sf::Color(150, 172, 192));
    m_footerText.setPosition(28.0f, 686.0f);
}

void MenuState::layoutAvatars() {
    m_avatarSlots.clear();
    const auto& pool = Avatars::pool();
    if (pool.empty()) return;

    // The row is centred on the menu column and shrinks rather than running off
    // it, so dropping a tenth portrait into assets/avatars/ does not push the
    // last one off the screen.
    const int count = static_cast<int>(pool.size());
    const float width = 328.0f;
    const float gap = 9.0f;
    float size = (width - (count - 1) * gap) / static_cast<float>(count);
    size = std::clamp(size, 24.0f, 48.0f);

    const float span = count * size + (count - 1) * gap;
    const float startX = kColumnX - span / 2.0f;
    for (int i = 0; i < count; ++i) {
        m_avatarSlots.push_back({ startX + i * (size + gap), 600.0f, size, size });
    }
}

void MenuState::renderAvatars(sf::RenderTarget& target) const {
    drawLabel(target, m_font, "COMMANDER PORTRAIT", { kColumnX - 160.0f, 578.0f }, 11,
              sf::Color(158, 150, 138), 3.0f);

    const auto& pool = Avatars::pool();
    if (pool.empty()) {
        drawLabel(target, m_font, "Drop images into assets/avatars/ to choose a face",
                  { kColumnX - 160.0f, 602.0f }, 12, sf::Color(112, 106, 112));
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
            sf::RectangleShape ring({ box.width + 6.0f, box.height + 6.0f });
            ring.setPosition(box.left - 3.0f, box.top - 3.0f);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(2.0f);
            ring.setOutlineColor(sf::Color(236, 190, 74));
            target.draw(ring);
        }
        if (hot) {
            drawLabel(target, m_font, Avatars::label(pool[i]),
                      { box.left - 4.0f, box.top - 16.0f }, 10,
                      sf::Color(236, 214, 178), 1.2f);
        }
    }

    // Under the thumbnails but clear of the footer hint at y=686. The first
    // version put this at 686 exactly and the two lines printed over each other.
    drawLabel(target, m_font,
              picked.empty() ? "following your primary core" : "click again to follow your core",
              { kColumnX - 160.0f, 600.0f + m_avatarSlots.front().height + 8.0f }, 10,
              sf::Color(122, 116, 122), 1.4f);
}

void MenuState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
        m_startButton.setHovered(m_startButton.contains(p));
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
    m_settingsButton.render(target);
    m_quitButton.render(target);
    renderAvatars(target);
    target.draw(m_footerText);
}

// =============================================================================
// RoleSelectState implementation
// =============================================================================

RoleSelectState::RoleSelectState(StateManager& sm, const sf::Font& font)
    : m_stateManager(sm), m_font(font) {
    layoutTiles();
    m_confirmButton.setup(font, "COMMIT LOADOUT", { 640.0f, 660.0f },
                          { 320.0f, 48.0f }, sf::Color(236, 190, 74), 18);
    m_backButton.setup(font, "BACK", { 130.0f, 44.0f },
                       { 160.0f, 38.0f }, sf::Color(160, 150, 136), 14);
    AudioManager::get().playMusicCue(AudioManager::Cue::MusicMenu);
}

void RoleSelectState::layoutTiles() {
    // Two rows of three, centred in the 1280x720 design space.
    constexpr float kTileW = 340.0f;
    constexpr float kTileH = 168.0f;
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

void RoleSelectState::handleEvent(const sf::Event& event, const sf::RenderWindow& window) {
    if (event.type == sf::Event::MouseMoved) {
        const sf::Vector2f p = window.mapPixelToCoords({ event.mouseMove.x, event.mouseMove.y });
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
        }

        if (m_confirmButton.contains(p)) {
            g_run.startNewRun(m_primary, m_secondary);
            m_stateManager.changeState(std::make_unique<MapState>(m_stateManager, m_font));
        } else if (m_backButton.contains(p)) {
            m_stateManager.changeState(std::make_unique<MenuState>(m_stateManager, m_font));
        }
    } else if (event.type == sf::Event::KeyPressed) {
        if (event.key.code == sf::Keyboard::Tab) {
            m_pickingPrimary = !m_pickingPrimary;
        } else if (event.key.code == sf::Keyboard::Enter) {
            g_run.startNewRun(m_primary, m_secondary);
            m_stateManager.changeState(std::make_unique<MapState>(m_stateManager, m_font));
        }
    }
}

void RoleSelectState::update(float) {}

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

    drawPanel(target, bounds, fill, outline);

    // A thicker bar down the left edge reads as the doctrine colour at a glance.
    sf::RectangleShape stripe({ 5.0f, bounds.height - 2.0f });
    stripe.setPosition(bounds.left + 1.0f, bounds.top + 1.0f);
    stripe.setFillColor(blocked ? sf::Color(70, 70, 74) : accent);
    target.draw(stripe);

    const sf::Color textColour = blocked ? sf::Color(120, 118, 116) : sf::Color(238, 232, 222);
    const float x = bounds.left + 22.0f;

    drawLabel(target, m_font, displayName(role), { x, bounds.top + 16.0f }, 21,
              blocked ? sf::Color(120, 118, 116) : accent, 2.0f, true);
    drawLabel(target, m_font, roleTitle(role), { x, bounds.top + 46.0f }, 13,
              sf::Color(158, 156, 154));

    drawLabel(target, m_font, rolePassiveName(role), { x, bounds.top + 78.0f }, 14,
              textColour, 1.0f, true);

    // The passive text is one long sentence; wrap it by hand to the tile width.
    const std::string passive = rolePassiveText(role);
    std::string line;
    float y = bounds.top + 100.0f;
    std::istringstream words(passive);
    std::string word;
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (candidate.size() > 44) {
            drawLabel(target, m_font, line, { x, y }, 12, sf::Color(176, 172, 168));
            y += 17.0f;
            line = word;
        } else {
            line = candidate;
        }
    }
    if (!line.empty()) drawLabel(target, m_font, line, { x, y }, 12, sf::Color(176, 172, 168));

    // Slot badge
    if (isPrimary || isSecondary) {
        const char* tag = isPrimary ? "PRIMARY" : "SECONDARY";
        sf::FloatRect badge { bounds.left + bounds.width - 106.0f, bounds.top + 12.0f, 94.0f, 22.0f };
        drawPanel(target, badge, sf::Color(14, 14, 18, 240), accent);
        drawLabel(target, m_font, tag, { badge.left + 9.0f, badge.top + 4.0f }, 11, accent, 1.4f);
    }
}

void RoleSelectState::render(sf::RenderTarget& target) {
    sf::RectangleShape backdrop({ 1280.0f, 720.0f });
    backdrop.setFillColor(sf::Color(12, 13, 17));
    target.draw(backdrop);

    drawLabel(target, m_font, "DUAL-CORE PROTOCOL", { 640.0f - 150.0f, 54.0f }, 26,
              sf::Color(236, 214, 178), 4.0f, true);

    std::ostringstream brief;
    brief << "Choose a primary core (" << DeckRules::kMinPrimary << "+ cards, may field its Titan)"
          << "  and a secondary splash (up to " << DeckRules::kMaxSecondary
          << " cards, no Titan).  " << DeckRules::kDeckSize << " cards total.";
    drawLabel(target, m_font, brief.str(), { 210.0f, 94.0f }, 13, sf::Color(150, 148, 146));

    drawLabel(target, m_font,
              m_pickingPrimary ? "> SELECTING PRIMARY CORE" : "> SELECTING SECONDARY CORE",
              { 210.0f, 118.0f }, 13, sf::Color(236, 190, 74), 2.0f, true);
    drawLabel(target, m_font, "TAB switches slot", { 960.0f, 118.0f }, 12,
              sf::Color(120, 118, 116));

    for (int i = 0; i < kMechRoleCount; ++i) {
        drawRoleTile(target, static_cast<MechRole>(i), i);
    }
    // The loadout line, so the commitment is legible before it is made.
    const DeckConfiguration preview = DeckBuilder::build(m_primary, m_secondary);
    std::ostringstream summary;
    summary << displayName(m_primary) << " " << preview.countFor(m_primary)
            << "   /   " << displayName(m_secondary) << " " << preview.countFor(m_secondary);
    if (!preview.isValidDeck()) summary << "   -   " << toString(preview.validate());

    sf::Text line;
    line.setFont(m_font);
    line.setString(summary.str());
    line.setCharacterSize(16);
    line.setLetterSpacing(1.6f);
    line.setFillColor(preview.isValidDeck() ? sf::Color(214, 208, 198) : sf::Color(214, 120, 110));
    TextUtils::centerBoth(line);
    line.setPosition(640.0f, 626.0f);
    target.draw(line);

    m_confirmButton.render(target);
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
    text.setFont(m_font);
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
        name.setFont(m_font);
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

    drawLabel(target, m_font, "YOUR COMMANDER", { panel.left + 24.0f, panel.top + 18.0f }, 12,
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

    drawLabel(target, m_font,
              std::string(displayName(g_run.getPrimaryRole())) + "  /  "
                  + displayName(g_run.getSecondaryRole()),
              { textX, panel.top + 78.0f }, 14, sf::Color(168, 176, 192), 1.6f);

    const int hp = g_run.getCommanderHp();
    drawGauge(target, { textX, panel.top + 104.0f, 300.0f, 22.0f },
              static_cast<float>(hp) / static_cast<float>(Commander::kStartingHp),
              sf::Color(74, 132, 104, 235),
              "REACTOR  " + std::to_string(hp) + " / " + std::to_string(Commander::kStartingHp));

    // --- deck composition, as a stacked bar plus chips ------------------------
    drawLabel(target, m_font, "DECK", { panel.left + 24.0f, panel.top + 164.0f }, 12,
              sf::Color(150, 162, 182), 3.0f);
    drawLabel(target, m_font, std::to_string(g_run.deckSize()) + " CARDS",
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
        label.setFont(m_font);
        label.setString(std::to_string(slice.count) + "  " + slice.label);
        label.setCharacterSize(12);
        label.setLetterSpacing(1.4f);
        label.setFillColor(sf::Color(186, 182, 176));
        label.setPosition(chipX + 16.0f, barY + 22.0f);
        target.draw(label);
        chipX += 156.0f;
    }

    drawLabel(target, m_font,
              "Scrap one card after every win - the deck you finish with is the one you built.",
              { panel.left + 24.0f, panel.top + 258.0f }, 11, sf::Color(120, 124, 136), 1.0f);
}

void MapState::renderNextPanel(sf::RenderTarget& target) const {
    const Encounter& next = g_run.currentEncounter();
    const sf::FloatRect panel(608.0f, 288.0f, 576.0f, 316.0f);
    drawPanel(target, panel, sf::Color(22, 15, 18, 240), sf::Color(126, 74, 62, 210));

    drawLabel(target, m_font,
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
    line.setFont(m_font);
    line.setString(next.subtitle);
    line.setCharacterSize(13);
    line.setFillColor(sf::Color(172, 164, 158));
    line.setPosition(textX, panel.top + 78.0f);
    target.draw(line);

    drawGauge(target, { textX, panel.top + 104.0f, 300.0f, 22.0f }, 1.0f,
              sf::Color(150, 58, 54, 235),
              "REACTOR  " + std::to_string(next.commanderHp));

    // --- what you are walking into, as facts rather than a sentence ----------
    drawLabel(target, m_font, "THREAT", { panel.left + 24.0f, panel.top + 164.0f }, 12,
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
        drawLabel(target, m_font, fact.key, { panel.left + 24.0f, y }, 11,
                  sf::Color(132, 118, 116), 2.0f);
        sf::Text value;
        value.setFont(m_font);
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

    drawLabel(target, m_font, "THE CAMPAIGN", { 96.0f, 46.0f }, 26,
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
    you.name = roleTitle(you.primary);

    DuelistSetup foe;
    foe.deck = g_run.buildOpponentDeck();
    foe.primary = encounter.primary;
    foe.secondary = encounter.secondary;
    foe.hp = encounter.commanderHp;
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
    if (index == m_hoverCardIndex) y -= 52.0f;
    return { 640.0f + offset * spacing, y };
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
        if (Layout::logButton().contains(p)) { m_logOpen = !m_logOpen; return; }
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

void DuelState::consumeEvents() {
    for (const DuelEvent& event : m_duel.drainEvents()) {
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
            m_combat.shockwave(at, accentOf(event.instanceId), 96.0f, 0.62f);
            m_combat.flash(event.instanceId, sf::Color(255, 255, 255, 140), 0.4f);
            m_combat.slam(event.instanceId);
            m_combat.smoke(at, 14);
            m_combat.dropMarker(at, accentOf(event.instanceId));

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
    m_floating.update(dt);
    m_vfx.update(dt);
    m_combat.update(dt);
    m_spotlight.update(dt);
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
    m_endScreen.update(dt);
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

    // Portrait fills the left end, the way a real HQ card carries its crest.
    const sf::FloatRect portraitBox(box.left + 4.0f, box.top + 4.0f, 42.0f, box.height - 8.0f);
    CardArt::drawAvatar(target, isEnemy ? m_enemyArtPath : m_playerArtPath,
                        portraitBox, accent, 0.10f);

    // Name plate over the remaining width
    const float textLeft = portraitBox.left + portraitBox.width + 8.0f;
    sf::Text name;
    name.setFont(m_font);
    name.setString(cmd.getName());
    name.setCharacterSize(13);
    name.setStyle(sf::Text::Bold);
    name.setLetterSpacing(1.2f);
    name.setFillColor(sf::Color(238, 228, 212));
    while (name.getCharacterSize() > 9 &&
           name.getLocalBounds().width > box.width - portraitBox.width - 80.0f) {
        name.setCharacterSize(name.getCharacterSize() - 1);
    }
    name.setPosition(textLeft, box.top + 4.0f);
    target.draw(name);

    drawLabel(target, m_font, "REACTOR CORE", { textLeft, box.top + 20.0f }, 8,
              sf::Color(accent.r, accent.g, accent.b, 200), 2.2f);

    // Life bar under the name so damage reads at a glance
    const float ratio = static_cast<float>(cmd.getHp()) /
                        static_cast<float>(std::max(1, cmd.getMaxHp()));
    drawBar(target, { textLeft, box.top + 33.0f, box.width - portraitBox.width - 84.0f, 10.0f },
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
        drawLabel(target, m_font, "STRIKE THE CORE",
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
    drawLabel(target, m_font, cores,
              { panel.left + 72.0f, panel.top + 26.0f }, 10,
              sf::Color(accent.r, accent.g, accent.b, 210), 2.0f);

    // Energy as a single stamped badge, the way Kredits read in KARDS.
    const sf::FloatRect badge(panel.left + 72.0f, panel.top + 42.0f, 54.0f, 28.0f);
    drawPanel(target, badge, sf::Color(28, 22, 14, 245), sf::Color(226, 158, 44));

    sf::Text mana;
    mana.setFont(m_font);
    mana.setString(std::to_string(cmd.getMana()));
    mana.setCharacterSize(18);
    mana.setStyle(sf::Text::Bold);
    mana.setFillColor(sf::Color(248, 196, 70));
    TextUtils::centerBoth(mana);
    mana.setPosition(badge.left + 17.0f, badge.top + 14.0f);
    target.draw(mana);

    sf::RectangleShape slash({ 1.0f, 18.0f });
    slash.setPosition(badge.left + 32.0f, badge.top + 5.0f);
    slash.setFillColor(sf::Color(150, 120, 60));
    target.draw(slash);

    drawLabel(target, m_font, std::to_string(cmd.getManaCap()),
              { badge.left + 38.0f, badge.top + 6.0f }, 13, sf::Color(196, 164, 96));

    // The overcharge core sits beside the energy badge. A Paladin always shows
    // it, empty or not, because a resource that only appears once it has
    // something in it is a resource the player never learns they have.
    if (cmd.getOvercharge() > 0 || cmd.getPrimaryRole() == MechRole::Paladin) {
        const sf::FloatRect core(badge.left + 60.0f, badge.top, 54.0f, 28.0f);
        drawPanel(target, core, sf::Color(14, 24, 30, 245), sf::Color(96, 206, 226));
        sf::Text charge;
        charge.setFont(m_font);
        charge.setString(std::to_string(cmd.getOvercharge()));
        charge.setCharacterSize(18);
        charge.setStyle(sf::Text::Bold);
        charge.setFillColor(sf::Color(126, 226, 244));
        TextUtils::centerBoth(charge);
        charge.setPosition(core.left + 17.0f, core.top + 14.0f);
        target.draw(charge);
        drawLabel(target, m_font, "OC", { core.left + 32.0f, core.top + 7.0f }, 11,
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
        drawLabel(target, m_font, pile.label,
                  { pile.box.left + 1.0f, pile.box.top + pile.box.height + 3.0f }, 9,
                  hot ? sf::Color(226, 214, 196) : sf::Color(126, 120, 128), 1.8f);
        if (hot) {
            // Spelling it out on hover, since the pile itself only ever shows
            // a bare number.
            const std::string detail = std::string(pile.label) + ": "
                                     + std::to_string(pile.count) + " cards";
            drawLabel(target, m_font, detail,
                      { pile.box.left, pile.box.top - 16.0f }, 11,
                      sf::Color(238, 226, 206), 1.2f);
        }
    }
}

void DuelState::renderLogButton(sf::RenderTarget& target) {
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
        drawLabel(target, m_font, "LOG",
                  { box.left + box.width + 8.0f, box.top + 14.0f }, 11,
                  sf::Color(210, 202, 194), 2.0f);
    }
}

void DuelState::renderLogPanel(sf::RenderTarget& target) {
    if (!m_logOpen) return;

    // Stops at 526 so it never covers the player strip that starts at 552.
    const sf::FloatRect panel(84.0f, 132.0f, 330.0f, 394.0f);
    drawPanel(target, panel, sf::Color(12, 11, 16, 246), sf::Color(150, 128, 84, 220));

    drawLabel(target, m_font, "BATTLE LOG",
              { panel.left + 14.0f, panel.top + 12.0f }, 13,
              sf::Color(236, 210, 150), 2.6f, true);

    sf::RectangleShape rule({ panel.width - 28.0f, 1.0f });
    rule.setPosition(panel.left + 14.0f, panel.top + 34.0f);
    rule.setFillColor(sf::Color(120, 104, 70, 200));
    target.draw(rule);

    // Newest last, oldest trimmed - the tail is what a player just missed.
    sf::Text body;
    body.setFont(m_font);
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

    drawLabel(target, m_font, "CLICK THE BARS TO CLOSE",
              { panel.left + 14.0f, panel.top + panel.height - 22.0f }, 9,
              sf::Color(118, 112, 106), 1.8f);
}

/// The enemy hand, face-down along the top edge.
void DuelState::renderEnemyHand(sf::RenderTarget& target) {
    const int count = static_cast<int>(m_duel.commander(Side::Opponent).getHand().size());
    const sf::FloatRect area = Layout::enemyHandFan();
    if (count <= 0) {
        drawLabel(target, m_font, "NO CARDS",
                  { area.left + 60.0f, area.top + 24.0f }, 11,
                  sf::Color(120, 110, 124), 2.0f);
        return;
    }

    const float cardW = 42.0f;
    const float cardH = 60.0f;
    // Cards tighten up as the grip grows rather than running off the screen.
    const float step = count > 1
        ? std::min(24.0f, (area.width - cardW) / static_cast<float>(count - 1))
        : 0.0f;
    const float span = cardW + step * static_cast<float>(count - 1);
    const float startX = area.left + (area.width - span) / 2.0f;

    for (int i = 0; i < count; ++i) {
        const sf::FloatRect box(startX + step * static_cast<float>(i),
                                area.top + 2.0f, cardW, cardH);
        drawCardBack(target, box, Side::Opponent, sf::Color(198, 120, 198), 1.0f);
    }

    sf::Text n;
    n.setFont(m_font);
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
        // Empty trap cells only show while you are holding a trap.
        if (!trap && !draggingTrap) continue;
        const bool highlighted = draggingTrap && !trap && Layout::trapZone(side).contains(m_mousePos);
        CardArt::drawTrapSlot(target, m_font, trap, box, side,
                              side == Side::Player, highlighted);
    }

    if (!traps.empty() || draggingTrap) {
        drawLabel(target, m_font, side == Side::Player ? "SET" : "ENEMY SET",
                  { Layout::kTrapX, Layout::trapZone(side).top - 14.0f }, 9,
                  sf::Color(126, 112, 90), 2.2f);
    }
}

void DuelState::renderHand(sf::RenderTarget& target) {
    const auto& hand = m_duel.commander(Side::Player).getHand();
    const int count = static_cast<int>(hand.size());
    const sf::Vector2f size = handCardSize();

    // Draw left to right so later cards overlap earlier ones, then lift the
    // hovered card clear of the stack.
    for (int i = 0; i < count; ++i) {
        if (i == m_dragCardIndex || i == m_hoverCardIndex) continue;
        const CardData& card = hand[static_cast<size_t>(i)];
        CardArt::drawCard(target, m_font, card, handCardCentre(i, count), size, 0.0f,
                          isPlayerTurn() && canPlayCard(card), false);
    }
    if (m_hoverCardIndex >= 0 && m_hoverCardIndex < count && m_hoverCardIndex != m_dragCardIndex) {
        const CardData& card = hand[static_cast<size_t>(m_hoverCardIndex)];
        CardArt::drawCard(target, m_font, card, handCardCentre(m_hoverCardIndex, count),
                          size * 1.45f, 0.0f, isPlayerTurn() && canPlayCard(card), true);
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
    drawLabel(target, m_font, turn.str(), { 300.0f, 14.0f }, 15,
              sf::Color(198, 186, 166), 2.6f, true);
    drawLabel(target, m_font,
              m_duel.activeSide() == Side::Player ? "YOUR MOVE" : "ENEMY MOVES",
              { 300.0f, 36.0f }, 13,
              m_duel.activeSide() == Side::Player ? sf::Color(240, 208, 128)
                                                  : sf::Color(198, 128, 198), 2.0f);

    m_endTurnButton.render(target);

    renderLogPanel(target);

    renderHand(target);
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

        drawLabel(target, m_font, "right-click to cancel", { 560.0f, 546.0f }, 12,
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
    m_endScreen.renderScrim(target);
    m_portraitRig.render(target);

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

void RewardState::leaveToMap() {
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
        m_hovered = pickAt(p);
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
        text.setFont(m_font);
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
    text.setFont(m_font);
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
