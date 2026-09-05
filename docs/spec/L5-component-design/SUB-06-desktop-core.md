# L5 -- SUB-06 desktop-core

Satisfies SUB-06. Owned code: `src/stpone/logging/**`,
`src/stpone/common/**`, `src/stpone/exception.py`, `src/stpone/__init__.py`.

## Components

| ID | Component | Path | Responsibility | satisfies | verified-by | Milestone |
|---|---|---|---|---|---|---|
| COMP-0601 | logging | src/stpone/logging/logger.py, src/stpone/logging/formatter.py, src/stpone/logging/filter.py, src/stpone/logging/config.toml | `get_logger` (lazy dictConfig from config.toml), `error_ctx`, `SimpleFormatter` (level prefix at WARNING+), `BelowLevelFilter` (stdout below WARNING, stderr at and above). | SUB-06, SYS-030 | UT-0601 | M1 |
| COMP-0602 | common and exceptions | src/stpone/common/__init__.py, src/stpone/exception.py | typani `Result`/`Ok`/`Err`/`Unreachable` re-exported as the package vocabulary; `DeveloperException` for programmer errors. | SUB-06 | UT-0602 | M1 |

### comp-0601
<!-- frob:describes src/stpone/logging/logger.py::get_logger -->
<!-- frob:describes src/stpone/logging/logger.py::error_ctx -->
<!-- frob:describes src/stpone/logging/formatter.py::SimpleFormatter -->
<!-- frob:describes src/stpone/logging/formatter.py::SimpleFormatter.format -->
<!-- frob:describes src/stpone/logging/filter.py::BelowLevelFilter -->
<!-- frob:describes src/stpone/logging/filter.py::BelowLevelFilter.filter -->

The house layout from the frob scaffold; `error_ctx` is carried over
from `stp-upgrade` for the serial package's error paths.

### comp-0602
<!-- frob:describes src/stpone/exception.py::DeveloperException -->

The homebrew `Result`, `Empty`, `Unreachable` and `Singleton` of
`stp-upgrade` are replaced by typani's (same API surface: `is_ok`,
`danger_ok`, `map`, `and_then`...), removing a duplicated copy.

## Unit test plan

| ID | Case | verifies | runnable |
|---|---|---|---|
| UT-0601 | Filter passes below threshold only; formatter prefixes at WARNING+; `get_logger` returns a Logger; `error_ctx` logs and re-raises. | COMP-0601 | tests/unit/test_logging.py::test_error_ctx_logs_and_reraises |
| UT-0602 | `stpone` exports resolve to typani's types and `DeveloperException` is an `Exception`. | COMP-0602 | tests/integration/test_wiring.py::test_logging_and_exception_wiring |
