#!/usr/bin/env python3

import os
import json
import sys
import subprocess
import tempfile
import logging
from datetime import datetime
import json
from pathlib import Path

# Logging goes to stderr so it never pollutes a tool's stdout payload.
# Default level is WARNING; callers (e.g. `pdt -v`) can crank it up.
logging.basicConfig(
    level=os.environ.get("LEYLINELIB_LOG_LEVEL", "WARNING"),
    handlers=[logging.StreamHandler(sys.stderr)]
)
from pathlib import Path


def _opt_env(name, default=""):
    """Return env var ``name`` (stripped) or ``default``; never raise.

    This module used to ``assert`` on every required-ish env var, which
    made *importing* leylinelib fail on machines that only wanted to run
    the ROM/asset tools. Most callers never touch the GEPD/Mouseinjector
    bundle paths; treat them as optional and let downstream code raise
    iff it actually needs the value.
    """
    val = os.getenv(name)
    if val is None:
        return default
    return val.strip() or default


def _warn_if_unset(name, val):
    if not val:
        logging.warning(
            "leylinelib: env var %s is not set; "
            "operations that need it will fail at point of use.", name)
    return val


# Optional: GEPD bundle / 1964 install root. Only required by the
# bundle-management tools; ROM/asset tools don't touch it.
GEPD = _opt_env("GEPD_TARGET_HOST") or _opt_env("GEPD_TARGET")

# Optional: Perfect Dark source root. Many tools accept --pd-src or work
# from cwd; this is a fallback for legacy code paths.
PD = _opt_env("PD_HOST") or _opt_env("PD")

# everywhere just kind of assumes ntsc-final
ROMID = _opt_env("ROMID", "ntsc-final")

# GEPD save / plugin locations (only meaningful when GEPD is set).
gepd_save_dir = _opt_env("GEPD_SAVE_DIR", f"{GEPD}/save" if GEPD else "")
gepd_plugin_dir = _opt_env("GEPD_PLUGIN_DIR", f"{GEPD}/plugin" if GEPD else "")

PDSHARE = _opt_env("PDSHARE", str(Path(__file__).parents[4] / "share"))

PDPYTHON = _opt_env("PDPYTHON", str(Path(__file__).parents[4] / "bin"))

GEPD_ZIP = _opt_env('GEPD_ZIP')

# assumption: /app/gepd_archive is bind-mounted to host dir where gepd bundles can be placed
GEPD_ARCHIVE = _opt_env('GEPD_ARCHIVE')

# assumption:/app/gepd_target is bind-mounted to host dir of target 1964 installation to be updated
GEPD_TARGET = _opt_env('GEPD_TARGET')

MOUSEINJECTOR = _opt_env("MOUSEINJECTOR")

DC_BUILD_TAG = _opt_env("DC_BUILD_TAG", datetime.utcnow().isoformat())

DECOMP_BUILD = os.getenv("DECOMP_BUILD")

CURRENT_BUILD_ARCHIVE = CURRENT_BUILD_ARKHIVE = (
    Path(GEPD_ARCHIVE) / DC_BUILD_TAG if GEPD_ARCHIVE else None)

