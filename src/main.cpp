#include "core/Engine.hpp"
#include <iostream>

int main() {
    std::cout << "========================================\n";
    std::cout << " SANCTUM: OATH OF LIGHT (SFML C++)\n";
    std::cout << "========================================\n";

    Engine engine;
    if (!engine.init()) {
        std::cerr << "Error: failed to start the game engine!\n";
        return -1;
    }

    engine.run();
    return 0;
}
