# Documentation

Start with [../CONTRIBUTING.md](../CONTRIBUTING.md) to set up a build. People
and AI agents who reverse-engineer the game read [../AGENTS.md](../AGENTS.md)
first.

## The project

| Document | Contents |
|---|---|
| [anotherworld/ROADMAP.md](anotherworld/ROADMAP.md) | The plan: from the recompiled port to native, data-driven systems, phase by phase |
| [anotherworld/FEATURES.md](anotherworld/FEATURES.md) | What is built today |
| [anotherworld/DECISIONS.md](anotherworld/DECISIONS.md) | Decisions since the fork, newest first |
| [anotherworld/separation.md](anotherworld/separation.md) | How the features reach the recompiled game through `app/features/` and the hook table |
| [anotherworld/security.md](anotherworld/security.md) | What a mod can reach, and the tests that enforce it |
| [anotherworld/](anotherworld/) | One design document per system (`design-*.md`) and research notes (`notes-*.md`) |
| [../PLAN.md](../PLAN.md) | The recompilation's status, open defects and technical findings |

## Guides

| Guide | Contents |
|---|---|
| [guides/build.md](guides/build.md) | Regenerating the recompiled code, building, running, crash reports, troubleshooting |
| [guides/releases.md](guides/releases.md) | `make dist`, the smoke test, and GitHub releases |
| [guides/controls-and-settings.md](guides/controls-and-settings.md) | Bindings, the settings tabs and the in-game overlay |
| [guides/diagnostics.md](guides/diagnostics.md) | Scripted runs, frame capture, the live console, checkpoints, and the diagnostics toolkit (`tools/*.py`) |
| [guides/code-mods.md](guides/code-mods.md) | Code mods (`.nrm`) and hooks on bank code |
| [guides/recomp-facts.md](guides/recomp-facts.md) | Facts about the game and the port that earlier sessions got wrong |
| [guides/emulator-first.md](guides/emulator-first.md) | Read before interpreting a display list: the five faithfulness checks and how to use a reference emulator |
| [guides/app-architecture.md](guides/app-architecture.md) | App structure, runtime flow and callbacks |
| [guides/sprite-mechanisms.md](guides/sprite-mechanisms.md) | How a 2D image reaches the screen, and how to check one |
| [guides/njpeg-backgrounds.md](guides/njpeg-backgrounds.md) | The njpeg background pipeline and its 75 assets |
| [guides/hd-backgrounds.md](guides/hd-backgrounds.md) | The HD background pack in `mods/backgrounds/` |
| [guides/rsp-microcode.md](guides/rsp-microcode.md) | RSP microcode research and recompilation |
| [guides/linux-migration.md](guides/linux-migration.md) | Linux setup details |
| [guides/app-build.md](guides/app-build.md) | Section index of the former build guide, for older references |

## Reference

| Document | Contents |
|---|---|
| [scenes.md](scenes.md) | What each screen should show, from the maintainer, and the port's status per screen |
| [symbols.md](symbols.md) | Proposed symbol names: address, name, evidence, confidence |
| [notes/](notes/) | Research notes on items, drops, saves, bank hooks, the tutorial crash and the black sprite |

## History

The record of the recompilation project this one forked from, up to the fork
on 2026-10-14, is kept at the git tag `history-archive`: 142 session handoffs,
the status log, the decision log and the completed libultra plan.
[history/README.md](history/README.md) says how to read it. A file there
describes the port as it was when the file was written.

## Conventions

- ROM dumps and extracted assets are never committed.
- `tools/RT64` is the only git submodule. `tools/N64Recomp` and
  `tools/N64ModernRuntime` are gitignored clones. The project's changes to all
  three are the patches in `../patches/`; never commit inside them.
- Platform I/O (window, input, audio) stays in the app, so the game code never
  touches SDL directly.
- `../debug/` holds capture scripts for investigations. It is not part of the
  app build.
