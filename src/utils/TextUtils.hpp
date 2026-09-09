#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <sstream>
#include <vector>

namespace TextUtils {

/**
 * @brief Wrap `text` so no line exceeds maxWidth pixels at the given size.
 *
 * Card rules text comes from cards.json as one long line; drawn as-is it
 * runs straight off the 150px card.
 */
inline std::string wrap(const std::string& text, const sf::Font& font,
                        unsigned int charSize, float maxWidth) {
    if (text.empty()) return text;

    sf::Text probe;
    probe.setFont(font);
    probe.setCharacterSize(charSize);

    std::string result;
    std::string line;

    // Preserve any newlines the author already put in
    std::istringstream paragraphs(text);
    std::string paragraph;
    bool firstParagraph = true;

    while (std::getline(paragraphs, paragraph)) {
        if (!firstParagraph) result += '\n';
        firstParagraph = false;
        line.clear();

        std::istringstream words(paragraph);
        std::string word;
        while (words >> word) {
            std::string candidate = line.empty() ? word : line + " " + word;
            probe.setString(candidate);
            if (probe.getLocalBounds().width > maxWidth && !line.empty()) {
                result += line + "\n";
                line = word;
            } else {
                line = candidate;
            }
        }
        result += line;
    }

    return result;
}

/// Centre horizontally; the origin sits on the top edge of the string
inline void centerHorizontally(sf::Text& text) {
    sf::FloatRect b = text.getLocalBounds();
    text.setOrigin(b.left + b.width / 2.0f, b.top);
}

/// Centre on both axes, so setPosition() places the visual middle of the text
inline void centerBoth(sf::Text& text) {
    sf::FloatRect b = text.getLocalBounds();
    text.setOrigin(b.left + b.width / 2.0f, b.top + b.height / 2.0f);
}

} // namespace TextUtils
