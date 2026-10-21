// Host run of tests/unit/render_cases.h: one line per case on stdout.
#include <stdio.h>

#include "render_cases.h"

int main() {
    render_cases::run([](const char* line) { puts(line); });
    return 0;
}
