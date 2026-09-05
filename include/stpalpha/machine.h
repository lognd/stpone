#pragma once
// frob:waive PARSE002 reason="C++-only enum class header kept as .h to match the include \
// convention -- tree-sitter-c salvages around it"

#include <stdint.h>

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P1"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0301
enum class MachineState : uint8_t { IDLE, WINDING };

// 20x4 LCD frame buffer. rows[r][c], null-terminated at [20].
// docs/runbooks/hardware-tests.md P1, simulator evidence tests/system/test_firmware.py"
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P1"
// frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0301
struct LCDPacket {
    char rows[4][21];
};
