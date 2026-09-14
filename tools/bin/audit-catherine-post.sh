#!/bin/bash
# Audit Catherine Reprobate's post-Raf integration work from perfect-dark-foj
# against pd-modloader WORKTREE.
#
# Range: after Raf's last ext-tex commit (6072ec815) through fojo-branchcleanup tip
# This includes ext-tex integration, texture handling updates, bio extensions, etc.
#
# Usage:
#   audit-catherine-post.sh                  # summary only
#   audit-catherine-post.sh -v               # verbose (show diffs)
#   audit-catherine-post.sh --rollup-only    # rollup comparison only

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PD_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
REPO_DIR="$(cd "$PD_ROOT/pd-modloader" && pwd)"
FOJ_DIR="$(cd "$PD_ROOT/perfect-dark-foj" && pwd)"

exec "$SCRIPT_DIR/fdiff" --repo "$REPO_DIR" audit \
  --source-repo "$FOJ_DIR" \
  --range '6072ec815..fojo-branchcleanup' \
  --author "Catherine Reprobate" \
  --to WORKTREE \
  "$@"
