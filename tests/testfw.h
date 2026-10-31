// Tiny test framework (original code for LBN Coop SA).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase { const char* name; std::function<void()> fn; };
inline std::vector<TestCase>& testRegistry() { static std::vector<TestCase> r; return r; }
inline int& testFailures() { static int f = 0; return f; }
struct TestReg { TestReg(const char* n, std::function<void()> f) { testRegistry().push_back({n, std::move(f)}); } };

#define TEST(name) static void name(); static TestReg reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { std::printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); testFailures()++; } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (eps))) { \
    std::printf("  FALHOU %s:%d: %s ~= %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, _a, _b); testFailures()++; } } while (0)
