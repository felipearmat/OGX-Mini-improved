// Minimal host-side test harness (OGX-Mini-improved). No external dependencies so the
// tests build anywhere a C++20 compiler exists (CI, distrobox, a plain PC).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int run_all() {
    int failed_cases = 0;
    for (const auto& c : registry()) {
        const int before = failures();
        c.fn();
        const bool ok = failures() == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
        failed_cases += ok ? 0 : 1;
    }
    std::printf("%zu tests, %d failed\n", registry().size(), failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace test

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)

#define TEST(name)                                                                     \
    static void TEST_CONCAT(test_fn_, name)();                                         \
    static test::Registrar TEST_CONCAT(test_reg_, name)(#name, TEST_CONCAT(test_fn_, name)); \
    static void TEST_CONCAT(test_fn_, name)()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);     \
            ++test::failures();                                                        \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        const auto _va = (a);                                                          \
        const auto _vb = (b);                                                          \
        if (!(_va == _vb)) {                                                           \
            std::printf("  %s:%d: CHECK_EQ(%s, %s) failed: %lld != %lld\n", __FILE__, __LINE__, \
                        #a, #b, static_cast<long long>(_va), static_cast<long long>(_vb)); \
            ++test::failures();                                                        \
        }                                                                              \
    } while (0)

#define TEST_MAIN() \
    int main() { return test::run_all(); }
