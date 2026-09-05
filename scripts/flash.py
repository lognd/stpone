#!/usr/bin/env python3
"""Single operator entrypoint: build, attach the bootloader, flash.

Run from the repo with `uv run python scripts/flash.py [--target NAME]`.
The logic lives in the stpone.flash package so it is unit-tested; this file
only exists so there is one obvious thing to run.
"""

from __future__ import annotations

import sys

from stpone.flash.cli import main

if __name__ == "__main__":
    sys.exit(main())
