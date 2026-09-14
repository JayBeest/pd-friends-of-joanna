#!/bin/bash
# Audit Catherine Reprobate's pre-Raf modloader work from perfect-dark-foj
# against pd-modloader WORKTREE.
#
# Range: merge-base (246d73766) through end of Raf's ext-tex section (6072ec815)
# Author filter: only Catherine's commits (includes her 3 interleaved ext-tex commits)
#
# Usage:
#   audit-catherine-pre.sh                  # summary only
#   audit-catherine-pre.sh -v               # verbose (show diffs)
#   audit-catherine-pre.sh --rollup-only    # rollup comparison only

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PD_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
REPO_DIR="$(cd "$PD_ROOT/pd-modloader" && pwd)"
FOJ_DIR="$(cd "$PD_ROOT/perfect-dark-foj" && pwd)"

exec "$SCRIPT_DIR/fdiff" --repo "$REPO_DIR" audit \
  --source-repo "$FOJ_DIR" \
  --range '246d73766..6072ec815' \
  --author "Catherine Reprobate" \
  --to WORKTREE \
  "$@"
