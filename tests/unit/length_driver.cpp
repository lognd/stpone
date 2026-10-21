// Host driver for include/stpalpha/length.h: reads "<pulses> <wheel_um> <cpr> <target_pulses>" lines
// and prints the tenths, the formatted LEN fields, the two RESULT rows (settled and settling) and
// the progress row.
#include <stdio.h>

#include <stpalpha/length.h>

using namespace stpalpha;

int main() {
    unsigned long pulses = 0, wheel = 0, cpr = 0, target = 0;
    while (scanf("%lu %lu %lu %lu", &pulses, &wheel, &cpr, &target) == 4) {
        uint32_t f = pulses_to_tenths(static_cast<uint32_t>(pulses), static_cast<uint32_t>(wheel),
                                      static_cast<uint32_t>(cpr));
        char len[12];
        char r1[21];
        char r2[21];
        char s1[21];
        char s2[21];
        format_tenths(len, sizeof(len), f, 4);
        char bar[21];
        format_result(r1, r2, sizeof(r1), false, static_cast<uint32_t>(pulses),
                      static_cast<uint32_t>(target), static_cast<uint32_t>(wheel),
                      static_cast<uint32_t>(cpr));
        format_result(s1, s2, sizeof(s1), true, static_cast<uint32_t>(pulses),
                      static_cast<uint32_t>(target), static_cast<uint32_t>(wheel),
                      static_cast<uint32_t>(cpr));
        format_progress(bar, sizeof(bar),
                        percent_done(static_cast<uint32_t>(pulses), static_cast<uint32_t>(target)));
        printf("%lu|%s|%s|%s|%s|%s|%s\n", static_cast<unsigned long>(f), len, r1, r2, s1, s2, bar);
    }
    return 0;
}
