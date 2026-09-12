#include "rendering/CardArt.hpp"

#include "utils/Fonts.hpp"
#include "utils/ResourceManager.hpp"
#include "utils/TextUtils.hpp"
#include <algorithm>
#include <cmath>

namespace CardArt {
namespace {

/// Cover-fit a sprite into `window` without distorting the artwork.
void coverFit(sf::Sprite& sprite, const sf::Texture& texture, sf::Vector2f window) {
    const sf::Vector2u size = texture.getSize();
    if (size.x == 0 || size.y == 0 || window.x <= 0.0f || window.y <= 0.0f) return;

    const float scale = std::max(window.x / static_cast<float>(size.x),
                                 window.y / static_cast<float>(size.y));
    const int visibleW = static_cast<int>(std::round(window.x / scale));
    const int visibleH = static_cast<int>(std::round(window.y / scale));

    sprite.setTexture(texture, true);
    sprite.setTextureRect(sf::IntRect((static_cast<int>(size.x) - visibleW) / 2,
                                      (static_cast<int>(size.y) - visibleH) / 2,
                                      visibleW, visibleH));
    sprite.setOrigin(visibleW / 2.0f, visibleH / 2.0f);
    sprite.setScale(scale, scale);
}

/// A stat gem. It takes no font: the number inside it is one of the things a
/// player reads most often in a duel, so it is always set in the UI face.
void drawStatBadge(sf::RenderTarget& target, sf::Vector2f centre,
                   float radius, sf::Color fill, int value, unsigned int charSize) {
    sf::CircleShape gem(radius);
    gem.setOrigin(radius, radius);
    gem.setPosition(centre);
    gem.setFillColor(fill);
    gem.setOutlineThickness(1.5f);
    gem.setOutlineColor(sf::Color(14, 12, 10, 235));
    target.draw(gem);

    sf::Text text;
    text.setFont(Fonts::ui());
    text.setString(std::to_string(value));
    text.setCharacterSize(charSize);
    text.setStyle(sf::Text::Bold);
    text.setFillColor(sf::Color(18, 15, 12));
    TextUtils::centerBoth(text);
    text.setPosition(centre.x, centre.y - 1.0f);
    target.draw(text);
}

} // namespace

sf::Color accentFor(const CardData& card) {
    // One signature colour per doctrine, readable against the dark board.
    switch (card.role) {
    case MechRole::Vanguard:   return sf::Color(120, 160, 210);   // steel blue
    case MechRole::Paladin:    return sf::Color(240, 190, 80);    // plasma gold
    case MechRole::Valkyrie:   return sf::Color(150, 220, 195);   // nanite mint
    case MechRole::Dragoon:    return sf::Color(230, 120, 90);    // afterburner
    case MechRole::Siege:      return sf::Color(190, 150, 100);   // gun bronze
    case MechRole::Inquisitor: return sf::Color(175, 120, 225);   // cipher violet
    }
    return sf::Color(160, 176, 196);
}

sf::Color panelFor(const CardData& card) {
    // A dark hull tinted towards the doctrine accent.
    const sf::Color accent = accentFor(card);
    return sf::Color(static_cast<sf::Uint8>(16 + accent.r / 12),
                     static_cast<sf::Uint8>(20 + accent.g / 12),
                     static_cast<sf::Uint8>(28 + accent.b / 12), 250);
}

/**
 * @brief What fills the art window when a card has no picture yet.
 *
 * A missing texture used to leave the card body showing through, which reads as
 * a flat coloured slab and looks like a rendering bug rather than art that has
 * not been drawn yet. This paints the window as a deliberate placeholder: a
 * vertical wash in the doctrine's accent, a hairline border, and the doctrine's
 * initial set large and faint behind where the picture will go.
 */
void drawArtPlaceholder(sf::RenderTarget& target, const sf::Font& font,
                        const CardData& card, sf::Vector2f centre, sf::Vector2f size,
                        sf::RenderStates states, int dim) {
    // Built from the card BODY colour with only a trace of accent mixed in. A
    // wash made from the accent alone came out as a slab of hot magenta on
    // Inquisitor cards - louder than the real art it stands in for, which is the
    // opposite of what a placeholder should do.
    const sf::Color accent = accentFor(card);
    const sf::Color panel = panelFor(card);
    const auto shade = [&](float mix, float lift, sf::Uint8 alpha) {
        const auto channel = [&](float base, float tint) {
            const float v = (base * (1.0f - mix) + tint * mix) * lift * dim / 255.0f;
            return static_cast<sf::Uint8>(std::min(255.0f, std::max(0.0f, v)));
        };
        return sf::Color(channel(panel.r, accent.r), channel(panel.g, accent.g),
                         channel(panel.b, accent.b), alpha);
    };

    const float hw = size.x / 2.0f;
    const float hh = size.y / 2.0f;
    sf::VertexArray wash(sf::TriangleStrip, 4);
    wash[0].position = { centre.x - hw, centre.y - hh };
    wash[1].position = { centre.x + hw, centre.y - hh };
    wash[2].position = { centre.x - hw, centre.y + hh };
    wash[3].position = { centre.x + hw, centre.y + hh };
    wash[0].color = wash[1].color = shade(0.18f, 1.15f, 255);
    wash[2].color = wash[3].color = shade(0.06f, 0.80f, 255);
    target.draw(wash, states);

    sf::RectangleShape edge(size);
    edge.setOrigin(hw, hh);
    edge.setPosition(centre);
    edge.setFillColor(sf::Color::Transparent);
    edge.setOutlineThickness(1.0f);
    edge.setOutlineColor(shade(0.55f, 1.0f, 110));
    target.draw(edge, states);

    // The doctrine initial, sized to the window so it works at hand scale and
    // at the zoomed scale without a second set of numbers.
    sf::Text mark;
    mark.setFont(font);   // decoration, drawn large - the display face suits it
    mark.setString(std::string(1, displayName(card.role)[0]));
    mark.setCharacterSize(static_cast<unsigned>(std::max(10.0f, size.y * 0.62f)));
    mark.setStyle(sf::Text::Bold);
    mark.setFillColor(shade(1.0f, 1.0f, 70));
    const sf::FloatRect mb = mark.getLocalBounds();
    mark.setOrigin(mb.left + mb.width / 2.0f, mb.top + mb.height / 2.0f);
    mark.setPosition(centre);
    target.draw(mark, states);
}


/// True for the two doctrines whose frame is an ornate border rather than a
/// hairline. Their ornament eats into the card, so the text column has to give
/// it room - see textW in drawCard.
bool ornateFrame(const CardData& card) {
    return card.role == MechRole::Dragoon || card.role == MechRole::Siege;
}

std::string frameFor(const CardData& card) {
    // Dragoon and Siege have frames of their own now. The remaining four
    // doctrines still share the hand-made set, allotted by feel: warm for
    // plasma, cold for armour, dark for warfare.
    switch (card.role) {
    case MechRole::Dragoon:    return "assets/frames/frame_dragoon.png";
    case MechRole::Siege:      return "assets/frames/frame_siege.png";
    case MechRole::Paladin:    return "assets/frames/frame_paladin.png";
    case MechRole::Valkyrie:   return "assets/frames/frame_saintess.png";
    case MechRole::Inquisitor: return "assets/frames/frame_eclipse.png";
    case MechRole::Vanguard:   break;
    }
    return "assets/frames/frame_knight.png";
}

std::string keywordLine(const CardData& card) {
    std::string line;
    auto add = [&](Keyword::Mask mask, const char* name) {
        if (!card.hasKeyword(mask)) return;
        if (!line.empty()) line += "  ";
        line += name;
    };
    add(Keyword::Taunt, "GUARD");
    add(Keyword::Ranged, "RANGED");
    add(Keyword::Rush, "BLITZ");
    add(Keyword::Aerial, "AERIAL");
    add(Keyword::Thruster, "THRUSTER");
    add(Keyword::Intercept, "INTERCEPT");
    add(Keyword::Entrench, "ENTRENCH");
    add(Keyword::Reactive, "REACTIVE");
    add(Keyword::Overkill, "OVERKILL");
    add(Keyword::Plasma, "PLASMA");
    add(Keyword::Splash, "SPLASH");
    add(Keyword::EMP, "EMP");
    return line;
}

std::vector<GlossaryEntry> glossaryFor(const CardData& card) {
    std::vector<GlossaryEntry> entries;

    // What the card type does, first - it frames everything else.
    switch (card.category) {
    case CardCategory::Spell:
        entries.push_back({ "SPELL",
            "Resolves the moment you play it, then goes to your crypt." });
        break;
    case CardCategory::Trap:
        entries.push_back({ "TRAP",
            "Set face-down for 1 mana. It stays hidden and flips by itself the "
            "instant its condition is met, even on the enemy's turn." });
        break;
    case CardCategory::Unit:
        entries.push_back({ "UNIT",
            "Deploy to your frontline or support row. It cannot attack the turn "
            "it arrives unless it has Blitz. "
            "Without Ranged or Aerial it fights in melee: it must stand in your "
            "frontline, and it reaches only its own lane and one lane either "
            "side. It can strike the enemy reactor only once their whole board "
            "is empty." });
        break;
    }

    // Summoning cost rules only matter for the higher tiers.
    if (card.tier == CardTier::Tier2) {
        entries.push_back({ "TIER II",
            "May be deployed by scrapping 1 friendly frame as tribute, which cuts "
            "2 energy from its cost." });
    } else if (card.tier == CardTier::Tier3) {
        entries.push_back({ "TIER III",
            "May be deployed by scrapping 2 friendly frames as tribute, which "
            "cuts 4 energy from its cost. Only your primary core may field one." });
    }

    auto keyword = [&](Keyword::Mask mask, const char* term, const char* text) {
        if (card.hasKeyword(mask)) entries.push_back({ term, text });
    };

    keyword(Keyword::Taunt, "GUARD",
            "Screens the frames immediately left and right of it: while it "
            "stands, they cannot be attacked at all. It does not protect the "
            "rest of the row, and the Guard itself is always a legal target - "
            "so a wall is built by spacing Guards, not by stacking them.");
    keyword(Keyword::Ranged, "RANGED",
            "Artillery. Fires from either row, at any lane, and takes no return "
            "fire - but deals none either, so it cannot defend itself in melee. "
            "It can shell the enemy reactor over anything in the way, at half "
            "damage; ranging on the core that way, or firing at all from your "
            "support row, leaves it exposed to their fire until your next "
            "upkeep. It cannot reach a standing enemy support row; that is what "
            "Aerial is for. Soft: takes 1 more damage from every hit.");
    keyword(Keyword::Rush, "BLITZ",
            "Can attack on the same turn it is deployed.");
    keyword(Keyword::Aerial, "AERIAL",
            "Flies past the frontline to strike anything on the enemy board, "
            "support row included, and takes no return fire. The only keyword "
            "that reaches the back row through an intact line. Guard still "
            "holds it.");
    keyword(Keyword::Thruster, "THRUSTER",
            "Advancing to the frontline costs no energy.");
    keyword(Keyword::Reactive, "REACTIVE",
            "Reactive plating: takes 1 less damage from every attack.");
    keyword(Keyword::Overkill, "OVERKILL",
            "Damage beyond what it takes to scrap the defender carries "
            "straight through into the enemy reactor.");
    keyword(Keyword::Plasma, "PLASMA",
            "Ignores plating entirely - reactive armour, Aegis and absorption "
            "alike. With an Overcharge Core behind it, each strike also spends "
            "1 overcharge for +2 damage.");
    keyword(Keyword::Splash, "SPLASH",
            "The attack also spills half its damage onto the frames flanking "
            "the target.");
    keyword(Keyword::EMP, "EMP",
            "Anything it hits is shorted out: it cannot act next turn and "
            "burns 1 health each upkeep.");

    // Ability triggers the player can see on the card face.
    for (const Ability& ability : card.abilities) {
        switch (ability.trigger) {
        case AbilityTrigger::OnDeploy:
            entries.push_back({ "DEPLOY",
                "Triggers once, the moment the frame lands from your hand." });
            break;
        case AbilityTrigger::OnDestroyed:
            entries.push_back({ "DETONATE",
                "Triggers when this frame is destroyed, wherever it was." });
            break;
        case AbilityTrigger::OnDamaged:
            entries.push_back({ "ON HIT",
                "Triggers each time this frame survives an attack." });
            break;
        case AbilityTrigger::Aura:
            entries.push_back({ "FIELD",
                "Applies continuously while this frame is on the board, and ends "
                "the moment it leaves." });
            break;
        case AbilityTrigger::OnKill:
            entries.push_back({ "ON KILL",
                "Triggers each time this frame destroys an enemy." });
            break;
        case AbilityTrigger::OnTurnStart:
            entries.push_back({ "UPKEEP",
                "Triggers at the start of each of your turns." });
            break;
        case AbilityTrigger::OnTurnEnd:
            entries.push_back({ "END OF TURN",
                "Triggers at the end of each of your turns." });
            break;
        default:
            break;
        }
    }

    // The same trigger can appear twice on one card; keep the first of each.
    std::vector<GlossaryEntry> unique;
    for (const GlossaryEntry& entry : entries) {
        const bool seen = std::any_of(unique.begin(), unique.end(),
            [&](const GlossaryEntry& e) { return e.term == entry.term; });
        if (!seen) unique.push_back(entry);
    }
    return unique;
}

std::vector<GlossaryEntry> statusesFor(const Unit& unit) {
    std::vector<GlossaryEntry> out;

    if (unit.armour > 0) {
        out.push_back({ "ABSORPTION PLATING",
            "Carrying " + std::to_string(unit.armour) + " plating. It soaks that "
            "much from the next hits and lapses at your upkeep. Plasma ignores it." });
    }
    if (unit.stunTurns > 0) {
        out.push_back({ "SHORTED OUT",
            "Cannot attack or advance for " + std::to_string(unit.stunTurns) +
            " more turn(s)." });
    }
    if (unit.shorted) {
        out.push_back({ "EMP BURN",
            "Loses 1 health at the start of each of its controller's turns until "
            "it is destroyed." });
    }
    if (unit.wardOff) {
        out.push_back({ "HARDENED",
            "An allied field is shielding it: spells and counter-protocols "
            "cannot destroy it outright." });
    }
    if (unit.auraAttack != 0 || unit.auraHealth != 0) {
        std::string text = "An allied field is granting ";
        if (unit.auraAttack != 0) text += std::to_string(unit.auraAttack) + " attack";
        if (unit.auraAttack != 0 && unit.auraHealth != 0) text += " and ";
        if (unit.auraHealth != 0) text += std::to_string(unit.auraHealth) + " health";
        text += ". It is lost the moment the source leaves the board.";
        out.push_back({ "FIELD BONUS", text });
    }
    if (unit.auraRangedBonus > 0) {
        out.push_back({ "FIRE SUPPORT",
            "A spotter is adding " + std::to_string(unit.auraRangedBonus) +
            " damage to its ranged attacks." });
    }
    if (unit.permAttack != 0 || unit.permHealth != 0) {
        std::string text = "Permanently ";
        text += (unit.permAttack < 0 || unit.permHealth < 0) ? "reduced by " : "raised by ";
        text += std::to_string(std::abs(unit.permAttack)) + " attack and "
              + std::to_string(std::abs(unit.permHealth)) + " health. "
              "This does not wear off.";
        out.push_back({ "PERMANENT DAMAGE", text });
    }
    if (unit.extraAttacks > 0) {
        out.push_back({ "MULTI-STRIKE",
            "May attack " + std::to_string(unit.extraAttacks) +
            " extra time(s) each turn." });
    }
    if (unit.damage > 0) {
        out.push_back({ "DAMAGED",
            "Has taken " + std::to_string(unit.damage) + " damage. Repairs remove "
            "it; the frame is destroyed if it reaches its maximum." });
    }
    if (unit.exhausted && unit.stunTurns == 0) {
        out.push_back({ "SPINNING UP",
            "Deployed too recently to act. It is ready at your next upkeep." });
    }
    return out;
}

// =============================================================================

void drawCard(sf::RenderTarget& target, const sf::Font& font, const CardData& card,
              sf::Vector2f centre, sf::Vector2f size, float rotation,
              bool playable, bool highlighted, bool withBadges) {
    const sf::Color accent = accentFor(card);
    const sf::Uint8 dim = playable ? 255 : 118;
    auto fade = [dim](sf::Color c) {
        return sf::Color(static_cast<sf::Uint8>(c.r * dim / 255),
                         static_cast<sf::Uint8>(c.g * dim / 255),
                         static_cast<sf::Uint8>(c.b * dim / 255), c.a);
    };

    sf::RenderStates states;
    states.transform.translate(centre);
    states.transform.rotate(rotation);

    // Proportions taken from the frame art: the artwork ends on its divider.
    const float artTop = -0.457f * size.y;
    const float artH = 0.457f * size.y;
    const float artW = 0.840f * size.x;
    // 0.88, not 0.80. The column was measured around Georgia; the UI face runs
    // wider at the same pixel size, and paying for that in width costs nothing
    // but paying for it in point size undoes the reason for the change.
    //
    // The two ornate frames are the exception. Their stone and steel border
    // reaches about 13% of the card width in from each edge even after being
    // thinned, and an 88% column ran straight under it - the card's own name
    // was unreadable behind a pillar. They get 78% instead, which clears the
    // ornament with a little to spare.
    const float textW = (ornateFrame(card) ? 0.780f : 0.880f) * size.x;

    // Body
    sf::RectangleShape body(size);
    body.setOrigin(size.x / 2.0f, size.y / 2.0f);
    body.setFillColor(panelFor(card));
    body.setOutlineThickness(1.0f);
    body.setOutlineColor(sf::Color(8, 7, 10, 220));
    target.draw(body, states);

    // Artwork
    const sf::Texture& art = ResourceManager::get().getTexture(card.textureFile);
    if (art.getSize().x > 1) {
        sf::Sprite sprite;
        coverFit(sprite, art, { artW, artH });
        sprite.setPosition(0.0f, artTop + artH / 2.0f);
        sprite.setColor(sf::Color(dim, dim, dim, 255));
        target.draw(sprite, states);
    } else {
        drawArtPlaceholder(target, font, card, { 0.0f, artTop + artH / 2.0f },
                           { artW, artH }, states, dim);
    }

    // Scrim so the name never fights the picture
    sf::VertexArray scrim(sf::TriangleStrip, 4);
    const float halfArt = artW / 2.0f;
    const float scrimTop = -artH * 0.28f;
    scrim[0] = sf::Vertex({ -halfArt, scrimTop }, sf::Color(0, 0, 0, 0));
    scrim[1] = sf::Vertex({ halfArt, scrimTop }, sf::Color(0, 0, 0, 0));
    scrim[2] = sf::Vertex({ -halfArt, 0.0f }, sf::Color(0, 0, 0, 195));
    scrim[3] = sf::Vertex({ halfArt, 0.0f }, sf::Color(0, 0, 0, 195));
    target.draw(scrim, states);

    // Frame
    const sf::Texture& frame = ResourceManager::get().getTexture(frameFor(card));
    if (frame.getSize().x > 1) {
        sf::Sprite sprite(frame);
        const sf::FloatRect fb = sprite.getLocalBounds();
        sprite.setOrigin(fb.width / 2.0f, fb.height / 2.0f);
        sprite.setScale(size.x / fb.width, size.y / fb.height);
        sprite.setColor(sf::Color(dim, dim, dim, 255));
        target.draw(sprite, states);
    }

    // Name
    sf::Text title;
    title.setFont(Fonts::ui());
    title.setString(card.name);
    title.setCharacterSize(static_cast<unsigned int>(0.082f * size.x));
    title.setStyle(sf::Text::Bold);
    title.setFillColor(sf::Color(248, 244, 232));
    while (title.getCharacterSize() > 8 && title.getLocalBounds().width > textW) {
        title.setCharacterSize(title.getCharacterSize() - 1);
    }
    // At hand size the floor of 8px is reached immediately, so a long name like
    // "Bastion-01 Guard Drone" ran straight off both sides of the card. Rather
    // than shrink below a readable size, condense it - a semibold sans takes
    // 12% out of its width without looking squeezed, which covers every name in
    // the catalogue. Anything still over is truncated rather than clipped, so
    // the card says there is more to read instead of ending mid-word.
    TextUtils::centerHorizontally(title);
    float squeeze = 1.0f;
    if (title.getLocalBounds().width > textW) {
        squeeze = std::max(0.88f, textW / title.getLocalBounds().width);
        title.setScale(squeeze, 1.0f);
    }
    if (title.getLocalBounds().width * squeeze > textW) {
        std::string clipped = card.name;
        while (clipped.size() > 4) {
            clipped.pop_back();
            // Three dots, not an ellipsis glyph: sf::Text reads a std::string as
            // Latin-1, so the three UTF-8 bytes of U+2026 arrive as three wrong
            // characters. Anything above ASCII has to go through
            // sf::String::fromUtf8, and at 8px it would not look different.
            title.setString(clipped + "...");
            TextUtils::centerHorizontally(title);
            if (title.getLocalBounds().width * squeeze <= textW) break;
        }
    }
    title.setPosition(0.0f, 0.028f * size.y);
    target.draw(title, states);

    // Type line: role, tier, keywords
    std::string typeLine = displayName(card.role);
    if (card.category == CardCategory::Spell) typeLine = "SPELL";
    else if (card.category == CardCategory::Trap) typeLine = "TRAP";
    else if (card.tier == CardTier::Tier2) typeLine += "  II";
    else if (card.tier == CardTier::Tier3) typeLine += "  III";

    sf::Text type;
    type.setFont(Fonts::ui());
    type.setString(typeLine);
    type.setCharacterSize(static_cast<unsigned int>(0.054f * size.x));
    type.setLetterSpacing(1.5f);
    type.setFillColor(fade(accent));
    TextUtils::centerHorizontally(type);
    type.setPosition(0.0f, 0.104f * size.y);
    target.draw(type, states);

    // Rules text, shrunk until it fits between the divider and the bottom rule.
    //
    // 0.080 rather than 0.064, with a floor of 8px rather than 7: on a 106px
    // hand card the old ratio landed on 6-7px, which is not a readable size -
    // the player had to open the inspector to find out what a card in their own
    // hand did. Starting higher and refusing to go below 8 means a long rules
    // line wraps to another row instead of shrinking out of legibility.
    const float textTop = 0.168f * size.y;
    // Reserve the badge row. Without this the text was allowed to run down to
    // the card edge and the attack/health circles were then drawn on top of its
    // last line - which is why the final words of a long rules box disappeared
    // behind a number.
    const float badgeBand = (withBadges && card.category == CardCategory::Unit)
                                ? 0.170f * size.x
                                : 0.075f * size.y;
    const float textRoom = size.y / 2.0f - textTop - badgeBand;
    sf::Text desc;
    desc.setFont(Fonts::ui());
    desc.setLineSpacing(1.12f);
    desc.setFillColor(sf::Color(206, 204, 198));
    unsigned int textSize = static_cast<unsigned int>(0.080f * size.x);
    for (;;) {
        desc.setCharacterSize(textSize);
        desc.setString(TextUtils::wrap(card.description, Fonts::ui(), textSize, textW));
        if (textSize <= 8 || desc.getLocalBounds().height <= textRoom) break;
        --textSize;
    }

    // A long rules box still will not fit on a 106px card at a readable size,
    // and it used to simply run off the bottom edge and under the badges. Drop
    // whole lines until it fits and mark the cut, which is honest: the full
    // text is one hover away, and a card that trails off says so.
    if (desc.getLocalBounds().height > textRoom) {
        std::string rules = desc.getString();
        while (!rules.empty() && desc.getLocalBounds().height > textRoom) {
            const std::size_t cut = rules.rfind('\n');
            if (cut == std::string::npos) { rules.clear(); break; }
            rules.erase(cut);
            desc.setString(rules + " ...");
        }
    }
    TextUtils::centerHorizontally(desc);
    desc.setPosition(0.0f, textTop);
    target.draw(desc, states);

    // Mana gem
    const float gemR = 0.076f * size.x;
    if (!withBadges) {
        if (highlighted) {
            sf::RectangleShape glow(size);
            glow.setOrigin(size.x / 2.0f, size.y / 2.0f);
            glow.setFillColor(sf::Color::Transparent);
            glow.setOutlineThickness(2.5f);
            glow.setOutlineColor(sf::Color(255, 240, 176, 225));
            target.draw(glow, states);
        }
        return;
    }

    sf::CircleShape gem(gemR);
    gem.setOrigin(gemR, gemR);
    gem.setPosition(-size.x / 2.0f + gemR + 3.0f, artTop + gemR + 1.0f);
    gem.setFillColor(playable ? sf::Color(120, 200, 255) : sf::Color(104, 100, 100));
    gem.setOutlineThickness(1.5f);
    gem.setOutlineColor(sf::Color(16, 14, 12, 235));
    // Gems are drawn through the same transform so they rotate with the card.
    {
        sf::CircleShape local = gem;
        target.draw(local, states);

        sf::Text cost;
        cost.setFont(Fonts::ui());
        cost.setString(std::to_string(card.manaCost));
        cost.setCharacterSize(static_cast<unsigned int>(gemR * 1.25f));
        cost.setStyle(sf::Text::Bold);
        cost.setFillColor(sf::Color(18, 15, 12));
        TextUtils::centerBoth(cost);
        cost.setPosition(local.getPosition().x, local.getPosition().y - 1.0f);
        target.draw(cost, states);
    }

    // Attack / health for units
    if (card.category == CardCategory::Unit) {
        const float r = 0.074f * size.x;
        const float y = size.y / 2.0f - r - 4.0f;
        sf::CircleShape atk(r);
        atk.setOrigin(r, r);
        atk.setPosition(-size.x / 2.0f + r + 4.0f, y);
        atk.setFillColor(sf::Color(226, 148, 62));
        atk.setOutlineThickness(1.5f);
        atk.setOutlineColor(sf::Color(16, 14, 12, 235));
        target.draw(atk, states);

        sf::Text atkText;
        atkText.setFont(Fonts::ui());
        atkText.setString(std::to_string(card.attack));
        atkText.setCharacterSize(static_cast<unsigned int>(r * 1.25f));
        atkText.setStyle(sf::Text::Bold);
        atkText.setFillColor(sf::Color(24, 16, 8));
        TextUtils::centerBoth(atkText);
        atkText.setPosition(atk.getPosition().x, atk.getPosition().y - 1.0f);
        target.draw(atkText, states);

        sf::CircleShape hp(r);
        hp.setOrigin(r, r);
        hp.setPosition(size.x / 2.0f - r - 4.0f, y);
        hp.setFillColor(sf::Color(198, 72, 76));
        hp.setOutlineThickness(1.5f);
        hp.setOutlineColor(sf::Color(16, 14, 12, 235));
        target.draw(hp, states);

        sf::Text hpText = atkText;
        hpText.setString(std::to_string(card.health));
        TextUtils::centerBoth(hpText);
        hpText.setFillColor(sf::Color(255, 236, 232));
        hpText.setPosition(hp.getPosition().x, hp.getPosition().y - 1.0f);
        target.draw(hpText, states);
    }

    if (highlighted) {
        sf::RectangleShape glow(size);
        glow.setOrigin(size.x / 2.0f, size.y / 2.0f);
        glow.setFillColor(sf::Color::Transparent);
        glow.setOutlineThickness(2.5f);
        glow.setOutlineColor(sf::Color(255, 240, 176, 225));
        target.draw(glow, states);
    }
}

// =============================================================================

void drawUnit(sf::RenderTarget& target, const sf::Font& font, const Unit& unit,
              sf::FloatRect bounds, bool selectable, bool selected, bool targeted) {
    const CardData& card = unit.data;
    const sf::Color accent = accentFor(card);

    // Lift the tile off the dark table: the card palette alone is almost the
    // same value as the board and the units disappeared into it.
    const sf::Color panel = panelFor(card);
    sf::RectangleShape body({ bounds.width, bounds.height });
    body.setPosition(bounds.left, bounds.top);
    body.setFillColor(sf::Color(static_cast<sf::Uint8>(std::min(255, panel.r + 22)),
                                static_cast<sf::Uint8>(std::min(255, panel.g + 20)),
                                static_cast<sf::Uint8>(std::min(255, panel.b + 24)), 252));
    body.setOutlineThickness(1.5f);
    body.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, 190));
    target.draw(body);

    // Portrait fills the top two thirds. 0.70 rather than 0.60: paired with the
    // taller tile this brings the art window to roughly 4:3, which is the
    // aspect every illustration is cropped to, so coverFit stops cutting the
    // subject in half.
    const float artH = bounds.height * 0.70f;
    const sf::Texture& art = ResourceManager::get().getTexture(card.textureFile);
    if (art.getSize().x > 1) {
        sf::Sprite sprite;
        coverFit(sprite, art, { bounds.width - 4.0f, artH });
        sprite.setPosition(bounds.left + bounds.width / 2.0f, bounds.top + 2.0f + artH / 2.0f);
        if (unit.stunTurns > 0) sprite.setColor(sf::Color(150, 160, 200));
        else if (!selectable)   sprite.setColor(sf::Color(190, 190, 190));
        target.draw(sprite);
    } else {
        drawArtPlaceholder(target, font, card,
                           { bounds.left + bounds.width / 2.0f, bounds.top + 2.0f + artH / 2.0f },
                           { bounds.width - 4.0f, artH }, sf::RenderStates::Default,
                           selectable ? 255 : 190);
    }

    // Stat bar under the art. It carries only the badges: the name used to sit
    // here too and the attack and health circles were drawn straight over both
    // ends of it, so anything longer than about eight characters was unreadable.
    sf::RectangleShape plate({ bounds.width - 4.0f, bounds.height - artH - 4.0f });
    plate.setPosition(bounds.left + 2.0f, bounds.top + artH + 2.0f);
    plate.setFillColor(sf::Color(10, 9, 14, 225));
    target.draw(plate);

    // Name rides a scrim across the bottom of the artwork instead, which is
    // where a card game normally puts it and which leaves the full tile width
    // free of the badges.
    const float nameH = 15.0f;
    sf::RectangleShape nameStrip({ bounds.width - 4.0f, nameH });
    nameStrip.setPosition(bounds.left + 2.0f, bounds.top + artH - nameH);
    nameStrip.setFillColor(sf::Color(8, 7, 11, 205));
    target.draw(nameStrip);

    sf::Text name;
    name.setFont(Fonts::ui());
    name.setString(card.name);
    name.setCharacterSize(11);
    name.setFillColor(sf::Color(236, 232, 222));
    while (name.getCharacterSize() > 7 && name.getLocalBounds().width > bounds.width - 8.0f) {
        name.setCharacterSize(name.getCharacterSize() - 1);
    }
    TextUtils::centerHorizontally(name);
    name.setPosition(bounds.left + bounds.width / 2.0f, bounds.top + artH - nameH + 1.0f);
    target.draw(name);

    // Keyword ribbon along the top
    const std::string keywords = keywordLine(card);
    if (!keywords.empty()) {
        sf::Text kw;
        kw.setFont(Fonts::ui());
        kw.setString(keywords);
        kw.setCharacterSize(8);
        kw.setLetterSpacing(1.2f);
        kw.setFillColor(sf::Color(250, 238, 190, 235));
        kw.setOutlineColor(sf::Color(0, 0, 0, 200));
        kw.setOutlineThickness(1.5f);
        while (kw.getCharacterSize() > 6 && kw.getLocalBounds().width > bounds.width - 8.0f) {
            kw.setCharacterSize(kw.getCharacterSize() - 1);
        }
        TextUtils::centerHorizontally(kw);
        kw.setPosition(bounds.left + bounds.width / 2.0f, bounds.top + 3.0f);
        target.draw(kw);
    }

    // Attack and health badges straddle the bottom corners
    const float r = 13.0f;
    const float badgeY = bounds.top + artH + (bounds.height - artH) / 2.0f;
    drawStatBadge(target, { bounds.left + r + 3.0f, badgeY },
                  r, sf::Color(226, 148, 62), unit.attack(), 13);

    const bool hurt = unit.damage > 0;
    drawStatBadge(target, { bounds.left + bounds.width - r - 3.0f, badgeY },
                  r, hurt ? sf::Color(214, 84, 78) : sf::Color(122, 186, 120), unit.health(), 13);

    // Status pips down the right edge
    float pipY = bounds.top + 6.0f;
    auto pip = [&](sf::Color colour) {
        sf::CircleShape dot(4.0f);
        dot.setOrigin(4.0f, 4.0f);
        dot.setPosition(bounds.left + bounds.width - 7.0f, pipY);
        dot.setFillColor(colour);
        dot.setOutlineThickness(1.0f);
        dot.setOutlineColor(sf::Color(10, 8, 12, 220));
        target.draw(dot);
        pipY += 11.0f;
    };
    if (unit.armour > 0)    pip(sf::Color(120, 190, 255));
    if (unit.stunTurns > 0) pip(sf::Color(180, 190, 255));
    if (unit.shorted)       pip(sf::Color(140, 220, 120));
    if (unit.wardOff)       pip(sf::Color(250, 232, 150));

    // Exhausted units are dimmed so it is obvious who can still act
    if (!selectable && unit.attacksThisTurn >= unit.attacksAllowed()) {
        sf::RectangleShape veil({ bounds.width, bounds.height });
        veil.setPosition(bounds.left, bounds.top);
        veil.setFillColor(sf::Color(6, 6, 12, 110));
        target.draw(veil);
    }

    if (selected || targeted) {
        sf::RectangleShape glow({ bounds.width + 4.0f, bounds.height + 4.0f });
        glow.setPosition(bounds.left - 2.0f, bounds.top - 2.0f);
        glow.setFillColor(sf::Color::Transparent);
        glow.setOutlineThickness(2.5f);
        glow.setOutlineColor(targeted ? sf::Color(255, 96, 84) : sf::Color(255, 226, 130));
        target.draw(glow);
    } else if (selectable) {
        sf::RectangleShape hint({ bounds.width + 2.0f, bounds.height + 2.0f });
        hint.setPosition(bounds.left - 1.0f, bounds.top - 1.0f);
        hint.setFillColor(sf::Color::Transparent);
        hint.setOutlineThickness(1.5f);
        hint.setOutlineColor(sf::Color(190, 230, 255, 150));
        target.draw(hint);
    }
}

void drawEmptySlot(sf::RenderTarget& target, sf::FloatRect bounds, bool highlighted,
                   sf::Color accent) {
    sf::RectangleShape slot({ bounds.width, bounds.height });
    slot.setPosition(bounds.left, bounds.top);
    slot.setFillColor(highlighted ? sf::Color(accent.r / 4, accent.g / 4, accent.b / 4, 190)
                                  : sf::Color(16, 15, 22, 130));
    slot.setOutlineThickness(highlighted ? 2.0f : 1.0f);
    slot.setOutlineColor(highlighted ? accent : sf::Color(58, 54, 66, 170));
    target.draw(slot);
}

void drawCardBack(sf::RenderTarget& target, sf::FloatRect bounds, Side owner,
                  sf::Color accent, float alpha) {
    auto fade = [alpha](sf::Color c) {
        return sf::Color(c.r, c.g, c.b, static_cast<sf::Uint8>(c.a * alpha));
    };

    const std::string path = owner == Side::Opponent
        ? "assets/frames/card_back_2.png"
        : "assets/frames/card_back.png";
    if (ResourceManager::exists(path)) {
        const sf::Texture& back = ResourceManager::get().getTexture(path);
        sf::Sprite sprite;
        coverFit(sprite, back, { bounds.width, bounds.height });
        sprite.setPosition(bounds.left + bounds.width / 2.0f,
                           bounds.top + bounds.height / 2.0f);
        sprite.setColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(255 * alpha)));
        target.draw(sprite);
        return;
    }

    // Fallback: a plate and two rules, enough to read as a card back at 42px.
    sf::RectangleShape body({ bounds.width, bounds.height });
    body.setPosition(bounds.left, bounds.top);
    body.setFillColor(fade(sf::Color(20, 19, 26, 250)));
    body.setOutlineThickness(1.2f);
    body.setOutlineColor(fade(sf::Color(accent.r, accent.g, accent.b, 170)));
    target.draw(body);

    sf::RectangleShape plate({ bounds.width - 12.0f, bounds.height - 16.0f });
    plate.setPosition(bounds.left + 6.0f, bounds.top + 8.0f);
    plate.setFillColor(sf::Color::Transparent);
    plate.setOutlineThickness(1.0f);
    plate.setOutlineColor(fade(sf::Color(accent.r, accent.g, accent.b, 90)));
    target.draw(plate);

    for (int i = 0; i < 2; ++i) {
        sf::RectangleShape bar({ bounds.width - 26.0f, 1.5f });
        bar.setPosition(bounds.left + 13.0f, bounds.top + bounds.height * (0.42f + i * 0.14f));
        bar.setFillColor(fade(sf::Color(accent.r, accent.g, accent.b, 120)));
        target.draw(bar);
    }
}

void drawAvatar(sf::RenderTarget& target, const std::string& path,
                sf::FloatRect bounds, sf::Color accent, float bias) {
    const sf::Texture& tex = ResourceManager::get().getTexture(path);
    if (tex.getSize().x > 1) {
        const float tw = static_cast<float>(tex.getSize().x);
        const float th = static_cast<float>(tex.getSize().y);
        const float scale = std::max(bounds.width / tw, bounds.height / th);
        const int visW = static_cast<int>(bounds.width / scale);
        const int visH = static_cast<int>(bounds.height / scale);

        const int maxTop = std::max(0, static_cast<int>(th) - visH);
        const int top = std::min(maxTop, static_cast<int>(maxTop * 2.0f * bias));

        sf::Sprite sprite(tex);
        sprite.setTextureRect(sf::IntRect((static_cast<int>(tw) - visW) / 2, top, visW, visH));
        sprite.setScale(scale, scale);
        sprite.setPosition(bounds.left, bounds.top);
        target.draw(sprite);
    } else {
        sf::RectangleShape blank({ bounds.width, bounds.height });
        blank.setPosition(bounds.left, bounds.top);
        blank.setFillColor(sf::Color(30, 26, 34, 240));
        target.draw(blank);
    }

    sf::RectangleShape edge({ bounds.width, bounds.height });
    edge.setPosition(bounds.left, bounds.top);
    edge.setFillColor(sf::Color::Transparent);
    edge.setOutlineThickness(1.5f);
    edge.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, 200));
    target.draw(edge);
}

/// The border colour that marks a card as an armed counter-protocol.
sf::Color armedBorder() { return sf::Color(186, 128, 236); }

void drawTrapSlot(sf::RenderTarget& target, const sf::Font& font, const TrapCard* trap,
                  sf::FloatRect bounds, Side owner, bool reveal, bool highlighted) {
    if (!trap) {
        drawEmptySlot(target, bounds, highlighted, sf::Color(150, 140, 110));
        return;
    }

    const sf::Vector2f centre(bounds.left + bounds.width / 2.0f,
                              bounds.top + bounds.height / 2.0f);
    const sf::Vector2f size(bounds.width, bounds.height);

    if (!reveal) {
        // The opponent's counters stay sealed - that is the whole point of a
        // counter-protocol, and it is the only thing on this board that is
        // hidden from the player.
        sf::RectangleShape body(size);
        body.setPosition(bounds.left, bounds.top);
        body.setFillColor(sf::Color(34, 26, 20, 245));
        body.setOutlineThickness(1.5f);
        body.setOutlineColor(sf::Color(126, 104, 66));
        target.draw(body);
        drawCardBack(target, { bounds.left + 1.5f, bounds.top + 1.5f,
                               bounds.width - 3.0f, bounds.height - 3.0f },
                     owner, sf::Color(126, 104, 66));
        return;
    }

    // Your own counter is drawn as the card it is. Nothing about it is a secret
    // from you, and a face-down back in your own zone only meant you had to
    // remember what you set.
    //
    // Composed here rather than by calling drawCard: at 68px wide the rules box
    // cannot fit even at the minimum font, and it overflowed past the bottom of
    // the card. A slot this size only has to say WHICH counter is armed - the
    // rules are one click away in the inspector.
    const CardData& card = trap->data;
    const sf::Color accent = accentFor(card);

    sf::RectangleShape panel(size);
    panel.setPosition(bounds.left, bounds.top);
    panel.setFillColor(panelFor(card));
    target.draw(panel);

    // Art fills the top, leaving room for the name and the ribbon.
    const float ribbonH = 15.0f;
    const float nameH = 22.0f;
    const float artH = size.y - ribbonH - nameH;
    const sf::FloatRect artBox(bounds.left + 2.0f, bounds.top + 2.0f,
                               size.x - 4.0f, artH - 3.0f);

    const sf::Texture& art = ResourceManager::get().getTexture(card.textureFile);
    if (art.getSize().x > 1) {
        sf::Sprite sprite;
        coverFit(sprite, art, { artBox.width, artBox.height });
        sprite.setPosition(artBox.left + artBox.width / 2.0f,
                           artBox.top + artBox.height / 2.0f);
        target.draw(sprite);
    } else {
        drawArtPlaceholder(target, font, card,
                           { artBox.left + artBox.width / 2.0f,
                             artBox.top + artBox.height / 2.0f },
                           { artBox.width, artBox.height },
                           sf::RenderStates::Default, 255);
    }

    // A hairline in the doctrine colour under the art, as on a full card.
    sf::RectangleShape divider({ size.x - 8.0f, 1.0f });
    divider.setPosition(bounds.left + 4.0f, bounds.top + artH - 1.0f);
    divider.setFillColor(sf::Color(accent.r, accent.g, accent.b, 190));
    target.draw(divider);

    sf::Text name;
    name.setFont(Fonts::ui());
    name.setCharacterSize(9);
    name.setStyle(sf::Text::Bold);
    name.setFillColor(sf::Color(232, 226, 216));
    name.setString(TextUtils::wrap(card.name, Fonts::ui(), 9, size.x - 6.0f));
    TextUtils::centerHorizontally(name);
    name.setPosition(bounds.left + size.x / 2.0f, bounds.top + artH + 2.0f);
    target.draw(name);

    // KARDS-style status marking: the outer frame is replaced rather than
    // decorated, so "armed" reads at a glance without another icon on the board.
    const sf::Color armed = armedBorder();
    for (int layer = 0; layer < 2; ++layer) {
        const float grow = 1.0f + layer * 2.5f;
        sf::RectangleShape edge({ size.x + grow * 2.0f, size.y + grow * 2.0f });
        edge.setPosition(bounds.left - grow, bounds.top - grow);
        edge.setFillColor(sf::Color::Transparent);
        edge.setOutlineThickness(layer == 0 ? 2.2f : 1.0f);
        edge.setOutlineColor(sf::Color(armed.r, armed.g, armed.b, layer == 0 ? 255 : 90));
        target.draw(edge);
    }

    // The word, on a ribbon across the foot of the card.
    sf::RectangleShape ribbon({ size.x, ribbonH });
    ribbon.setPosition(bounds.left, bounds.top + size.y - ribbonH);
    ribbon.setFillColor(sf::Color(armed.r / 3, armed.g / 4, armed.b / 3, 248));
    target.draw(ribbon);

    sf::Text label;
    label.setFont(Fonts::ui());
    label.setString("ARMED");
    label.setCharacterSize(9);
    label.setStyle(sf::Text::Bold);
    label.setLetterSpacing(2.4f);
    label.setFillColor(sf::Color(238, 214, 255));
    TextUtils::centerBoth(label);
    label.setPosition(bounds.left + size.x / 2.0f, bounds.top + size.y - ribbonH / 2.0f - 1.0f);
    target.draw(label);
}

} // namespace CardArt
