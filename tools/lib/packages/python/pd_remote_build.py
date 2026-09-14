"""
pd_remote_build
===============

Lightweight remote-build orchestrator for the Perfect Dark N64 decomp.

The Mac dev box can't build N64 ROMs directly, but a remote Linux box
can. This module wraps a one-shot rsync → ssh build → rsync-back loop
that drops a tarball of the build artifacts into a local output dir.

Design
------
* All side effects (rsync, ssh, mkdir, file IO) go through small adapter
  functions that take a ``runner`` callable. The default runner is
  ``subprocess.run`` with ``check=True``. Tests inject a fake runner
  that records argv lists and returns canned exit codes.
* The orchestrator is a pure-Python class — no global state, no env
  reads, no chdir. The CLI wrapper is thin and only assembles a
  ``RemoteBuildSpec`` from argv before calling ``RemoteBuilder.run()``.
* Defaults match the docker-caroll psake task names ("foj") and the
  on-disk layout produced by ``mk-moddir``.

Public API
----------
* :class:`RemoteBuildSpec` — input parameters.
* :class:`RemoteBuilder` — orchestrator.
* :func:`build_argv_rsync_push` / :func:`build_argv_rsync_pull` /
  :func:`build_argv_ssh_build` / :func:`build_argv_ssh_archive` —
  pure argv-building helpers, exposed for unit testing.
"""

from __future__ import annotations

import dataclasses
import datetime as _dt
import os
import shlex
import subprocess
from pathlib import Path
from typing import Callable, Iterable, List, Optional, Sequence


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

#: Paths excluded from the source rsync push. These are derived/cache
#: directories that the remote will (re)generate, plus large state we
#: don't need on the remote.
DEFAULT_RSYNC_EXCLUDES: tuple[str, ...] = (
    "build/",
    "dist/",
    "mods/",
    ".git/",
    ".venv/",
    "__pycache__/",
    "*.o",
    "*.pyc",
    ".DS_Store",
    # Per-host project config: each side carries its own paths
    # (mac uses /Users/..., linuxbox uses /home/catherine/...). Without
    # this exclude, --delete clobbers the remote's config and the
    # subsequent psake run tries to bind-mount mac-only paths.
    ".pdt.toml",
    ".pdt.json",
)

#: Default psake tasklist used to produce a foj N64 build with mod assets.
DEFAULT_TASKLIST: str = "foj"

#: Paths excluded from every overlay rsync, in addition to whatever
#: the overlay declares. Overlays are user-curated asset trees, so the
#: only things we strip unconditionally are VCS/junk directories that
#: should never be part of a deliverable.
DEFAULT_OVERLAY_EXCLUDES: tuple[str, ...] = (
    ".git/",
    ".DS_Store",
)

#: Default ROM id whose build artifacts we tar up and ship back.
DEFAULT_ROM_ID: str = "ntsc-final"


# ---------------------------------------------------------------------------
# Spec / runner types
# ---------------------------------------------------------------------------

#: Subprocess runner signature. Must accept argv as a list of strings,
#: must respect ``cwd``, must raise on non-zero exit.
Runner = Callable[..., subprocess.CompletedProcess]


@dataclasses.dataclass
class Overlay:
    """A non-built asset tree to rsync on top of the remote worktree.

    Overlays are *additive* (no ``--delete``): they layer files into
    the build tree so the build can see them, and/or so they end up
    inside the final tarball. Multiple overlays apply in declared
    order, grouped by ``stage``.

    Attributes
    ----------
    source
        Local directory whose contents are pushed.
    dest_subdir
        Path relative to ``RemoteBuildSpec.remote_path`` where the
        overlay lands. Empty string means "the worktree root".
    excludes
        Per-overlay rsync excludes (the spec-level excludes are *not*
        applied to overlays, since they're tuned for source trees).
    stage
        When to apply the overlay:

        * ``"post-build"`` (default) -- after ``pdt psake`` finishes,
          before the tarball is created. Use this for shipping
          non-built assets alongside the build output (e.g. dropping
          asset files into ``build/<rom>/mod/``).
        * ``"pre-build"`` -- after the source push, before the build
          runs. Use this when the build tooling needs to *see* the
          assets (e.g. a filetable compiler that ingests raw files).
    """

    source: Path
    dest_subdir: str = ""
    excludes: Sequence[str] = ()
    stage: str = "post-build"


@dataclasses.dataclass
class RemoteBuildSpec:
    """Inputs to a remote N64 build invocation.

    Attributes
    ----------
    remote
        SSH target, e.g. ``"linuxbox"`` or ``"user@host"``.
    remote_path
        Absolute path on the remote where the source tree should live.
    source
        Local source tree (the n64 decomp working copy).
    out_dir
        Local directory to drop the resulting tarball into.
    rom_id
        Build variant id (matches ``$ROMID`` in the psake/Makefile).
    tasklist
        psake tasklist to run on the remote.
    jobs
        ``--jobs`` value forwarded to the remote ``pdt`` invocation.
        ``None`` means "let the remote decide".
    artifacts
        Iterable of remote-relative globs to include in the tarball.
        Defaults to ``("build/<rom_id>/pd.z64", "build/<rom_id>/mod/")``.
    excludes
        rsync excludes for the push step.
    tag
        Optional explicit build tag (used in the tarball name). If
        ``None``, a UTC timestamp is generated at run time.
    extra_ssh_opts
        Additional argv tokens inserted after ``ssh`` (e.g.
        ``["-o", "ControlPath=~/.ssh/cm-%r@%h:%p"]``).
    rsync_back
        If False, skip the artifact-pull step (useful for fire-and-
        forget builds where the user picks up the tarball themselves).
    remote_config
        Optional path *on the remote* to a ``.pdt.toml`` (or ``.json``).
        If set, the build step copies this file into
        ``<remote_path>/.pdt.toml`` before invoking ``pdt psake`` so the
        remote build picks up host-appropriate paths (e.g. linuxbox
        ``/home/catherine/...`` instead of the mac ``/Users/catherine/...``).
        Typical value: ``"~/.config/pdt.toml"``.
    """

    remote: str
    remote_path: str
    source: Path
    out_dir: Path
    rom_id: str = DEFAULT_ROM_ID
    tasklist: str = DEFAULT_TASKLIST
    jobs: Optional[int] = None
    artifacts: Optional[Sequence[str]] = None
    excludes: Sequence[str] = DEFAULT_RSYNC_EXCLUDES
    tag: Optional[str] = None
    extra_ssh_opts: Sequence[str] = ()
    rsync_back: bool = True
    remote_config: Optional[str] = None
    overlays: Sequence[Overlay] = ()

    def resolved_artifacts(self) -> List[str]:
        """Return the artifact glob list with ``rom_id`` substituted."""
        if self.artifacts is not None:
            return list(self.artifacts)
        return [
            f"build/{self.rom_id}/pd.z64",
            f"build/{self.rom_id}/mod",
        ]

    def resolved_tag(self) -> str:
        """Return ``tag`` or a fresh UTC timestamp tag."""
        return self.tag or _dt.datetime.utcnow().strftime("%Y-%m-%d.%H-%M-%S")

    def tarball_name(self, tag: Optional[str] = None) -> str:
        """Return the canonical artifact tarball filename."""
        return f"pd-n64-{self.rom_id}-{tag or self.resolved_tag()}.tgz"


# ---------------------------------------------------------------------------
# Pure argv builders (the testable core)
# ---------------------------------------------------------------------------


def _ssh_prefix(spec: RemoteBuildSpec) -> List[str]:
    cmd: List[str] = ["ssh"]
    cmd.extend(spec.extra_ssh_opts)
    cmd.append(spec.remote)
    return cmd


def build_argv_ssh_mkdir(spec: RemoteBuildSpec) -> List[str]:
    """argv for: ssh remote 'mkdir -p <remote_path> [overlay subdirs...]'.

    Run before the rsync push so a fresh remote box doesn't fail with
    "No such file or directory". Also pre-creates each overlay's
    ``dest_subdir`` so the overlay rsyncs land in the right place
    without needing modern rsync's ``--mkpath``.
    """
    base = spec.remote_path.rstrip("/")
    targets = [base]
    for ov in spec.overlays:
        sub = (ov.dest_subdir or "").strip("/")
        if sub:
            targets.append(f"{base}/{sub}")
    remote_sh = "mkdir -p " + " ".join(shlex.quote(t) for t in targets)
    return _ssh_prefix(spec) + [remote_sh]


def build_argv_rsync_overlay(spec: RemoteBuildSpec,
                              overlay: Overlay) -> List[str]:
    """argv for: rsync local overlay.source/ → remote subdir (additive).

    Notably *no* ``--delete`` flag: overlays add files to the build
    tree, they don't replace it.
    """
    src = str(overlay.source).rstrip("/") + "/"
    sub = (overlay.dest_subdir or "").strip("/")
    base = spec.remote_path.rstrip("/")
    dst_path = f"{base}/{sub}" if sub else base
    dst = f"{spec.remote}:{dst_path}"
    argv = ["rsync", "-az"]
    # Always strip VCS/junk; then apply any per-overlay excludes the
    # caller asked for. (We don't dedupe; rsync tolerates repeats.)
    for ex in DEFAULT_OVERLAY_EXCLUDES:
        argv.extend(["--exclude", ex])
    for ex in overlay.excludes:
        argv.extend(["--exclude", ex])
    if spec.extra_ssh_opts:
        argv.extend(["-e", "ssh " + " ".join(shlex.quote(t) for t in spec.extra_ssh_opts)])
    argv.extend([src, dst])
    return argv


def build_argv_rsync_push(spec: RemoteBuildSpec) -> List[str]:
    """argv for: rsync local source → remote workdir."""
    src = str(spec.source).rstrip("/") + "/"
    dst = f"{spec.remote}:{spec.remote_path.rstrip('/')}"
    argv = ["rsync", "-az", "--delete"]
    for ex in spec.excludes:
        argv.extend(["--exclude", ex])
    if spec.extra_ssh_opts:
        argv.extend(["-e", "ssh " + " ".join(shlex.quote(t) for t in spec.extra_ssh_opts)])
    argv.extend([src, dst])
    return argv


def build_argv_ssh_build(spec: RemoteBuildSpec) -> List[str]:
    """argv for: ssh remote && pdt psake foj.

    The remote command sequence:
      cd <remote_path>
      pdt psake --tasklist <tasklist> [--jobs N]

    We let the remote's ``pdt`` discover its own root; we only set CWD.
    """
    pdt_cmd = ["pdt", "psake", "--tasklist", spec.tasklist]
    if spec.jobs is not None:
        pdt_cmd.extend(["--jobs", str(spec.jobs)])
    pieces = [f"cd {shlex.quote(spec.remote_path)}"]
    if spec.remote_config:
        # Copy the host-appropriate config into the worktree so pdt's
        # config-walk picks it up. Use `cp -f` (not symlink) so the
        # file is a real .pdt.toml even on hosts where ln might fail.
        # If the value starts with ``~/`` we leave it unquoted so the
        # remote shell expands the tilde (shlex.quote would single-
        # quote it, which prevents tilde expansion).
        if spec.remote_config.startswith("~/") or spec.remote_config == "~":
            src_cfg = spec.remote_config
        else:
            src_cfg = shlex.quote(spec.remote_config)
        pieces.append(
            f"cp -f {src_cfg} "
            f"{shlex.quote(spec.remote_path.rstrip('/') + '/.pdt.toml')}"
        )
    pieces.append(" ".join(shlex.quote(t) for t in pdt_cmd))
    remote_sh = "set -euo pipefail; " + " && ".join(pieces)
    return _ssh_prefix(spec) + [remote_sh]


def build_argv_ssh_archive(spec: RemoteBuildSpec, tag: str) -> List[str]:
    """argv for: ssh remote && tar czf /tmp/<tarball> <artifacts>.

    The tar command is run with ``-C <remote_path>`` so the artifact
    paths inside the tarball are relative to the source root.
    """
    tarball = f"/tmp/{spec.tarball_name(tag)}"
    artifacts = spec.resolved_artifacts()
    tar_argv = ["tar", "czf", tarball, "-C", spec.remote_path] + list(artifacts)
    remote_sh = " ".join(shlex.quote(t) for t in tar_argv) + f" && echo {shlex.quote(tarball)}"
    return _ssh_prefix(spec) + [remote_sh]


def build_argv_rsync_pull(spec: RemoteBuildSpec, tag: str) -> List[str]:
    """argv for: rsync remote tarball → local out_dir."""
    tarball = spec.tarball_name(tag)
    src = f"{spec.remote}:/tmp/{tarball}"
    dst = str(spec.out_dir).rstrip("/") + "/"
    argv = ["rsync", "-az", src, dst]
    if spec.extra_ssh_opts:
        argv.extend(["-e", "ssh " + " ".join(shlex.quote(t) for t in spec.extra_ssh_opts)])
    return argv


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------


def _default_runner(argv: Sequence[str], **kw) -> subprocess.CompletedProcess:
    """The default subprocess runner. Streams output, raises on failure."""
    return subprocess.run(list(argv), check=True, **kw)


@dataclasses.dataclass
class RemoteBuildResult:
    """Outcome of a successful :meth:`RemoteBuilder.run`."""

    tag: str
    tarball_path: Path
    steps: List[List[str]]


class RemoteBuilder:
    """Orchestrates a remote N64 build cycle.

    Parameters
    ----------
    spec
        The :class:`RemoteBuildSpec` to execute.
    runner
        Callable taking an argv list. Must raise on non-zero exit.
        Defaults to ``subprocess.run(..., check=True)``.
    mkdir
        Callable taking a :class:`Path` to create the local output dir.
        Defaults to ``Path.mkdir(parents=True, exist_ok=True)``.
    log
        Optional logger callable taking a single string.
    """

    def __init__(
        self,
        spec: RemoteBuildSpec,
        runner: Optional[Runner] = None,
        mkdir: Optional[Callable[[Path], None]] = None,
        log: Optional[Callable[[str], None]] = None,
    ) -> None:
        self.spec = spec
        self._runner: Runner = runner or _default_runner
        self._mkdir = mkdir or (lambda p: p.mkdir(parents=True, exist_ok=True))
        self._log = log or (lambda msg: print(msg))

    # ---- step methods (each returns its argv) -----------------------------

    def step_mkdir(self) -> List[str]:
        argv = build_argv_ssh_mkdir(self.spec)
        self._log("[mkdir] " + " ".join(shlex.quote(a) for a in argv))
        self._runner(argv)
        return argv

    def step_push(self) -> List[str]:
        argv = build_argv_rsync_push(self.spec)
        self._log("[push] " + " ".join(shlex.quote(a) for a in argv))
        self._runner(argv)
        return argv

    def step_overlay(self, overlay: Overlay) -> List[str]:
        argv = build_argv_rsync_overlay(self.spec, overlay)
        self._log("[overlay] " + " ".join(shlex.quote(a) for a in argv))
        self._runner(argv)
        return argv

    def step_build(self) -> List[str]:
        argv = build_argv_ssh_build(self.spec)
        self._log("[build] " + " ".join(shlex.quote(a) for a in argv))
        self._runner(argv)
        return argv

    def step_archive(self, tag: str) -> List[str]:
        argv = build_argv_ssh_archive(self.spec, tag)
        self._log("[archive] " + " ".join(shlex.quote(a) for a in argv))
        self._runner(argv)
        return argv

    def step_pull(self, tag: str) -> List[str]:
        argv = build_argv_rsync_pull(self.spec, tag)
        self._log("[pull] " + " ".join(shlex.quote(a) for a in argv))
        self._runner(argv)
        return argv

    # ---- orchestration ----------------------------------------------------

    def run(self) -> RemoteBuildResult:
        spec = self.spec
        if not spec.source.is_dir():
            raise FileNotFoundError(f"source dir not found: {spec.source}")
        self._mkdir(spec.out_dir)

        for ov in spec.overlays:
            if not ov.source.is_dir():
                raise FileNotFoundError(f"overlay source not found: {ov.source}")

        tag = spec.resolved_tag()
        steps: List[List[str]] = []
        steps.append(self.step_mkdir())
        steps.append(self.step_push())
        for ov in spec.overlays:
            if ov.stage == "pre-build":
                steps.append(self.step_overlay(ov))
        steps.append(self.step_build())
        for ov in spec.overlays:
            if ov.stage == "post-build":
                steps.append(self.step_overlay(ov))
        steps.append(self.step_archive(tag))
        if spec.rsync_back:
            steps.append(self.step_pull(tag))

        tarball_path = spec.out_dir / spec.tarball_name(tag)
        return RemoteBuildResult(tag=tag, tarball_path=tarball_path, steps=steps)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_arg_parser():
    import argparse
    p = argparse.ArgumentParser(
        prog="pdt build-n64-remote",
        description="Build the N64 PD ROM on a remote Linux box and "
                    "fetch the resulting tarball.",
    )
    p.add_argument("--remote",
                   help="ssh target, e.g. user@linuxbox (required unless "
                        "supplied by .pdt.toml [remote-build])")
    p.add_argument("--remote-path",
                   help="absolute path on the remote to rsync the source into "
                        "(required unless supplied by .pdt.toml [remote-build])")
    p.add_argument("--source", default=os.getcwd(),
                   help="local source tree (default: cwd)")
    p.add_argument("--out", default=os.path.join(os.getcwd(), "out"),
                   dest="out_dir",
                   help="local dir to drop the tarball into (default: ./out)")
    p.add_argument("--rom-id", default=DEFAULT_ROM_ID,
                   help=f"ROM variant (default: {DEFAULT_ROM_ID})")
    p.add_argument("--tasklist", default=DEFAULT_TASKLIST,
                   help=f"psake tasklist (default: {DEFAULT_TASKLIST})")
    p.add_argument("--jobs", type=int, default=None,
                   help="forward --jobs to remote pdt (default: remote decides)")
    p.add_argument("--no-rsync-back", action="store_true",
                   help="skip the final pull; tarball stays on the remote")
    p.add_argument("--tag", default=None,
                   help="explicit build tag (default: UTC timestamp)")
    p.add_argument("--ssh-opt", action="append", default=[],
                   help="extra ssh option, repeatable (e.g. -o ControlPath=...)")
    p.add_argument("--remote-config", default=None, dest="remote_config",
                   help="path on the remote to a .pdt.toml/.json that should "
                        "be copied into <remote-path>/.pdt.toml before psake "
                        "runs (e.g. ~/.config/pdt.toml). default: leave the "
                        "remote's existing .pdt.toml in place.")
    p.add_argument("--overlay", action="append", default=[], dest="overlay",
                   help="local asset tree to rsync on top of the remote "
                        "worktree. Format: 'SOURCE', 'SOURCE:DEST_SUBDIR', "
                        "or 'SOURCE:DEST_SUBDIR:STAGE' where STAGE is "
                        "'pre-build' or 'post-build' (default). Repeatable; "
                        "overlays apply in declared order within each stage. "
                        "Excludes can only be set via "
                        "[[remote-build.overlays]] in toml.")
    p.add_argument("--artifact", action="append", default=[], dest="artifact",
                   help="remote-relative path to include in the tarball "
                        "(relative to --remote-path). Repeatable. If not "
                        "given, defaults to "
                        "'build/<rom-id>/pd.z64' and 'build/<rom-id>/mod'. "
                        "Can also be set via [remote-build].artifacts in "
                        ".pdt.toml.")
    p.add_argument("--dry-run", action="store_true",
                   help="print argv lists without executing")
    return p


def _parse_overlay_arg(spec_str: str) -> Overlay:
    """Parse a CLI ``--overlay`` value into an :class:`Overlay`.

    Format: ``SOURCE``, ``SOURCE:DEST_SUBDIR``, or
    ``SOURCE:DEST_SUBDIR:STAGE`` where ``STAGE`` is ``pre-build`` or
    ``post-build``. The split is colon-delimited from the right;
    source paths containing colons are unsupported (rare on unix).
    """
    parts = spec_str.split(":")
    stage = "post-build"
    sub = ""
    if len(parts) >= 3 and parts[-1] in ("pre-build", "post-build"):
        stage = parts[-1]
        sub = parts[-2]
        source_str = ":".join(parts[:-2])
    elif len(parts) >= 2:
        sub = parts[-1]
        source_str = ":".join(parts[:-1])
    else:
        source_str = parts[0]
    return Overlay(
        source=Path(source_str).expanduser().resolve(),
        dest_subdir=sub,
        stage=stage,
    )


def cli_main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)
    if not args.remote or not args.remote_path:
        print("error: --remote and --remote-path are required (or set them "
              "in .pdt.toml under [remote-build])", file=__import__("sys").stderr)
        return 2
    overlays = tuple(_parse_overlay_arg(s) for s in (args.overlay or []))
    spec = RemoteBuildSpec(
        remote=args.remote,
        remote_path=args.remote_path,
        source=Path(args.source).expanduser().resolve(),
        out_dir=Path(args.out_dir).expanduser().resolve(),
        rom_id=args.rom_id,
        tasklist=args.tasklist,
        jobs=args.jobs,
        tag=args.tag,
        extra_ssh_opts=tuple(args.ssh_opt),
        rsync_back=not args.no_rsync_back,
        remote_config=args.remote_config,
        overlays=overlays,
        artifacts=tuple(args.artifact) if args.artifact else None,
    )

    if args.dry_run:
        # Print argv for every step without running anything. Use a
        # fixed tag so the output is reproducible.
        tag = spec.resolved_tag()
        steps_argv = [
            build_argv_ssh_mkdir(spec),
            build_argv_rsync_push(spec),
        ]
        for ov in spec.overlays:
            if ov.stage == "pre-build":
                steps_argv.append(build_argv_rsync_overlay(spec, ov))
        steps_argv.append(build_argv_ssh_build(spec))
        for ov in spec.overlays:
            if ov.stage == "post-build":
                steps_argv.append(build_argv_rsync_overlay(spec, ov))
        steps_argv.append(build_argv_ssh_archive(spec, tag))
        if spec.rsync_back:
            steps_argv.append(build_argv_rsync_pull(spec, tag))
        for argv_step in steps_argv:
            print(" ".join(shlex.quote(a) for a in argv_step))
        return 0

    builder = RemoteBuilder(spec)
    result = builder.run()
    print(f"OK tag={result.tag} tarball={result.tarball_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(cli_main())
