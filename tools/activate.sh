# Friends of Joanna tool activation. Idempotent; safe to re-source.
# Usage: source /path/to/tools/activate.sh

_fojo_tools_src="${BASH_SOURCE[0]:-${(%):-%N}}"
_fojo_tools_root="$(cd "$(dirname "$_fojo_tools_src")" && pwd)"
unset _fojo_tools_src

if [ "$FOJO_TOOLS_ROOT" = "$_fojo_tools_root" ]; then
    echo "fojo tools already active at $_fojo_tools_root"
    unset _fojo_tools_root
    return 0 2>/dev/null || exit 0
fi

_FOJO_TOOLS_OLD_PATH="$PATH"
_FOJO_TOOLS_OLD_PDTOOLS="${PDTOOLS:-}"
_FOJO_TOOLS_OLD_FOJO_TOOLS_ROOT="${FOJO_TOOLS_ROOT:-}"
_FOJO_TOOLS_OLD_PD="${PD:-}"

for b in "$_fojo_tools_root/bin" "$_fojo_tools_root/pdtools/bin"; do
    [ -d "$b" ] || continue
    case ":$PATH:" in
        *":$b:"*) ;;
        *) PATH="$b:$PATH" ;;
    esac
done
export PATH

export FOJO_TOOLS_ROOT="$_fojo_tools_root"
export PDTOOLS="$_fojo_tools_root/pdtools"

_fojo_tools_pd_candidate="$(cd "$_fojo_tools_root/.." 2>/dev/null && pwd)"
if [ -z "${PD:-}" ] && [ -f "$_fojo_tools_pd_candidate/stagetable.txt" ] && [ -f "$_fojo_tools_pd_candidate/checksums.ntsc-beta.md5" ]; then
    export PD="$_fojo_tools_pd_candidate"
fi
unset _fojo_tools_pd_candidate

deactivate_fojo_tools() {
    export PATH="$_FOJO_TOOLS_OLD_PATH"
    if [ -n "$_FOJO_TOOLS_OLD_PDTOOLS" ]; then export PDTOOLS="$_FOJO_TOOLS_OLD_PDTOOLS"; else unset PDTOOLS; fi
    if [ -n "$_FOJO_TOOLS_OLD_FOJO_TOOLS_ROOT" ]; then export FOJO_TOOLS_ROOT="$_FOJO_TOOLS_OLD_FOJO_TOOLS_ROOT"; else unset FOJO_TOOLS_ROOT; fi
    if [ -n "$_FOJO_TOOLS_OLD_PD" ]; then export PD="$_FOJO_TOOLS_OLD_PD"; else unset PD; fi
    unset _FOJO_TOOLS_OLD_PATH _FOJO_TOOLS_OLD_PDTOOLS _FOJO_TOOLS_OLD_FOJO_TOOLS_ROOT _FOJO_TOOLS_OLD_PD
    unset -f deactivate_fojo_tools
    echo "fojo tools deactivated"
}

unset _fojo_tools_root
echo "fojo tools active"
echo "  FOJO_TOOLS_ROOT = $FOJO_TOOLS_ROOT"
echo "  PDTOOLS         = $PDTOOLS"
if [ -n "${PD:-}" ]; then
    echo "  PD              = $PD"
fi
echo "run  deactivate_fojo_tools  to restore"