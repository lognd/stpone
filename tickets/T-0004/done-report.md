## Done report

The HAL is C with avr-libc's crt and ISR() bindings (DEC-001): the vector table, .init8 boot hook and BADISR fallback are verified by disassembly, every image builds through scripts/flash.py, hello_world prints under simavr, and the float32 length model recomputes to 165011 pulses (UT-0101..0105, UT-0201, UT-0202, UT-0301, UT-0302, UT-0401, UT-0402).

### Changed
```
 CLAUDE.md                                          |   4 +-
 CMakeLists.txt                                     | 142 +++++++++++
 README.md                                          |   8 +-
 docs/decisions/DEC-001-avr-libc-startup.md         |  51 ++++
 docs/decisions/DEC-002-m1-port-bootstrap.md        |  31 +++
 docs/guides/agent-playbook.md                      |  45 ++++
 docs/hardware/pinout.md                            |  30 +++
 docs/index.md                                      |  47 ++++
 docs/runbooks/flash.md                             |  57 +++++
 docs/runbooks/hardware-tests.md                    |  62 +++++
 docs/spec/L1-customer-tests.md                     |  29 +++
 docs/spec/L1-requirements.md                       |  66 +++++
 docs/spec/L2-customer-test-plan.md                 |  31 +++
 docs/spec/L2-requirement-specification.md          |  55 +++++
 docs/spec/L3-system-integration-test-plan.md       |  18 ++
 docs/spec/L3-system-specification.md               |  40 +++
 docs/spec/L4-subsystem-integration-test-plans.md   |  15 ++
 docs/spec/L4-system-design.md                      |  30 +++
 .../L5-component-design/SUB-01-firmware-hal.md     |  69 ++++++
 .../L5-component-design/SUB-02-firmware-lcd.md     |  36 +++
 .../L5-component-design/SUB-03-firmware-app.md     |  29 +++
 .../SUB-04-hardware-harnesses.md                   |  25 ++
 docs/spec/L5-component-design/SUB-05-flash-tool.md |  83 +++++++
 .../L5-component-design/SUB-06-desktop-core.md     |  36 +++
 .../L5-component-design/SUB-07-serial-protocol.md  | 106 ++++++++
 docs/spec/L5-component-design/SUB-08-process.md    |  22 ++
 docs/spec/README.md                                |  70 ++++++
 frob.toml                                          |  16 ++
 include/stpalpha/hal/lcd.h                         |  23 ++
 include/stpalpha/hal/setup.h                       |  74 ++++++
 include/stpalpha/machine.h                         |  16 ++
 pyproject.toml                                     |   6 +
 scripts/flash.py                                   |  16 ++
 src/stpalpha/hal/lcd.c                             | 170 +++++++++++++
 src/stpalpha/hal/setup.c                           | 275 +++++++++++++++++++++
 src/stpalpha/main.cpp                              | 211 ++++++++++++++++
 src/stpone/__init__.py                             |  18 ++
 src/stpone/common/__init__.py                      |   8 +
 src/stpone/exception.py                            |   7 +
 src/stpone/flash/__init__.py                       |   9 +
 src/stpone/flash/app.py                            |  90 +++++++
 src/stpone/flash/avrdude.py                        |  53 ++++
 src/stpone/flash/build.py                          |  87 +++++++
 src/stpone/flash/cli.py                            |  97 ++++++++
 src/stpone/flash/config.py                         |  79 ++++++
 src/stpone/flash/errors.py                         |  50 ++++
 src/stpone/flash/host.py                           | 138 +++++++++++
 src/stpone/flash/port.py                           |  39 +++
 src/stpone/flash/proc.py                           |  45 ++++
 src/stpone/logging/__init__.py                     |   5 +
 src/stpone/logging/config.toml                     |  30 +++
 src/stpone/logging/filter.py                       |  26 ++
 src/stpone/logging/formatter.py                    |  24 ++
 src/stpone/logging/logger.py                       |  49 ++++
 src/stpone/py.typed                                |   0
 src/stpone/serial/__init__.py                      |   9 +
 src/stpone/serial/packets/__init__.py              |   9 +
 src/stpone/serial/packets/floats.py                | 103 ++++++++
 src/stpone/serial/packets/generic.py               | 185 ++++++++++++++
 src/stpone/serial/packets/handshake.py             |  57 +++++
 src/stpone/serial/packets/integers.py              | 162 ++++++++++++
 src/stpone/serial/packets/string.py                |  89 +++++++
 src/stpone/serial/rdt.py                           |  39 +++
 tests/avr/hello_world.cpp                          |  33 +++
 tests/avr/test_buttons_leds.cpp                    |  23 ++
 tests/avr/test_buzzer.cpp                          |  70 ++++++
 tests/avr/test_encoder.cpp                         |  97 ++++++++
 tests/avr/test_lcd.cpp                             | 107 ++++++++
 tests/avr/test_leds_pins.cpp                       |  98 ++++++++
 tests/avr/test_timer0.cpp                          |  86 +++++++
 tests/conftest.py                                  |  66 +++++
 tests/integration/test_flash_cli.py                |  51 ++++
 tests/integration/test_serial_stream.py            | 124 ++++++++++
 tests/integration/test_wiring.py                   |  40 +++
 tests/system/test_firmware.py                      | 116 +++++++++
 tests/system/test_package.py                       |  25 ++
 tests/system/test_process.py                       | 102 ++++++++
 tests/unit/test_flash_app.py                       |  85 +++++++
 tests/unit/test_flash_build.py                     |  95 +++++++
 tests/unit/test_flash_config.py                    |  44 ++++
 tests/unit/test_flash_host.py                      |  99 ++++++++
 tests/unit/test_flash_parser.py                    |  35 +++
 tests/unit/test_flash_port.py                      |  32 +++
 tests/unit/test_flash_proc.py                      |  26 ++
 tests/unit/test_length_model.py                    |  32 +++
 tests/unit/test_logging.py                         |  50 ++++
 tests/unit/test_serial_generic.py                  |  96 +++++++
 tests/unit/test_serial_packets.py                  | 176 +++++++++++++
 tickets/T-0001/ticket.md                           | 139 ++++++++++-
 tickets/T-0002/done-report.md                      | 110 +++++++++
 tickets/T-0002/ticket.md                           |  17 +-
 tickets/T-0003/done-report.md                      | 113 +++++++++
 tickets/T-0003/ticket.md                           |  19 +-
 tickets/T-0004/ticket.md                           |  19 +-
 tickets/T-0006/ticket.md                           |  30 +++
 tickets/T-0007/ticket.md                           |  31 +++
 96 files changed, 5818 insertions(+), 19 deletions(-)
```

### Evidence
- `tests/system/test_firmware.py::test_firmware_vector_table_binds_timer_isrs` (pytest node id, verified passing when recorded)
- `tests/system/test_firmware.py::test_hal_init_runs_before_main` (pytest node id, verified passing when recorded)
- `tests/system/test_firmware.py::test_hello_world_prints_on_usart1_in_simavr` (pytest node id, verified passing when recorded)
- `tests/unit/test_length_model.py::test_target_pulses_match_firmware_constants` (pytest node id, verified passing when recorded)

### Captured claims
- tests: 4 passed (from 4 evidence id(s))
- gates: 1 error(s), 28 warning(s), 44 waived
- error-findings: TEST006@.frob/coverage-stamp
