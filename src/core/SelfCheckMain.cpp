// SelfCheckMain.cpp — entry point for the Phase L0 portable-core self-check.
// Runs the cross-platform smoke checks and reports the result. Exit 0 == the
// portable core compiled, linked, and behaves on this platform.

#include <cstdio>

namespace arcade_core_smoke { int run_self_check(); }

int main() {
    int rc = arcade_core_smoke::run_self_check();
    if (rc == 0) {
        std::printf("arcade_core self-check: OK\n");
    } else {
        std::printf("arcade_core self-check: FAILED (code %d)\n", rc);
    }
    return rc;
}
