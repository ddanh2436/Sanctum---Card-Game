// Renders combat effects off-screen and reads the pixels back.
//
// Driving the real game to catch a 1.3 second effect turned out to be a poor
// way to check it: the shot lands early, or late, or the hand that turn holds no
// counter at all. Rendering it to a texture and looking at the result answers
// the same question in milliseconds and keeps answering it.

#include "TestAssert.hpp"
#include "rendering/CardArt.hpp"
#include "rendering/CombatVFX.hpp"
#include "utils/Rng.hpp"
#include "rendering/DeploySignature.hpp"
#include "rendering/DrawFlight.hpp"

#include <SFML/Graphics/RenderTexture.hpp>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace {

constexpr unsigned kW = 220;
constexpr unsigned kH = 240;
const sf::FloatRect kBox(60.0f, 70.0f, 68.0f, 94.0f);

/// The brightest pixel found anywhere in the image, and where it is.
sf::Color brightest(const sf::Image& image, unsigned& outX, unsigned& outY) {
    sf::Color best(0, 0, 0);
    int bestSum = -1;
    for (unsigned y = 0; y < image.getSize().y; ++y) {
        for (unsigned x = 0; x < image.getSize().x; ++x) {
            const sf::Color c = image.getPixel(x, y);
            const int sum = int(c.r) + int(c.g) + int(c.b);
            if (sum > bestSum) { bestSum = sum; best = c; outX = x; outY = y; }
        }
    }
    return best;
}

int litPixels(const sf::Image& image, int threshold) {
    int count = 0;
    for (unsigned y = 0; y < image.getSize().y; ++y) {
        for (unsigned x = 0; x < image.getSize().x; ++x) {
            const sf::Color c = image.getPixel(x, y);
            if (int(c.r) + int(c.g) + int(c.b) > threshold) ++count;
        }
    }
    return count;
}

/// Run the flare forward by `seconds` and hand back what it drew.
bool renderFlareAt(float seconds, sf::Image& out) {
    sf::RenderTexture canvas;
    if (!canvas.create(kW, kH)) return false;

    CombatVFX vfx;
    vfx.armFlare(kBox, CardArt::armedBorder());

    // Step in small slices so the particle motion matches a real frame rate.
    const float step = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += step) vfx.update(step);

    canvas.clear(sf::Color(10, 9, 12));
    vfx.renderAbove(canvas);
    canvas.display();
    out = canvas.getTexture().copyToImage();
    return true;
}

void test_flare_draws_a_border_in_the_armed_colour() {
    sf::Image image;
    if (!renderFlareAt(0.02f, image)) {
        std::cout << "  [SKIP] no render target available in this environment\n";
        return;
    }

    // Uncomment to eyeball the frame the numbers below are describing:
    //   image.saveToFile("vfx_arm_flare.png");

    unsigned x = 0, y = 0;
    const sf::Color peak = brightest(image, x, y);
    std::cout << "  brightest pixel " << int(peak.r) << "," << int(peak.g) << "," << int(peak.b)
              << " at (" << x << "," << y << ")\n";

    CHECK_MSG(int(peak.r) + int(peak.g) + int(peak.b) > 200,
              "the arming flare drew nothing bright at all");

    // The armed colour has to be present, but it is NOT the brightest thing on
    // screen and must not be asserted as such: the sparks are deliberately a
    // near-white so they read as heat coming off the frame, and they win the
    // peak every time. What matters is that plenty of purple-leaning pixels
    // exist - that is the border itself.
    const sf::Color armed = CardArt::armedBorder();
    int purple = 0;
    for (unsigned py = 0; py < image.getSize().y; ++py) {
        for (unsigned px = 0; px < image.getSize().x; ++px) {
            const sf::Color c = image.getPixel(px, py);
            const bool leansArmed = int(c.b) > int(c.g) + 12 && int(c.r) > int(c.g) + 4;
            if (leansArmed && int(c.r) + int(c.g) + int(c.b) > 120) ++purple;
        }
    }
    std::cout << "  armed colour is " << int(armed.r) << "," << int(armed.g) << ","
              << int(armed.b) << " - pixels leaning that way: " << purple << "\n";
    CHECK_MSG(purple > 120, "the flare is not drawing a border in the armed colour");

    // And it belongs around the card, not in the middle of it.
    const float cx = kBox.left + kBox.width / 2.0f;
    const float cy = kBox.top + kBox.height / 2.0f;
    const float dx = std::abs(float(x) - cx);
    const float dy = std::abs(float(y) - cy);
    CHECK_MSG(dx > kBox.width / 2.0f - 4.0f || dy > kBox.height / 2.0f - 4.0f,
              "the flare is drawing over the card instead of around its border");
    std::cout << "[PASS] test_flare_draws_a_border_in_the_armed_colour\n";
}

void test_flare_burns_off_and_disappears() {
    sf::Image early, mid, late, after;
    if (!renderFlareAt(0.02f, early)) {
        std::cout << "  [SKIP] no render target available in this environment\n";
        return;
    }
    renderFlareAt(0.60f, mid);
    renderFlareAt(1.20f, late);
    renderFlareAt(1.60f, after);   // past maxLife

    const int a = litPixels(early, 200);
    const int b = litPixels(mid, 200);
    const int c = litPixels(late, 200);
    const int d = litPixels(after, 200);
    std::cout << "  lit pixels  0.02s=" << a << "  0.60s=" << b
              << "  1.20s=" << c << "  1.60s=" << d << "\n";

    CHECK_MSG(a > 0, "nothing is lit at the start of the flare");
    CHECK_MSG(a > c, "the flare is not fading - it should burn off, not sit there");
    CHECK_MSG(d == 0, "the flare is still drawing after its lifetime has run out");
    std::cout << "[PASS] test_flare_burns_off_and_disappears\n";
}

void test_flare_holds_long_enough_to_see() {
    sf::Image quarter;
    if (!renderFlareAt(0.25f, quarter)) {
        std::cout << "  [SKIP] no render target available in this environment\n";
        return;
    }
    // The first version spent nearly all its brightness inside two frames. A
    // quarter of a second in, the effect must still clearly be happening.
    const int lit = litPixels(quarter, 200);
    std::cout << "  lit pixels at 0.25s: " << lit << "\n";
    CHECK_MSG(lit > 40, "the flare is over before a player could notice it");
    std::cout << "[PASS] test_flare_holds_long_enough_to_see\n";
}


// ---------------------------------------------------------------------------
// DrawFlight
//
// Geometry and a clock, so this is checked by reading numbers rather than
// pixels. Catching it in the running game is the wrong tool twice over: the
// deal is over about a second after the duel opens, and the state change that
// starts the duel takes long enough to swallow the whole animation before the
// first screenshot lands.
// ---------------------------------------------------------------------------

CardData sampleCard() {
    CardData card;
    card.id = "vg_test";
    card.name = "Test Frame";
    return card;
}

const sf::Vector2f kPile(43.0f, 670.0f);     // the player's deck stack
const sf::Vector2f kSlot(640.0f, 640.0f);    // a hand slot
const sf::Vector2f kSize(106.0f, 146.0f);

/// Step a flight to `seconds` in small slices, the way a frame loop would.
void runTo(DrawFlight& flight, float seconds) {
    for (float t = 0.0f; t < seconds; t += 0.01f) flight.update(0.01f);
}

void test_flight_leaves_the_pile_and_reaches_the_slot() {
    DrawFlight flight;
    flight.launch(sampleCard(), Side::Player, 3, kPile, kSlot, kSize);

    auto frames = flight.frames();
    CHECK_MSG(frames.size() == 1, "the card did not launch");
    const sf::Vector2f start = frames[0].centre;
    CHECK_MSG(std::abs(start.x - kPile.x) < 1.0f && std::abs(start.y - kPile.y) < 1.0f,
              "the card does not start on the draw pile");

    // Just before it settles it should be sitting on its hand slot.
    runTo(flight, 0.58f);
    frames = flight.frames();
    CHECK_MSG(!frames.empty(), "the card vanished before it landed");
    const sf::Vector2f end = frames[0].centre;
    std::cout << "  start (" << start.x << "," << start.y << ")  end ("
              << end.x << "," << end.y << ")\n";
    CHECK_MSG(std::abs(end.x - kSlot.x) < 1.0f && std::abs(end.y - kSlot.y) < 1.0f,
              "the card does not finish on the hand slot it was aimed at");

    runTo(flight, 0.10f);
    CHECK_MSG(!flight.busy(), "the flight never finished");
    std::cout << "[PASS] test_flight_leaves_the_pile_and_reaches_the_slot\n";
}

void test_flight_arcs_above_both_ends() {
    DrawFlight flight;
    flight.launch(sampleCard(), Side::Player, 0, kPile, kSlot, kSize);

    // Screen Y grows downward, so "high" is a small Y. The apex must clear the
    // higher of the two endpoints, or the card slides across the board instead
    // of being pulled off the stack.
    float highest = 9999.0f;
    for (float t = 0.0f; t < 0.45f; t += 0.01f) {
        flight.update(0.01f);
        const auto frames = flight.frames();
        if (!frames.empty()) highest = std::min(highest, frames[0].centre.y);
    }
    const float ceiling = std::min(kPile.y, kSlot.y);
    std::cout << "  apex y=" << highest << "  higher endpoint y=" << ceiling << "\n";
    CHECK_MSG(highest < ceiling - 60.0f, "the flight path is flat, not an arc");
    std::cout << "[PASS] test_flight_arcs_above_both_ends\n";
}

void test_card_turns_over_exactly_once_and_late() {
    DrawFlight flight;
    flight.launch(sampleCard(), Side::Player, 0, kPile, kSlot, kSize);

    runTo(flight, 0.44f);
    CHECK_MSG(!flight.frames().empty() && !flight.frames()[0].faceUp,
              "the card shows its face before it has finished flying");
    CHECK_MSG(!flight.consumeFlip(), "the flip fired early");

    // Somewhere in the pinch the card must be edge-on, or the turn reads as a
    // card that simply changed picture.
    float narrowest = 9999.0f;
    for (float t = 0.0f; t < 0.16f; t += 0.005f) {
        flight.update(0.005f);
        const auto frames = flight.frames();
        if (!frames.empty()) narrowest = std::min(narrowest, frames[0].size.x);
    }
    std::cout << "  narrowest width " << narrowest << " of " << kSize.x << "\n";
    CHECK_MSG(narrowest < kSize.x * 0.10f, "the card never closes up as it turns");
    CHECK_MSG(flight.consumeFlip(), "the flip never reported itself");
    CHECK_MSG(!flight.consumeFlip(), "the flip reported itself twice");
    std::cout << "[PASS] test_card_turns_over_exactly_once_and_late\n";
}

void test_the_hand_slot_stays_empty_until_the_card_lands() {
    DrawFlight flight;
    flight.launch(sampleCard(), Side::Player, 4, kPile, kSlot, kSize);

    CHECK_MSG(flight.hides(Side::Player, 4), "the hand would draw the card twice");
    CHECK_MSG(!flight.hides(Side::Player, 3), "an unrelated hand slot was blanked");
    CHECK_MSG(!flight.hides(Side::Opponent, 4), "the wrong side's hand was blanked");

    runTo(flight, 0.65f);
    CHECK_MSG(!flight.hides(Side::Player, 4),
              "the slot is still hidden after the card landed - the hand would be short");
    std::cout << "[PASS] test_the_hand_slot_stays_empty_until_the_card_lands\n";
}

void test_a_batch_is_staggered_not_stacked() {
    DrawFlight flight;
    for (int i = 0; i < 5; ++i) {
        flight.launch(sampleCard(), Side::Player, i, kPile, kSlot, kSize,
                      static_cast<float>(i) * 0.11f);
    }
    // Only the first has left the pile on the opening frame; five cards leaving
    // together arrive as one shape rather than as five cards.
    CHECK_MSG(flight.frames().size() == 1, "the whole hand launched on one frame");

    runTo(flight, 0.45f);
    std::cout << "  airborne at 0.45s: " << flight.frames().size() << " of 5\n";
    CHECK_MSG(flight.frames().size() == 5, "the batch never all got moving");

    runTo(flight, 0.60f);
    CHECK_MSG(!flight.busy(), "the staggered batch never cleared");
    std::cout << "[PASS] test_a_batch_is_staggered_not_stacked\n";
}


// ---------------------------------------------------------------------------
// Deployment primitives
//
// The six doctrine recipes live in DuelState, which needs a whole duel to
// build. What is checked here is the five primitives they are made of: that
// each one actually draws, that it lands where it was aimed, and that it clears
// itself up. A recipe that composes working primitives is then a reading
// exercise rather than a guess.
// ---------------------------------------------------------------------------

/// Render one spawner into a fresh target `seconds` after it fired.
/// Every effect in here spawns from Rng, so two renders of "the same" effect
/// are two different populations unless the generator is put back first. The
/// feather test compared an early frame of one random set against a late frame
/// of ANOTHER, which is not a measurement of drift at all - it red-lit the build
/// roughly one run in four, entirely on the draw.
constexpr unsigned int kVfxSeed = 20250911u;

bool renderPrimitive(const std::function<void(CombatVFX&)>& spawn, float seconds,
                     sf::Image& out, unsigned w = 320, unsigned h = 320) {
    sf::RenderTexture target;
    if (!target.create(w, h)) return false;

    Rng::seed(kVfxSeed);
    CombatVFX vfx;
    spawn(vfx);
    for (float t = 0.0f; t < seconds; t += 0.01f) vfx.update(0.01f);

    target.clear(sf::Color::Black);
    vfx.renderBelow(target);
    vfx.renderAbove(target);
    target.display();
    out = target.getTexture().copyToImage();
    return true;
}

/// Bounding box of everything brighter than `threshold`. Returns false if the
/// image is empty.
bool litBounds(const sf::Image& image, int threshold,
               unsigned& x0, unsigned& y0, unsigned& x1, unsigned& y1) {
    bool any = false;
    x0 = image.getSize().x; y0 = image.getSize().y; x1 = 0; y1 = 0;
    for (unsigned y = 0; y < image.getSize().y; ++y) {
        for (unsigned x = 0; x < image.getSize().x; ++x) {
            const sf::Color c = image.getPixel(x, y);
            if (c.r + c.g + c.b < threshold) continue;
            any = true;
            x0 = std::min(x0, x); y0 = std::min(y0, y);
            x1 = std::max(x1, x); y1 = std::max(y1, y);
        }
    }
    return any;
}

const sf::Vector2f kCell(160.0f, 200.0f);

void test_every_deployment_primitive_draws_and_then_clears() {
    struct Case {
        const char* name;
        std::function<void(CombatVFX&)> spawn;
        float alive;    // sampled while it should still be running
        float after;    // sampled once it should be gone
    };
    const Case cases[] = {
        { "groundCracks", [](CombatVFX& v) { v.groundCracks(kCell, sf::Color(200, 200, 210)); }, 0.10f, 0.70f },
        { "lightColumn",  [](CombatVFX& v) { v.lightColumn(kCell, sf::Color(255, 226, 150)); },  0.08f, 0.45f },
        { "boltRing",     [](CombatVFX& v) { v.boltRing(kCell, sf::Color(198, 148, 255)); },     0.10f, 0.50f },
        { "feathers",     [](CombatVFX& v) { v.feathers(kCell, sf::Color(150, 232, 214), 12); }, 0.30f, 2.10f },
        { "emberRing",    [](CombatVFX& v) { v.emberRing(kCell, sf::Color(255, 132, 48), 24); }, 0.10f, 0.80f },
    };

    for (const Case& c : cases) {
        sf::Image alive, gone;
        if (!renderPrimitive(c.spawn, c.alive, alive)) {
            std::cout << "  [SKIP] no render target available in this environment\n";
            return;
        }
        CHECK_MSG(renderPrimitive(c.spawn, c.after, gone), "second render failed");

        const int lit = litPixels(alive, 90);
        const int left = litPixels(gone, 90);
        std::cout << "  " << c.name << ": lit at " << c.alive << "s = " << lit
                  << ", left at " << c.after << "s = " << left << "\n";
        CHECK_MSG(lit > 20, "a deployment primitive draws nothing while it is running");
        CHECK_MSG(left == 0, "a deployment primitive is still drawing after its lifetime");
    }
    std::cout << "[PASS] test_every_deployment_primitive_draws_and_then_clears\n";
}

void test_the_light_column_falls_from_above_the_cell() {
    sf::Image image;
    if (!renderPrimitive([](CombatVFX& v) { v.lightColumn(kCell, sf::Color(255, 226, 150)); },
                         0.08f, image)) {
        std::cout << "  [SKIP] no render target available in this environment\n";
        return;
    }
    unsigned x0, y0, x1, y1;
    CHECK_MSG(litBounds(image, 90, x0, y0, x1, y1), "the column drew nothing");
    std::cout << "  column spans y " << y0 << ".." << y1 << " for a cell at y " << kCell.y << "\n";
    // It has to reach the cell and come from off the top, or it is a glow on
    // the floor rather than a drop from the sky.
    CHECK_MSG(y0 == 0, "the shaft does not come from off the top of the screen");
    CHECK_MSG(y1 >= static_cast<unsigned>(kCell.y) - 4, "the shaft stops short of the cell");
    // A little slack below the cell for the pool of light the beam lands in -
    // without it the shaft ends in mid-air on nothing.
    CHECK_MSG(y1 <= static_cast<unsigned>(kCell.y) + 20,
              "the shaft runs past the cell it lands on");
    std::cout << "[PASS] test_the_light_column_falls_from_above_the_cell\n";
}

void test_cracks_stay_on_the_ground_and_embers_spread_wider_than_they_rise() {
    sf::Image cracks, embers;
    if (!renderPrimitive([](CombatVFX& v) { v.groundCracks(kCell, sf::Color(200, 200, 210)); },
                         0.06f, cracks)) {
        std::cout << "  [SKIP] no render target available in this environment\n";
        return;
    }
    CHECK_MSG(renderPrimitive([](CombatVFX& v) { v.emberRing(kCell, sf::Color(255, 132, 48), 26); },
                              0.16f, embers), "ember render failed");

    unsigned x0, y0, x1, y1;
    CHECK_MSG(litBounds(cracks, 90, x0, y0, x1, y1), "the cracks drew nothing");
    const float crackW = static_cast<float>(x1 - x0);
    const float crackH = static_cast<float>(y1 - y0);
    std::cout << "  cracks " << crackW << " wide by " << crackH << " tall\n";
    // Both of these run along the deck, so both must be wider than they are
    // tall. A round burst would read as an explosion in the air.
    CHECK_MSG(crackW > crackH * 1.4f, "the cracks bulge upward instead of lying on the floor");

    CHECK_MSG(litBounds(embers, 90, x0, y0, x1, y1), "the embers drew nothing");
    const float emberW = static_cast<float>(x1 - x0);
    const float emberH = static_cast<float>(y1 - y0);
    std::cout << "  embers " << emberW << " wide by " << emberH << " tall\n";
    CHECK_MSG(emberW > emberH * 1.3f, "the ember ring is a fireball, not a shock front");
    std::cout << "[PASS] test_cracks_stay_on_the_ground_and_embers_spread_wider_than_they_rise\n";
}

void test_feathers_drift_sideways_as_they_fall() {
    // Sway is the whole point of the Valkyrie landing: without it these are
    // slow debris falling straight down.
    CombatVFX still;
    still.feathers(kCell, sf::Color(150, 232, 214), 1);

    sf::Image early, late;
    if (!renderPrimitive([](CombatVFX& v) { v.feathers(kCell, sf::Color(150, 232, 214), 14); },
                         0.05f, early)) {
        std::cout << "  [SKIP] no render target available in this environment\n";
        return;
    }
    CHECK_MSG(renderPrimitive([](CombatVFX& v) { v.feathers(kCell, sf::Color(150, 232, 214), 14); },
                              1.00f, late), "late render failed");

    unsigned ex0, ey0, ex1, ey1, lx0, ly0, lx1, ly1;
    CHECK_MSG(litBounds(early, 60, ex0, ey0, ex1, ey1), "no feathers at the start");
    CHECK_MSG(litBounds(late, 60, lx0, ly0, lx1, ly1), "the feathers vanished too early");
    std::cout << "  spread x " << (ex1 - ex0) << " -> " << (lx1 - lx0)
              << ",  lowest y " << ey1 << " -> " << ly1 << "\n";
    CHECK_MSG(ly1 > ey1, "the feathers are not falling");
    CHECK_MSG((lx1 - lx0) > (ex1 - ex0), "the feathers fall in a straight line - no sway");
    std::cout << "[PASS] test_feathers_drift_sideways_as_they_fall\n";
}


// ---------------------------------------------------------------------------
// The six doctrine landings
//
// The point of the recipes is that a player can tell which doctrine just landed
// without reading the card, so what is checked is that they are *different from
// each other*, not merely that each one draws.
// ---------------------------------------------------------------------------

struct Landing {
    MechRole role;
    const char* name;
};

const Landing kLandings[] = {
    { MechRole::Vanguard,   "Vanguard"   },
    { MechRole::Paladin,    "Arclight"   },
    { MechRole::Dragoon,    "Dragoon"    },
    { MechRole::Inquisitor, "Overseer"   },
    { MechRole::Siege,      "Siege"      },
    { MechRole::Valkyrie,   "Valkyrie"   },
};

/// Average colour and lit-pixel count of one doctrine's landing at `seconds`.
struct Signature {
    long r = 0, g = 0, b = 0;
    int lit = 0;
    DeploySignature::Shake shake;
};

bool renderLanding(MechRole role, float seconds, Signature& out) {
    sf::RenderTexture target;
    if (!target.create(420, 420)) return false;

    CardData card;
    card.id = "probe";
    card.role = role;

    CombatVFX vfx;
    out.shake = DeploySignature::play(vfx, card, { 210.0f, 240.0f }, 1);
    for (float t = 0.0f; t < seconds; t += 0.01f) vfx.update(0.01f);

    target.clear(sf::Color::Black);
    vfx.renderBelow(target);
    vfx.renderAbove(target);
    target.display();

    const sf::Image image = target.getTexture().copyToImage();
    // Kept on disk as well as measured. The numbers below say the six landings
    // differ; the pictures say whether they differ in a way worth looking at,
    // which is a judgement no assertion makes.
    if (const char* dir = std::getenv("SANCTUM_VFX_DUMP")) {
        image.saveToFile(std::string(dir) + "/landing_" +
                         std::to_string(static_cast<int>(role)) + ".png");
    }
    for (unsigned y = 0; y < image.getSize().y; ++y) {
        for (unsigned x = 0; x < image.getSize().x; ++x) {
            const sf::Color c = image.getPixel(x, y);
            if (c.r + c.g + c.b < 60) continue;
            out.r += c.r; out.g += c.g; out.b += c.b;
            ++out.lit;
        }
    }
    return true;
}

void test_every_doctrine_lands_differently() {
    Signature sigs[6];
    for (int i = 0; i < 6; ++i) {
        if (!renderLanding(kLandings[i].role, 0.12f, sigs[i])) {
            std::cout << "  [SKIP] no render target available in this environment\n";
            return;
        }
        const Signature& s = sigs[i];
        CHECK_MSG(s.lit > 60, "a doctrine landing draws almost nothing");
        std::cout << "  " << kLandings[i].name << ": " << s.lit << " lit, mean rgb "
                  << (s.r / s.lit) << "," << (s.g / s.lit) << "," << (s.b / s.lit)
                  << (s.shake.wanted() ? "  shake" : "  no shake") << "\n";
    }

    // Every pair has to differ in either how much it covers or what colour it
    // is. Two doctrines that match on both would be indistinguishable in play.
    for (int a = 0; a < 6; ++a) {
        for (int b = a + 1; b < 6; ++b) {
            const long ra = sigs[a].r / sigs[a].lit, ga = sigs[a].g / sigs[a].lit,
                       ba = sigs[a].b / sigs[a].lit;
            const long rb = sigs[b].r / sigs[b].lit, gb = sigs[b].g / sigs[b].lit,
                       bb = sigs[b].b / sigs[b].lit;
            const long hue = std::abs(ra - rb) + std::abs(ga - gb) + std::abs(ba - bb);
            const float area = static_cast<float>(std::max(sigs[a].lit, sigs[b].lit))
                             / static_cast<float>(std::min(sigs[a].lit, sigs[b].lit));
            const bool distinct = hue > 24 || area > 1.6f;
            if (!distinct) {
                std::cout << "  " << kLandings[a].name << " vs " << kLandings[b].name
                          << ": hue gap " << hue << ", area ratio " << area << "\n";
            }
            CHECK_MSG(distinct, "two doctrines land indistinguishably");
        }
    }
    std::cout << "[PASS] test_every_doctrine_lands_differently\n";
}

void test_only_the_heavy_doctrines_shake_the_camera() {
    Signature sigs[6];
    for (int i = 0; i < 6; ++i) {
        if (!renderLanding(kLandings[i].role, 0.05f, sigs[i])) {
            std::cout << "  [SKIP] no render target available in this environment\n";
            return;
        }
    }
    // Weight is the thing the shake encodes, so it belongs to the doctrines
    // that field heavy machines and to no others. Valkyrie above all must not
    // shake: it is the one landing that is supposed to feel weightless.
    CHECK_MSG(sigs[0].shake.wanted(), "a Vanguard lands without a jolt");
    CHECK_MSG(sigs[2].shake.wanted(), "a Dragoon lands without a jolt");
    CHECK_MSG(sigs[4].shake.wanted(), "a Siege fortress lands without a jolt");
    CHECK_MSG(!sigs[1].shake.wanted(), "an Arclight plasma drop shakes the camera");
    CHECK_MSG(!sigs[3].shake.wanted(), "an Overseer EMP shakes the camera");
    CHECK_MSG(!sigs[5].shake.wanted(), "a Valkyrie settling shakes the camera");

    // And the fortress must be the heaviest of the three.
    std::cout << "  shake seconds  Vanguard " << sigs[0].shake.seconds
              << "  Dragoon " << sigs[2].shake.seconds
              << "  Siege " << sigs[4].shake.seconds << "\n";
    CHECK_MSG(sigs[4].shake.seconds > sigs[0].shake.seconds &&
              sigs[4].shake.seconds > sigs[2].shake.seconds,
              "the heaviest doctrine in the game is not the heaviest landing");
    std::cout << "[PASS] test_only_the_heavy_doctrines_shake_the_camera\n";
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << " COMBAT VFX TESTS\n";
    std::cout << "========================================\n";

    test_flare_draws_a_border_in_the_armed_colour();
    test_flare_burns_off_and_disappears();
    test_flare_holds_long_enough_to_see();

    test_flight_leaves_the_pile_and_reaches_the_slot();
    test_flight_arcs_above_both_ends();
    test_card_turns_over_exactly_once_and_late();
    test_the_hand_slot_stays_empty_until_the_card_lands();
    test_a_batch_is_staggered_not_stacked();

    test_every_deployment_primitive_draws_and_then_clears();
    test_the_light_column_falls_from_above_the_cell();
    test_cracks_stay_on_the_ground_and_embers_spread_wider_than_they_rise();
    test_feathers_drift_sideways_as_they_fall();

    test_every_doctrine_lands_differently();
    test_only_the_heavy_doctrines_shake_the_camera();

    std::cout << "========================================\n";
    std::cout << " ALL COMBAT VFX TESTS PASSED\n";
    std::cout << "========================================\n";
    return 0;
}
