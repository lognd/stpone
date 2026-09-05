# Bootstrap only. This is a frob-enabled repository: `frob check`, `frob
# test`, `frob format` are the workflow interface (see CLAUDE.md), and the
# firmware build/flash workflow is scripts/flash.py. Only the steps that
# cannot be a frob subcommand live here: installing the Python environment
# (which is what installs frob's own prerequisites) and wiping build state.

STAMP := .venv/.install-stamp

.PHONY: install clean

$(STAMP): pyproject.toml uv.lock
	uv sync
	@touch $(STAMP)

install: $(STAMP)

clean:
	rm -rf build/ dist/ .pytest_cache/ .ruff_cache/ .coverage htmlcov/ coverage.xml
	find . -type d -name __pycache__ -exec rm -rf {} + 2>/dev/null; true
	find . -type d -name "*.egg-info" -exec rm -rf {} + 2>/dev/null; true
