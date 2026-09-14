# Friends of Joanna Tools

This directory is the installable tool root for the Friends of Joanna fork. It
contains modloader tools, build helpers, ROM and texture utilities, container
support, binary inspection tools, and the older `pdtools` decomp helper suite.

## In a Fojo Checkout

From a direct `pd-fojo` clone:

```sh
source ./activate.sh
```

That activates `tools/bin` and `tools/pdtools/bin`, sets `PD` to the current
checkout, and sets `PDTOOLS` to `tools/pdtools`.

## In the Monorepo Workspace

From the monorepo root:

```sh
source ./activate.sh
```

That puts these directories on `PATH`:

```sh
pd-fojo/tools/bin
pd-fojo/tools/pdtools/bin
```

It also sets `PD` to the local `pd-fojo` checkout, `PDTOOLS` to
`pd-fojo/tools/pdtools`, and `PD_ROMFILE` to the local Perfect Dark ROM path in
`pd-fojo-basedir/data`.

## Copied or Symlinked Installs

The tool folder can be copied or symlinked outside a Fojo workspace when one set
of tools needs to drive several branches.

```sh
source /path/to/fojo-tools/activate.sh
```

Or wire the environment manually:

```sh
export PATH=/path/to/fojo-tools/bin:/path/to/fojo-tools/pdtools/bin:$PATH
export PD=/path/to/pd-fojo
export PDTOOLS=/path/to/fojo-tools/pdtools
export PD_ROMFILE=/path/to/pd-fojo-basedir/data/pd.ntsc-final.z64
```

`pdt --root /path/to/pd-fojo ...` can also select a project checkout explicitly.
Project defaults may live in `.pdt.toml`, `.pdt.json`, or `~/.config/pdt.toml`;
real environment variables always win over config-file values.

## Layout

```text
tools/
  bin/                         # pdt, rom-diff, fdiff, pdheadedit, ...
  lib/packages/python/          # Python support libraries
  pdtools/bin/                  # Ryan-derived decomp helper scripts
  docker/                       # container image definitions
  scripts/psake/                # container-backed psake tasks
  share/                        # ROM/file metadata used by tools
  activate.sh                   # copied/symlinked tool-root activation
  CREDITS.md                    # contributor and derived-work attribution
```

The checkout-level `pd-fojo/activate.sh` script is the preferred entry point for
a normal source clone. `tools/activate.sh` is for copied or symlinked tool roots.

Existing port build helpers such as `mkfiletable`, `modsetcheck`, `pdsym`,
`mktextures`, and `release.py` remain in this directory too.

## Common Commands

```sh
pdt build-port
pdt rom info
pdt tex2png input.bin output.png
pdt png2tex input.png output.bin --format RGBA16
pdt build-n64-remote --tag $(date -u +%Y%m%d)
rom-diff modded.z64 vanilla.z64
fdiff show HEAD~1
```

For modloader-specific workflows, see the Fojo wiki pages `Modloader Workflow`,
`Modloader Tool Usage`, `Modset Check`, and `Tooling Reference`.