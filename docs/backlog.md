# Bulldog backlog

Single source of truth for tickets in flight, planned, retired, and grouped epics. PBD numbering is continuous; gaps and retired numbers are preserved for traceability. EPIC numbering tracks larger initiatives that span multiple PBDs.

Risk-register entries (in `docs/risk-register.md`) become tickets when their named trigger fires; until then they live in the register, not here.

Last updated: 2026-05-16 (EPIC-001 design checkpoint landed; v1 inspector PBDs cut).

---

## Active

| ID | Title | Status | Owner | Notes |
|----|-------|--------|-------|-------|
| PBD-012 | Source art out of git (LFS migration) | Planned | — | Risk register; pre-budget 1.5×. Expect stray-file discoveries. |
| PBD-014 | CI setup | Planned | — | Pre-budget 1.5×. Rolls in GL-context-on-headless concern. |
| PBD-018 | Vertical Slice Definition | Planned | — | Blocks playtest cadence. |
| PBD-019 | Case studies | Deferred | — | Activated by Frank's arrival; ≥2 readers required for value. |
| PBD-020 | Pause / return-to-menu from gameplay | Planned | — | Surfaced by PBD-016. Asymmetry: enter gameplay, never return without quitting. |
| PBD-021 | Gamepad navigation for menus | Planned | — | Surfaced by PBD-016. Keyboard-only today. |
| PBD-022 | General 2D screen-space shader | Watch | — | Minimap shader doing 4× duty (minimap + crosshair + speedometer + menu). Not urgent. |
| PBD-023 | Map Builder AppState scaffold | In progress | Frank | EPIC-001 v1, ticket 1/5. Adds `AppState::MapBuilder`, wires menu entry, Esc back, empty render hook. (S) |
| PBD-024 | Top-down free-pan camera | Planned | — | EPIC-001 v1, ticket 2/5. Pitch-locked -89° camera, WASD pan, wheel zoom, drives `Streamer::pump`. (S) Blocked by PBD-023. |
| PBD-025 | Cell/road overlay rendering | Planned | — | EPIC-001 v1, ticket 3/5. `DebugDraw` cell boundaries, road centerlines, loaded vs unloaded color coding. (S) Blocked by PBD-024. |
| PBD-026 | Instance pick + inspector readout | Planned | — | EPIC-001 v1, ticket 4/5. Cursor raycast → show model id/name/flags/position/scale/cell. Adds const accessor on `Streamer`. (M) Blocked by PBD-024, 025. |
| PBD-027 | Cell-jump and stream-window controls | Planned | — | EPIC-001 v1, ticket 5/5. Typed cell coord re-centres camera + forces streamer load. Clamps to world bounds. (S) Blocked by PBD-024. |

## Epics

| ID | Title | Status | Notes |
|----|-------|--------|-------|
| EPIC-001 | Create Map Builder | v1 designed (M overall) | Inspector-only v1. 5 PBDs (023–027) sequenced. Design checkpoint 2026-05-16. |

**EPIC-001 — v1 design (signed off 2026-05-16):**

> v1 of Map Builder is a **read-only, top-down inspector** reached from Dev Tools → Map Builder. It loads cells via the existing `Streamer`, renders them with a steep-pitch camera locked to top-down, overlays the road grid and cell boundaries via `DebugDraw`, displays the model id / position / flags of the instance under a cursor, and exits back to Dev Tools on Esc. **It does not edit, save, undo, place, paint, or modify any world data.**

**Why inspector-first** (premise findings that reshaped the epic):
- **Road graph is not authored.** `RoadGraph` is derived from loaded `CellCoord`s; there is no per-edge data to edit. "Road-graph editing" as a v1 feature has no target.
- **Model palette is 8 cubes.** `ModelRegistry` loads `streets.ide` into 6 building variants + road + sidewalk, all `proc:cube`. An editor v1 would be a placement tool for 8 cubes.
- **`save_ipl` already exists.** `src/world/ipl_loader.h` is read/write-symmetric; the persistence concern is solved when needed.
- **`assets/world/cells/*.ipl` is gitignored** as a runtime cache (`.gitignore:14`). Deferred decision: when edit-mode lands, un-gitignore so authored cells become source. v1 reads whatever is there.
- **No orthographic projection exists.** Top-down is implemented as a steep-pitch (-89°) perspective camera — costs nothing, matches the existing Debug-Fly pattern.

**Open-question resolutions:**
- *Output format:* defer (read-only v1). When edit-mode lands, write IPL directly via existing `save_ipl`.
- *Camera:* top-down only, -89° pitch perspective. No new projection code.
- *Tooling scope:* inspection only. No placement, no road-graph, no heightmap painting.
- *Persistence:* N/A for v1. Edit-mode writes to `assets/world/cells/`.
- *Inspector vs editor v1:* **inspector.** Right-size over ambition.
- *Undo/redo:* N/A for v1.

**Editing lands as EPIC-001's v2** (separate scoping exercise) — palette UI, persistence semantics, undo/redo become real questions once editing is actually on the table.

**Sequencing:** PBD-023 → 024 → 025 → 026 → 027, single-dev sequential. No parallelism — all PBDs touch `Application.cpp` in overlapping regions; with no PR workflow or CI, parallel merges risk conflicts the team can't handle cheaply.

**Pre-budgeted latent-state surfaces** (per the "exercising code along new paths" doctrine, expect at least one discovery):
- Streamer behaviour when camera teleports (PBD-027 stresses this).
- IPL `lod_pair` round-trip gap — `load_ipl` parses it, `save_ipl` doesn't write it. Surfaces in PBD-026 readout.
- Cells outside world bounds — Streamer clamps; PBD-027 must too.
- `Streamer::loaded_` has no const accessor — PBD-026 needs to add one without racing the worker thread.
- Heightmap singleton init order — PBD-026 raycast pulls Heightmap onto a new exercise path.
- Procedural cell generation determinism — Map Builder will trigger cells the player never visits.

## Retired (numbers preserved for traceability)

| ID | Reason |
|----|--------|
| PBD-004 | Logger sweep — premise was a grep error (logger is heavily adopted via `PE_*` macros). |
| PBD-005 | "Land in-flight traffic.cpp WIP" — process theater. |
| PBD-008 | Not recorded in any artifact; presumed retired. Gap surfaced 2026-05-15 during backlog reconstruction. |

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
| PBD-016 | Main menu + dev tools placeholder | `55fadb3` |

---

## Notes on gaps

PBD-010, PBD-013, PBD-017 don't appear in any record. If real history exists for these in someone's head or in chat, fold them in. Otherwise they remain unused gaps; future tickets continue from PBD-028.
