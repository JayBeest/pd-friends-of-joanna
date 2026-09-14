#!/bin/bash
# Audit Raf's ext-tex commits from perfect-dark-foj against pd-modloader.
#
# Usage:
#   ./tools/audit-raf.sh              # print to terminal
#   ./tools/audit-raf.sh -o report.txt  # save to file
#   ./tools/audit-raf.sh -v           # verbose (show full diffs)
#   ./tools/audit-raf.sh -v -o report.txt
#
# Raf's ext-tex commit range in foj (26 commits):
#   efa660a78  port-ext-tex: custom LOADTLUT command
#   ...
#   6072ec815  port-ext-tex: (last commit)

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

exec "$SCRIPT_DIR/fdiff" --repo "$REPO_DIR" audit \
  --source-repo "$FOJ_DIR" \
  --range 'efa660a78^..6072ec815' \
  --author Raf \
  --to WORKTREE \
  "$@"
