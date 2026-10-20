# Handoff

Written at the end of each planned session and read at the start of the next
(see `CLAUDE.md`). Overwritten each time; `git log -p docs/handoff.md` has the
history.

_Last updated: 2026-10-17_

## Done

- Stages A0–A2.4, B1.0–B1.2 merged (last: #16, B1.2 polish).
- B2.1 zoom-out, Tasks 1–4 of `docs/superpowers/plans/2026-10-16-stage-b2-1-zoom-out.md`,
  are committed on the local branch `stage-b2` (not pushed yet), plus a
  platform fix: an old subclass no longer unhooks a newer one on a reused handle.
- Agent workflow: Trail code map and learnings, the owner-only guard,
  `CLAUDE.md` and this handoff (branch `agent-workflow`).

## State

- `stage-b2` has no upstream and no PR. Its in-game checks have not been run.

## Next

1. B2.1 Task 5: install the release build, have the user play the in-game
   checks of spec §7, write the smoke-test section and
   `docs/re-notes/b2-1-exit.md`, update the specs and issue #12.
2. Push `stage-b2`, open the PR, enable Auto-fix.

## Watch out for

- Ask before every `horned-rat.ini` change and restore it afterwards.
  Installing builds with `tools/install.ps1` is pre-approved.

## Continuation prompt

> Continue Stage B2.1 from Task 5 of
> `docs/superpowers/plans/2026-10-16-stage-b2-1-zoom-out.md` on branch
> `stage-b2` (Tasks 1–4 are committed, not pushed). Build release, install it
> with `tools/install.ps1`, then walk me through the spec §7 in-game checks
> one at a time. From what we confirm, write the smoke-test items and
> `docs/re-notes/b2-1-exit.md`, file open items as issues, push and open the PR.
