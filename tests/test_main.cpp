#include "testfw.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/common/log.h"

int main() {
    int failedTests = 0;
    const char* filter = std::getenv("LBN_TEST_FILTER");
    if (std::getenv("LBN_TEST_LOG")) lbn::logOpen("lbn_tests.log", true);
    for (auto& t : testRegistry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = testFailures();
        t.fn();
        bool ok = testFailures() == before;
        if (!ok) failedTests++;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%zu testes, %d falharam, %d verificacoes falharam\n", testRegistry().size(), failedTests, testFailures());
    return failedTests == 0 ? 0 : 1;
}
