# `debug/`: capture scripts for investigations

Scripts that drive the native game for one kind of report and collect what an
investigation needs. None of it is part of the app build. The browser probes
that used to live here were removed with the browser build on 2026-11-01.

| file | what |
|---|---|
| `frame-capture.sh` | one key press writes the whole 8 MiB RDRAM image (`tools/ogre_frame.py` renders it) |
| `black-sprite-capture.sh` | RDRAM and the textured rectangles of the frame with the black sprite part (session 131) |
| `head-trace.sh` | names the texture a map unit's sprite is bound to |
| `head-crop.py` | crops the head region from a rendered frame |
| `menu-probe.sh` | a "this screen is slow" report: run log, host stack samples, coverage, a snapshot |

Each script's header lists its arguments and outputs. Captured output goes to
`debug/out/`, which git ignores.

The evidence folders of sessions 133 to 136 (`black-sprite-133/`, `-134/`,
`-136/`) were removed on 2026-11-01 after the defect was fixed. They are in the
git history, and `docs/notes/black-sprite.md` records what they showed.
