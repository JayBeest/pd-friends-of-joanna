"""
pd_deploy
=========

Deploy a built mod tarball (produced by ``pdt build-n64-remote`` or any
compatible pipeline) into a local install directory.

Design mirrors ``pd_remote_build``:

* Pure-Python orchestrator with testable argv builders + ``Deployer``
  + ``DeploySpec`` dataclass.
* All side effects (rsync, tar, mkdir, fs walks) go through small
  adapter callables that take a ``runner`` / ``lister`` argument. The
  defaults call ``subprocess.run`` and ``os.listdir``. Tests inject
  recorders.
* The CLI assembles a spec from argv + ``.pdt.toml [deploy]`` and runs
  the orchestrator. Compose-friendly: a separate verb from build.

Pipeline
--------
1. Resolve the source tarball (``--from PATH`` wins; otherwise newest
   matching ``pd-*.tgz`` in ``source_dir`` by **filename sort**, which
   matches the build-pipeline's UTC-timestamp tag).
2. Extract the tarball to a tempdir.
3. ``mkdir -p`` every artifact destination under ``install_dir``.
4. For each ``[[deploy.artifacts]]`` entry, rsync
   ``<tempdir>/<tarball_path>/`` → ``<install_dir>/<dest_subdir>``.
   Additive by default (no ``--delete``); ``--clean`` flips that.
5. Cleanup tempdir.

Public API
----------
* :class:`DeployArtifact` -- one (tarball_path, dest_subdir) entry.
* :class:`DeploySpec` -- input parameters.
* :class:`Deployer` -- orchestrator.
* :func:`pick_latest_tarball` / :func:`build_argv_extract` /
  :func:`build_argv_rsync_artifact` -- exposed for unit testing.
"""

from __future__ import annotations

import dataclasses
import os
import shlex
import subprocess
import tempfile
from pathlib import Path
from typing import Callable, List, Optional, Sequence


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

#: Default glob used to pick a tarball when --from is not given. Matches
#: the artifact naming convention used by ``pd_remote_build``.
DEFAULT_TARBALL_GLOB: str = "pd-*.tgz"


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------


Runner = Callable[..., subprocess.CompletedProcess]


@dataclasses.dataclass
class DeployArtifact:
    """One slice of the tarball to deploy.

    Attributes
    ----------
    tarball_path
        Path *inside* the tarball whose contents are deployed. Can be
        a directory (its contents are rsynced) or a single file.
    dest_subdir
        Path relative to ``DeploySpec.install_dir`` where the artifact
        lands. Empty string = install_dir root.
    excludes
        Per-artifact rsync excludes (in addition to a small default
        set of junk files).
    """

    tarball_path: str
    dest_subdir: str = ""
    excludes: Sequence[str] = ()


#: Excludes applied to every deploy rsync, regardless of caller config.
DEFAULT_DEPLOY_EXCLUDES: tuple[str, ...] = (
    ".DS_Store",
)


@dataclasses.dataclass
class DeploySpec:
    """Inputs to a deploy invocation.

    Attributes
    ----------
    install_dir
        Where the mod is installed. Typically a ``mod_<name>`` dir
        under the engine's ``$PD_MODDIR``.
    artifacts
        Which paths inside the tarball to deploy and where they land
        under ``install_dir``. Must be non-empty.
    source_dir
        Directory scanned for tarballs when ``tarball`` is None.
        Newest filename wins.
    tarball
        Explicit tarball path (``--from``). Overrides ``source_dir``.
    tarball_glob
        Glob used when scanning ``source_dir``.
    clean
        If True, rsync runs with ``--delete`` -- destructive but
        guarantees no stale files from previous deploys.
    global_excludes
        Extra rsync excludes applied to *every* artifact rsync, in
        addition to ``DEFAULT_DEPLOY_EXCLUDES`` and each artifact's own
        ``excludes``. Used to protect files that must never be
        overwritten (e.g. the stock ROM given by ``$PD_ROMFILE``).
    """

    install_dir: Path
    artifacts: Sequence[DeployArtifact]
    source_dir: Optional[Path] = None
    tarball: Optional[Path] = None
    tarball_glob: str = DEFAULT_TARBALL_GLOB
    clean: bool = False
    global_excludes: Sequence[str] = ()


@dataclasses.dataclass
class DeployResult:
    """Outcome of a successful :meth:`Deployer.run`."""

    tarball: Path
    install_dir: Path
    steps: List[List[str]]


# ---------------------------------------------------------------------------
# Tarball selection
# ---------------------------------------------------------------------------


def pick_latest_tarball(source_dir: Path, glob: str = DEFAULT_TARBALL_GLOB) -> Path:
    """Return the lexicographically-greatest tarball in ``source_dir``.

    The build pipeline names tarballs ``pd-n64-<rom>-<UTC-TS>.tgz``
    where the timestamp sorts as text, so filename order == time order.
    """
    if not source_dir.is_dir():
        raise FileNotFoundError(f"deploy source_dir not found: {source_dir}")
    candidates = sorted(source_dir.glob(glob))
    if not candidates:
        raise FileNotFoundError(
            f"no tarballs matching {glob!r} in {source_dir}")
    return candidates[-1]


# ---------------------------------------------------------------------------
# Pure argv builders
# ---------------------------------------------------------------------------


def build_argv_extract(tarball: Path, dest: Path) -> List[str]:
    """argv for: tar xzf <tarball> -C <dest>."""
    return ["tar", "xzf", str(tarball), "-C", str(dest)]


def build_argv_rsync_artifact(spec: DeploySpec,
                              artifact: DeployArtifact,
                              extract_root: Path) -> List[str]:
    """argv for one artifact's rsync from extract_root into install_dir.

    Directory contents are rsynced (trailing slash on src); single-file
    artifacts are copied to the dest dir verbatim.
    """
    src_path = extract_root / artifact.tarball_path
    # We can't stat src_path here (the extract happens at run time) so
    # we always treat tarball_path as a directory and append "/".
    # Single-file deploys should set dest_subdir to the parent dir and
    # let rsync place the file by name -- works for both cases as long
    # as the user's tarball_path is consistent.
    src = str(src_path).rstrip("/") + "/"
    sub = (artifact.dest_subdir or "").strip("/")
    dst = str(spec.install_dir / sub) if sub else str(spec.install_dir)
    dst = dst.rstrip("/") + "/"
    argv = ["rsync", "-a"]
    if spec.clean:
        argv.append("--delete")
    for ex in DEFAULT_DEPLOY_EXCLUDES:
        argv.extend(["--exclude", ex])
    for ex in spec.global_excludes:
        argv.extend(["--exclude", ex])
    for ex in artifact.excludes:
        argv.extend(["--exclude", ex])
    argv.extend([src, dst])
    return argv


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------


def _default_runner(argv: Sequence[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(list(argv), check=True, **kw)


class Deployer:
    """Orchestrates a mod-deploy cycle.

    Parameters
    ----------
    spec
        The :class:`DeploySpec` to execute.
    runner
        Callable taking an argv list. Must raise on non-zero exit.
    mkdir
        Callable taking a :class:`Path` to create a directory.
    tempdir_factory
        Callable returning a context manager that yields a Path.
        Defaults to :class:`tempfile.TemporaryDirectory`.
    log
        Optional logger callable taking a single string.
    """

    def __init__(
        self,
        spec: DeploySpec,
        runner: Optional[Runner] = None,
        mkdir: Optional[Callable[[Path], None]] = None,
        tempdir_factory: Optional[Callable[[], "tempfile.TemporaryDirectory"]] = None,
        log: Optional[Callable[[str], None]] = None,
    ) -> None:
        self.spec = spec
        self._runner: Runner = runner or _default_runner
        self._mkdir = mkdir or (lambda p: p.mkdir(parents=True, exist_ok=True))
        self._tempdir = tempdir_factory or (lambda: tempfile.TemporaryDirectory())
        self._log = log or (lambda msg: print(msg))

    def _resolve_tarball(self) -> Path:
        if self.spec.tarball is not None:
            tb = self.spec.tarball
            if not tb.is_file():
                raise FileNotFoundError(f"deploy tarball not found: {tb}")
            return tb
        if self.spec.source_dir is None:
            raise ValueError("deploy requires either tarball or source_dir")
        return pick_latest_tarball(self.spec.source_dir, self.spec.tarball_glob)

    def run(self) -> DeployResult:
        spec = self.spec
        if not spec.artifacts:
            raise ValueError("deploy spec has no artifacts")
        tarball = self._resolve_tarball()
        self._mkdir(spec.install_dir)
        for art in spec.artifacts:
            sub = (art.dest_subdir or "").strip("/")
            if sub:
                self._mkdir(spec.install_dir / sub)

        steps: List[List[str]] = []
        with self._tempdir() as td:
            extract_root = Path(td)
            extract_argv = build_argv_extract(tarball, extract_root)
            self._log("[extract] " + " ".join(shlex.quote(a) for a in extract_argv))
            self._runner(extract_argv)
            steps.append(extract_argv)
            for art in spec.artifacts:
                src_path = extract_root / art.tarball_path
                if not src_path.exists():
                    self._log(f"[skip]    {art.tarball_path!r} not in tarball")
                    continue
                argv = build_argv_rsync_artifact(spec, art, extract_root)
                self._log("[install] " + " ".join(shlex.quote(a) for a in argv))
                self._runner(argv)
                steps.append(argv)

        return DeployResult(
            tarball=tarball,
            install_dir=spec.install_dir,
            steps=steps,
        )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_arg_parser():
    import argparse
    p = argparse.ArgumentParser(
        prog="pdt deploy-mod",
        description="Deploy a built mod tarball into a local install dir.",
    )
    p.add_argument("--install-dir", dest="install_dir",
                   help="absolute path of the mod install dir, e.g. "
                        "$PD_MODDIR/mod_fojo (required unless supplied by "
                        ".pdt.toml [deploy])")
    p.add_argument("--source-dir", dest="source_dir", default=None,
                   help="directory to scan for the newest tarball when "
                        "--from is not given")
    p.add_argument("--from", dest="tarball", default=None,
                   help="explicit tarball path; overrides --source-dir")
    p.add_argument("--tarball-glob", dest="tarball_glob",
                   default=DEFAULT_TARBALL_GLOB,
                   help=f"glob for tarball auto-pick (default: "
                        f"{DEFAULT_TARBALL_GLOB!r})")
    p.add_argument("--artifact", action="append", default=[], dest="artifact",
                   help="tarball slice to deploy. Format: "
                        "'TARBALL_PATH' or 'TARBALL_PATH:DEST_SUBDIR'. "
                        "Repeatable. Excludes can only be set via "
                        "[[deploy.artifacts]] in toml.")
    p.add_argument("--clean", action="store_true",
                   help="rsync with --delete (destructive; removes files "
                        "in install_dir that aren't in the tarball)")
    p.add_argument("--dry-run", action="store_true",
                   help="print argv lists without executing")
    return p


def _parse_artifact_arg(spec_str: str) -> DeployArtifact:
    """Parse a CLI ``--artifact`` value into a :class:`DeployArtifact`."""
    if ":" in spec_str:
        path, _, sub = spec_str.rpartition(":")
    else:
        path, sub = spec_str, ""
    return DeployArtifact(tarball_path=path, dest_subdir=sub)


def cli_main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)
    if not args.install_dir:
        print("error: --install-dir is required (or set [deploy].install_dir "
              "in .pdt.toml)", file=__import__("sys").stderr)
        return 2
    if not args.tarball and not args.source_dir:
        print("error: provide --from <tarball> or --source-dir <dir>",
              file=__import__("sys").stderr)
        return 2
    if not args.artifact:
        print("error: at least one --artifact must be specified (or "
              "[[deploy.artifacts]] in .pdt.toml)",
              file=__import__("sys").stderr)
        return 2

    # Protect the stock ROM from being overwritten during deploy.
    # PD_ROMFILE is injected into the environment from .pdt.toml [env]
    # before cli_main is called, so we can read it here.
    romfile = os.environ.get("PD_ROMFILE", "").strip()
    global_excludes: tuple[str, ...] = ()
    if romfile:
        global_excludes = (os.path.basename(romfile),)

    spec = DeploySpec(
        install_dir=Path(args.install_dir).expanduser().resolve(),
        artifacts=tuple(_parse_artifact_arg(s) for s in args.artifact),
        source_dir=Path(args.source_dir).expanduser().resolve()
                    if args.source_dir else None,
        tarball=Path(args.tarball).expanduser().resolve()
                    if args.tarball else None,
        tarball_glob=args.tarball_glob,
        clean=args.clean,
        global_excludes=global_excludes,
    )

    if args.dry_run:
        # Resolve the tarball so the dry-run shows the real argv.
        # If the source dir is empty, fall through with a placeholder
        # so the user can still see the artifact-rsync template.
        try:
            tarball = (spec.tarball
                       if spec.tarball is not None
                       else pick_latest_tarball(spec.source_dir, spec.tarball_glob))
        except FileNotFoundError:
            tarball = Path("<TARBALL>")
        extract_root = Path("<TMPDIR>")
        print(" ".join(shlex.quote(a) for a in build_argv_extract(tarball, extract_root)))
        for art in spec.artifacts:
            print(" ".join(shlex.quote(a)
                  for a in build_argv_rsync_artifact(spec, art, extract_root)))
        return 0

    deployer = Deployer(spec)
    result = deployer.run()
    print(f"OK tarball={result.tarball} install_dir={result.install_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(cli_main())
