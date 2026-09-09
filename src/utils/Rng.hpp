#pragma once

#include <random>

/**
 * @brief Nguồn số ngẫu nhiên dùng chung cho toàn game.
 *
 * Trước đây Enemy và HolyVFX gọi rand() (không seed, chất lượng kém và
 * không thể tái lập khi debug). Toàn bộ gameplay giờ đi qua đây.
 */
class Rng {
public:
    static std::mt19937& engine() {
        static std::mt19937 gen(std::random_device{}());
        return gen;
    }

    /// Số nguyên trong đoạn [minVal, maxVal]
    static int range(int minVal, int maxVal) {
        if (minVal >= maxVal) return minVal;
        std::uniform_int_distribution<int> dist(minVal, maxVal);
        return dist(engine());
    }

    /// Số thực trong đoạn [minVal, maxVal]
    static float rangeF(float minVal, float maxVal) {
        std::uniform_real_distribution<float> dist(minVal, maxVal);
        return dist(engine());
    }

    /// Trả về true với xác suất percent%
    static bool chance(int percent) {
        return range(1, 100) <= percent;
    }

    /// Cố định seed (dùng cho unit test cần kết quả tái lập)
    static void seed(unsigned int value) {
        engine().seed(value);
    }
};
