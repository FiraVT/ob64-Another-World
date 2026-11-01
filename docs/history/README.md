# History: the recompilation record

The record of the recompilation project that OB64: Another World forked from (lfarroco/ogre-battle-64-recomp), up to the fork point `6429179` (2026-10-14), is kept at the git tag `history-archive`. It was removed from the working tree on 2026-11-01 (`docs/anotherworld/DECISIONS.md`).

The tag holds the decision log (`DECISIONS.md`), the session-by-session status log (`STATUS-LOG.md`, sessions 3 to 144), the completed libultra bridging plan (`LIBULTRA-BRIDGING.md`) and 142 session handoffs (`handoffs/`).

A reference written as `history-archive:docs/history/<file>` names a file at that tag. Read one with:

```bash
git show history-archive:docs/history/handoffs/HANDOFF-2026-09-15-session45.md
```

List the handoffs newest first with their titles:

```bash
make handoffs
```

On GitHub, open the tag from the branch selector and browse `docs/history/`.

A file there describes the port as it was when the file was written. Read [../README.md](../README.md) and the current guides first, and treat a claim there as a lead to verify (AGENTS.md rule 2). The facts that held up are in [../guides/recomp-facts.md](../guides/recomp-facts.md) and [../../PLAN.md](../../PLAN.md).
