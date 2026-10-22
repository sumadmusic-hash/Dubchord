#pragma once
// Leichtgewichts-Testrunner ohne externe Abhaengigkeiten.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace tf {

struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks()   { static int c = 0; return c; }

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TF_TEST(fn) \
    static void fn(); \
    static ::tf::Registrar reg_##fn(#fn, &fn); \
    static void fn()

#define TF_CHECK(cond) do { \
    ++::tf::checks(); \
    if (!(cond)) { ++::tf::failures(); \
        std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define TF_CHECK_NEAR(a, b, eps) do { \
    ++::tf::checks(); \
    double _va = (a), _vb = (b); \
    if (!(std::fabs(_va - _vb) <= (eps))) { ++::tf::failures(); \
        std::printf("    FAIL %s:%d: |%.9g - %.9g| > %g\n", \
                    __FILE__, __LINE__, _va, _vb, (double)(eps)); } \
} while (0)

inline int runAll(const char* suiteName) {
    std::printf("[RUN ] %s (%zu tests)\n", suiteName, registry().size());
    for (auto& c : registry()) {
        const int before = failures();
        std::printf("  - %s\n", c.name);
        c.fn();
        std::printf("    %s\n", failures() == before ? "ok" : "FAILED");
    }
    return failures();
}

} // namespace tf
