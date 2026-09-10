// Renders combat effects off-screen and reads the pixels back.
//
// Driving the real game to catch a 1.3 second effect turned out to be a poor
// way to check it: the shot lands early, or late, or the hand that turn holds no
// counter at all. Rendering it to a texture and looking at the result answers
// the same question in milliseconds and keeps answering it.

#include "TestAssert.hpp"
#include "rendering/CardArt.hpp"
#include "rendering/CombatVFX.hpp"
#include "rendering/DrawFlight.hpp"

#include <SFML/Graphics/RenderTexture.hpp>
#include <cmath>
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

    std::cout << "========================================\n";
    std::cout << " ALL COMBAT VFX TESTS PASSED\n";
    std::cout << "========================================\n";
    return 0;
}
