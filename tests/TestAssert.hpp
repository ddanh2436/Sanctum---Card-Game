#pragma once

#include <cstdio>
#include <cstdlib>

/**
 * @brief Kiểm tra điều kiện trong unit test.
 *
 * KHÔNG dùng assert() của <cassert>: cấu hình Release định nghĩa NDEBUG,
 * assert() bị biên dịch thành no-op và toàn bộ test sẽ "pass" một cách giả tạo.
 * CHECK luôn hoạt động ở mọi cấu hình build.
 */
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "\n[FAIL] %s:%d\n       Dieu kien: %s\n",       \
                         __FILE__, __LINE__, #cond);                             \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

/// Như CHECK nhưng kèm thông điệp giải thích
#define CHECK_MSG(cond, msg)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "\n[FAIL] %s:%d\n       %s\n       Dieu kien: %s\n", \
                         __FILE__, __LINE__, (msg), #cond);                      \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)
