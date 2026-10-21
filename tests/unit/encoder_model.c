/* Host model of the 40 kHz encoder poll: includes the REAL src/stpalpha/hal/setup.c (registers and
 * ISR macros come from tests/unit/avr_shim), synthesises a quadrature signal in continuous time and
 * samples it the way Timer1 does -- once per 400 cycles, each entry late by a random 0..jitter
 * cycles -- while the "main loop" is absent (the ISR is the only reader of the pins).
 *
 * usage: encoder_model <states_per_s> <jitter_cycles> <seed> <script>
 *   script: a string of segments: F<n> forward n states, R<n> reverse n states, S skip one state
 *   (an illegal double step) in the current direction; e.g. "F4000R2500S F100".
 *   optional 5th argument: arm enc_stop_at with the relay on (the ISR must drop it at that |count|).
 * Prints: count=<c> errors=<e> lat_max=<cycles> relay=<0|1> cut=<count|-1>
 */
/* hal_init_hook splices an AVR `call` into .init8; on the host that dead code is compiled out. */
#define __asm__ if (0) __asm__
#include "../../src/stpalpha/hal/setup.c"
#undef __asm__

#include <stdio.h>
#include <stdlib.h>

#define CPU_HZ 16000000ull
#define POLL_CYCLES 400ull

static const uint8_t kGray[4] = {0, 1, 3, 2}; /* (B << 1) | A: 00 01 11 10 */

static uint32_t g_rng = 12345;
static uint32_t rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        return 2;
    }
    const uint64_t state_cycles = CPU_HZ / (uint64_t)atol(argv[1]);
    const uint32_t jitter = (uint32_t)atol(argv[2]);
    g_rng = (uint32_t)atol(argv[3]);
    const char* script = argv[4];
    if (argc > 5) {
        enc_stop_at = (uint32_t)atol(argv[5]);
        g_reg_portb |= (uint8_t)(1 << RELAY_BIT);
    }

    /* Expand the script into the sequence of gray-code indices the shaft passes through. */
    static uint8_t path[400000];
    uint32_t n = 0;
    int idx = 0;
    int dir = 1;
    path[n++] = (uint8_t)idx;
    for (const char* p = script; *p != '\0';) {
        char op = *p++;
        long len = 1;
        if (op == 'F' || op == 'R') {
            len = strtol(p, (char**)&p, 10);
            dir = op == 'F' ? 1 : -1;
        } else if (op != 'S') {
            continue;
        }
        for (long i = 0; i < len; ++i) {
            idx = (idx + dir * (op == 'S' ? 2 : 1) + 4) % 4;
            path[n++] = (uint8_t)idx;
        }
    }

    /* Sample: poll k enters at k*400 + jitter_k; the pins show the state of that instant. */
    const uint64_t end = (uint64_t)n * state_cycles + 4 * POLL_CYCLES;
    for (uint64_t k = 0; k * POLL_CYCLES < end; ++k) {
        uint64_t entry = k * POLL_CYCLES + (jitter ? rnd() % (jitter + 1) : 0);
        uint64_t slot = entry / state_cycles;
        uint8_t st = kGray[path[slot < n ? slot : n - 1]];
        g_reg_pind = (st & 1) ? (uint8_t)(1 << ENC_BIT_A) : 0;
        g_reg_pinc = (st & 2) ? (uint8_t)(1 << ENC_BIT_B) : 0;
        g_reg_tcnt1 = (uint16_t)(entry - k * POLL_CYCLES + 39); /* entry latency + response and prologue */
        TIMER1_COMPA_vect();
    }
    uint32_t cut = 0;
    bool cut_valid = enc_take_cut(&cut);
    printf("count=%ld errors=%u lat_max=%u relay=%d cut=%ld\n", (long)enc_count,
           (unsigned)enc_errors, (unsigned)g_isr_lat_max, (g_reg_portb >> RELAY_BIT) & 1,
           cut_valid ? (long)cut : -1L);
    return 0;
}
