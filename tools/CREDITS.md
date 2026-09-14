# Fojo Tooling Credits

Friends of Joanna's tools combine Fojo-specific workflow code with tooling and
ideas from the broader Perfect Dark decomp and PC-port ecosystem.

## Contributors and Derived Work

- Catherine Reprobate: Fojo `pdt` integration, modloader build and deployment
  workflows, Docker/container orchestration, remote N64 build flow, release
  helpers, and migration of the toolchain into the Fojo fork.
- Ryan Dwyer: original Perfect Dark decompilation project and `pdtools` lineage.
  Fojo's ROM/filetable/segment parsing and extraction helpers include code and
  behavior adapted from Ryan's Perfect Dark tooling.
- Raphael "Raf" Pinochet: Perfect Dark PC-port texture loader and texture codec
  work. Fojo's PNG/N64 texture conversion paths derive format handling and codec
  behavior from that work.

## Notes

Source-near attribution comments are intentionally preserved in the files they
describe. This file is the user-facing map of who and what the migrated tool
folder depends on; it does not replace module-level notices.

If more upstream authors are identified in source headers, nested history, or
subtree metadata, add them here in the same change that imports or updates the
affected tool.

## License

The migrated Fojo tooling keeps the MIT license text from the original
`docker-caroll` tool package in `pd-fojo/tools/LICENSE`. Files with different
upstream license notices must keep those notices next to the files and be called
out here.