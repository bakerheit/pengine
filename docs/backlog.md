# Bulldog backlog

Single source of truth for tickets in flight, planned, retired, and grouped epics. PBD numbering is continuous; gaps and retired numbers are preserved for traceability. EPIC numbering tracks larger initiatives that span multiple PBDs.

Risk-register entries (in `docs/risk-register.md`) become tickets when their named trigger fires; until then they live in the register, not here.

Last updated: 2026-05-15.

---

## Active

### PBD-012 — Source art out of git (LFS migration)
**Status:** Planned, not urgent
**Source:** Risk register

Move source art (`.obj`, `.fbx`, paint PNGs) out of regular git into LFS or out-of-tree. Cooked outputs (`.emesh`, etc.) stay where the build expects them.

Pre-budget 1.5× per the "automation surfaces latent state" doctrine — expect to discover stray files (`.DS_Store`, build artifacts, accidentally-committed binaries) during the move.

### PBD-014 — CI setup
**Status:** Planned
**Source:** Risk register

Stand up CI on push/PR. Rolls in the GL-context-on-headless concern (headless Linux needs xvfb or equivalent; macOS runners need confirmation).

Pre-budget 1.5×. Likely surface areas: platform-specific build assumptions, env vars silently relied on, working-directory assumptions.

### PBD-016 — Main menu (M) — IN PROGRESS
**Owner:** Frank
**Assigned:** 2026-05-15
**Status:** In progress (first ticket; pre-work onboarding in flight)

**Goal:** Game launches into a main menu rather than directly into gameplay. Menu offers two entry points: "New Game" (starts gameplay as today) and "Dev Tools" (a submenu that currently holds one placeholder button, "Map Builder").

**Acceptance criteria:**
- On launch, the main menu is the first interactive screen (no gameplay running behind it).
- Main menu shows "New Game" and "Dev Tools" as selectable items.
- Selecting "New Game" enters gameplay exactly as the engine does today.
- Selecting "Dev Tools" navigates to a submenu containing a "Map Builder" item.
- The "Map Builder" item is a placeholder — selectable/focusable but its activation is a no-op (or a short "Coming soon" hint). No Map Builder functionality lives in this ticket.
- Back navigation works from Dev Tools → Main Menu (Esc, B button, or a "Back" item — Frank's call).
- Builds clean under the project's standard flags (no `-Werror` regressions).

**Out of scope:**
- Map Builder behavior. That's EPIC-001 / future tickets.
- Pause menu, settings menu, save/load.
- Visual polish beyond what's needed for the controls to be legible. Functionality first; we can prettify in a follow-up.
- Refactor of existing rendering / HUD / state code beyond the minimum needed to introduce an "in menu" vs "in game" state.
- Audio for menu navigation (deferred).

**Premise checks (do before coding):**
1. Does `Application.cpp` (or equivalent) already have a game-state concept (e.g. `enum class State { Menu, Playing }`)? If yes, extend it. If no, introducing one is part of this ticket — flag the scope expansion in the design note.
2. How does input currently route from SDL events into gameplay systems? Confirm the entry point before wiring menu navigation.
3. Is there an existing text/font rendering path (HUD components use textures — see `src/render/hud/`)? If so, reuse it. If not, the simplest acceptable approach is textured-quad text or sprite-based labels — anything that doesn't require pulling in a font engine.

**Rationale:** Required before adding the Map Builder (EPIC-001). Also unblocks future menu work (pause, settings, save/load) by establishing the game-state pattern.

**Doctrine reminders:**
- Premise verification before implementation (the "L and above" template; PBD-016 is M, so lighter ticket form — but premises 1–3 above are non-negotiable).
- Design checkpoint before code: post a brief (≤200 words) design note before writing any production code. The SDM (Alex) will sign off or push back.
- If a bug surfaces in adjacent code while you're working, file a follow-up ticket; do not fix-in-place.
- Context-capture at completion: at the end, write one short note answering "what context did this ticket build that isn't in any artifact?"

### PBD-018 — Vertical Slice Definition
**Status:** Planned
**Source:** Risk register

Define the first coherent vertical slice of Bulldog — the minimum playable loop that demonstrates "what this game is." Blocks meaningful playtest cadence.

### PBD-019 — Case studies (post-Frank-onboarding)
**Status:** Deferred commitment, activated by Frank's arrival
**Source:** Internal note

Write up 2–3 case studies of past Bulldog tickets (PBD-007 traffic split, PBD-015 doubled wheels, etc.) as institutional learning artifacts. Deferred until Frank is on the team so case-study work has more than one reader.

---

## Epics

### EPIC-001 — Create Map Builder
**Status:** Planned (not sized)
**Triggered by:** PBD-016 (main menu's "Dev Tools → Map Builder" entry point lands first)

**Goal:** An in-engine Map Builder accessible from the Dev Tools menu, enabling the team to author city layouts, road graphs, and item placements without hand-editing IPL files or rebuilding the engine.

**Open questions to resolve before sizing into PBDs:**
- **Output format.** Writes IPL files directly? A new editor-native format with an export step? In-memory only for v1 with serialization deferred?
- **Camera model.** Top-down 2D map view, free-fly 3D, or both? Top-down is cheaper and likely sufficient for layout work.
- **Tooling scope.** Placement only (drop a model at a position), or also road-graph editing, heightmap painting, cell-boundary editing?
- **Persistence.** Where do edits get saved? Round-trip through the asset cooker, or direct write to `assets/world/`?
- **Inspector vs. editor for v1.** Read-only inspector (visualize what the IPL loader saw) is much cheaper and would still be valuable as a debugging tool. May be the right v1.
- **Undo/redo.** Required for v1 or post-v1?

**Probable PBD breakdown (rough, pending design):**
- Inspector mode (read-only) — load and render current IPL data with a top-down camera, no editing.
- Edit mode v1 — placement only, in-memory edits, save-to-file.
- Road-graph editing — adds graph topology authoring.
- Heightmap / terrain authoring — separate concern, likely its own sub-epic.

---

## Retired (numbers preserved for traceability)

- **PBD-004** — Logger sweep. Premise was a grep error (logger is in fact heavily adopted via `PE_*` macros). Premise verification surfaced before implementation.
- **PBD-005** — "Land in-flight traffic.cpp WIP." Process theater — the work was going to happen regardless of a ticket.
- **PBD-008** — Not recorded in any artifact; presumed retired. (Gap in numbering surfaced 2026-05-15 during backlog reconstruction; if real history exists elsewhere, fold back in.)

---

## Done

| ID | Description | Landed |
|----|-------------|--------|
| PBD-001 | README initial draft | `e85b28e` |
| PBD-002 | docs/assets.md | `b321a29` |
| PBD-003 | Wire meshconv into build graph | `2db07ea`, `517a811` |
| PBD-006 | Traffic.cpp characterization tests | `79367a5` |
| PBD-007 | Traffic.cpp split (5 extractions) | `50e7063` → `8494efd` |
| PBD-009 | Risk register & doctrine draft | `440e52e` |
| PBD-011 | meshconv skinning regression fix | `2d432a4` |
| PBD-015 | Doubled-wheels fix (Car5_nowheels) | `b9d57c3` |

---

## Notes on gaps

PBD-010, PBD-013, PBD-017 don't appear in any record. If real history exists for these in someone's head or in chat, fold them in. Otherwise they remain unused gaps; future tickets continue from PBD-020.
