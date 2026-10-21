#pragma once
// frob:waive PARSE002 reason="C++-only enum class header kept as .h to match the include convention, tree-sitter-c salvages around it"

#include <stdint.h>

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P1"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0301
enum class MachineState : uint8_t { IDLE, WINDING, PAUSED, TRIM };

// frob:waive TEST001 reason="enum type with no behaviour of its own -- bench evidence docs/runbooks/hardware-tests.md P11"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0301
// How the last panel wind ended (DEC-010): NONE while running, never run, or in legacy mode;
// PASS = settled |count| >= goal; SHORT = the trim gave up; STOPPED = the operator pushed BTN1/BTN2.
enum class Outcome : uint8_t { NONE, PASS, SHORT, STOPPED };

// 20x4 LCD frame buffer. rows[r][c], null-terminated at [20].
// docs/runbooks/hardware-tests.md P1, simulator evidence tests/system/test_firmware.py"
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P1"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0301
struct LCDPacket {
    char rows[4][21];
};
