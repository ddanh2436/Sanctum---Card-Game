#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <iostream>

class ResourceManager {
private:
    std::map<std::string, sf::Texture> m_textures;
    std::map<std::string, sf::SoundBuffer> m_soundBuffers;
    std::map<std::string, sf::Font> m_fonts;

    ResourceManager() = default;

public:
    static ResourceManager& get() {
        static ResourceManager instance;
        return instance;
    }

    // Không cho phép copy
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    /**
     * @brief The given path, or the same file under a different image format.
     *
     * Art gets replaced by hand and the replacement is rarely the same format:
     * dropping menu_bg.jpg in over menu_bg.png left the menu loading a file
     * that no longer existed and rendering nothing, with the only clue a line
     * on stderr. Resolving the extension means the game finds the art the
     * author actually supplied instead of the one the code was written against.
     */
    /// Strict: this exact path, no extension substitution. resolve() has to use
    /// this rather than exists(), which is deliberately tolerant - asking the
    /// tolerant one "is menu_bg.png here?" answers yes on the strength of
    /// menu_bg.jpg, and resolve then hands back the path that does not exist.
    static bool existsExact(const std::string& filepath) {
        std::error_code ec;
        return !filepath.empty() && std::filesystem::exists(filepath, ec);
    }

    static std::string resolve(const std::string& filepath) {
        if (filepath.empty() || existsExact(filepath)) return filepath;

        const std::size_t dot = filepath.find_last_of('.');
        if (dot == std::string::npos) return filepath;
        const std::string stem = filepath.substr(0, dot);

        for (const char* ext : { ".png", ".jpg", ".jpeg", ".webp", ".bmp" }) {
            const std::string candidate = stem + ext;
            if (candidate != filepath && existsExact(candidate)) return candidate;
        }
        return filepath;
    }

    /// True when the file is on disk under any supported image extension.
    /// getTexture() substitutes a magenta placeholder for anything missing, so
    /// optional artwork must be probed with this before it is drawn.
    static bool exists(const std::string& filepath) {
        std::error_code ec;
        if (!filepath.empty() && std::filesystem::exists(filepath, ec)) return true;

        const std::size_t dot = filepath.find_last_of('.');
        if (dot == std::string::npos) return false;
        const std::string stem = filepath.substr(0, dot);
        for (const char* ext : { ".png", ".jpg", ".jpeg", ".webp", ".bmp" }) {
            if (std::filesystem::exists(stem + ext, ec)) return true;
        }
        return false;
    }

    sf::Texture& getTexture(const std::string& requested) {
        auto it = m_textures.find(requested);
        if (it != m_textures.end()) {
            return it->second;
        }
        // Cache under what the caller asked for, load from what is on disk.
        const std::string filepath = resolve(requested);

        sf::Texture texture;
        if (!texture.loadFromFile(filepath)) {
            std::cerr << "[ResourceManager] Warning: texture not found: " << filepath << std::endl;
            // Tạo texture 1x1 magenta mặc định để tránh crash
            sf::Image fallbackImg;
            fallbackImg.create(64, 64, sf::Color(180, 50, 180));
            texture.loadFromImage(fallbackImg);
        }
        texture.setSmooth(true);
        m_textures[requested] = std::move(texture);
        return m_textures[requested];
    }

    sf::SoundBuffer& getSoundBuffer(const std::string& filepath) {
        auto it = m_soundBuffers.find(filepath);
        if (it != m_soundBuffers.end()) {
            return it->second;
        }

        sf::SoundBuffer buffer;
        if (!buffer.loadFromFile(filepath)) {
            std::cerr << "[ResourceManager] Warning: sound not found: " << filepath << std::endl;
        }
        m_soundBuffers[filepath] = std::move(buffer);
        return m_soundBuffers[filepath];
    }

    sf::Font& getFont(const std::string& filepath) {
        auto it = m_fonts.find(filepath);
        if (it != m_fonts.end()) {
            return it->second;
        }

        sf::Font font;
        if (!font.loadFromFile(filepath)) {
            std::cerr << "[ResourceManager] Warning: font not found: " << filepath << std::endl;
        }
        m_fonts[filepath] = std::move(font);
        return m_fonts[filepath];
    }

    void clearAll() {
        m_textures.clear();
        m_soundBuffers.clear();
        m_fonts.clear();
    }
};
