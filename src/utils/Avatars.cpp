#include "utils/Avatars.hpp"
#include "utils/ResourceManager.hpp"
#include "utils/Settings.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Avatars {
namespace {

const char* kFolder = "assets/avatars";

std::vector<std::string> g_pool;
bool g_scanned = false;

bool isImage(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp";
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

void refresh() {
    g_pool.clear();
    g_scanned = true;

    std::error_code ec;
    if (!std::filesystem::is_directory(kFolder, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(kFolder, ec)) {
        if (!entry.is_regular_file(ec) || !isImage(entry.path())) continue;
        // Forward slashes throughout: the rest of the game builds asset paths
        // that way and the texture cache keys on the exact string.
        g_pool.push_back(std::string(kFolder) + "/" + entry.path().filename().string());
    }
    std::sort(g_pool.begin(), g_pool.end());
}

const std::vector<std::string>& pool() {
    if (!g_scanned) refresh();
    return g_pool;
}

std::string label(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return name;
}

std::string forPlayer(MechRole primary) {
    const auto& all = pool();

    // An explicit pick wins, as long as the file is still there.
    const std::string& pick = Settings::get().avatar;
    if (!pick.empty()) {
        const std::string full = std::string(kFolder) + "/" + pick;
        if (std::find(all.begin(), all.end(), full) != all.end()) return full;
    }

    // Otherwise the one named after the doctrine.
    // Two stems, because the doctrine has two names. `paladin.jpg` is what the
    // player already dropped in; `arclight.jpg` is what they would reach for
    // now that the screen says Arclight. Both find the same doctrine.
    const std::string wanted = lower(toString(primary));
    const std::string alsoWanted = lower(displayName(primary));
    for (const std::string& path : all) {
        const std::string stem = lower(label(path));
        if (stem == wanted || stem == alsoWanted) return path;
    }

    if (!all.empty()) return all.front();

    // Nothing in the folder at all: the legacy portraits still exist.
    const std::string legacy = "assets/portraits/" + wanted + ".png";
    if (ResourceManager::exists(legacy)) return legacy;
    return "assets/portraits/paladin.png";
}

void choose(const std::string& path) {
    if (path.empty()) {
        Settings::get().avatar.clear();
    } else {
        const std::size_t slash = path.find_last_of("/\\");
        Settings::get().avatar = slash == std::string::npos ? path : path.substr(slash + 1);
    }
    Settings::get().save();
}

std::string chosen() { return Settings::get().avatar; }

} // namespace Avatars
