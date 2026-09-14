#!/bin/bash
# Rollup + integration audit of Raf's ext-tex work.
#
# Shows only the rollup comparison (identical / integrated / missing)
# with full unified diffs for diverged functions. Skips per-commit detail.
#
# Usage:
#   ./tools/audit-raf-rollup.sh                    # color output in pager
#   ./tools/audit-raf-rollup.sh -o report.diff     # save to file
#   ./tools/audit-raf-rollup.sh --no-pager         # print to stdout, no pager
#   ./tools/audit-raf-rollup.sh -o report.diff --no-pager
#
# Pipe the saved file through standard diff tools:
#   filterdiff report.diff | delta
#   filterdiff -i '*/ext_tex.c' report.diff
#   vim -R -c 'set ft=diff' report.diff

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PD_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
REPO_DIR="$(cd "$PD_ROOT/pd-modloader" && pwd)" || {
  echo "Error: pd-modloader not found at $PD_ROOT/pd-modloader" >&2
  exit 1
}
FOJ_DIR="$(cd "$PD_ROOT/perfect-dark-foj" && pwd 2>/dev/null)" || {
  echo "Error: perfect-dark-foj not found at $PD_ROOT/perfect-dark-foj" >&2
  echo "Set FOJ_DIR to override." >&2
  exit 1
}

# Parse our own flags (--no-pager), pass the rest through
USE_PAGER=1
EXTRA_ARGS=()
for arg in "$@"; do
  case "$arg" in
    --no-pager) USE_PAGER=0 ;;
    *) EXTRA_ARGS+=("$arg") ;;
  esac
done

FDIFF_CMD=(
  "$SCRIPT_DIR/fdiff" --repo "$REPO_DIR" audit
  --source-repo "$FOJ_DIR"
  --range 'efa660a78^..6072ec815'
  --author Raf
  --to WORKTREE
  --rollup-only
  --verbose
  ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}
)

if [[ $USE_PAGER -eq 1 ]] && [ -t 1 ]; then
  # Interactive terminal — use a pager with color support
  "${FDIFF_CMD[@]}" | less -R
else
  "${FDIFF_CMD[@]}"
fi
