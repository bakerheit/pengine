# Bulldog backlog

Single source of truth for tickets in flight, planned, retired, and grouped epics. PBD numbering is continuous; gaps and retired numbers are preserved for traceability. EPIC numbering tracks larger initiatives that span multiple PBDs.

Risk-register entries (in `docs/risk-register.md`) become tickets when their named trigger fires; until then they live in the register, not here.

Last updated: 2026-05-16 (EPIC-001 v1 shipped; EPIC-002 v1 design checkpoint signed off).

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
| PBD-028 | `save_ipl` round-trips `lod_pair` | Bundled | — | Surfaced by PBD-026. Bundled into PBD-033 — editing forces the round-trip to be correct. |
| PBD-029 | `Menu::draw` contract / background-quad gotcha | Planned | — | Surfaced by PBD-026. Frank worked around by adding `Menu::draw_text_lines`. Rename or add `paint_background` flag. (S) |
| PBD-030 | Asset palette UI | In progress | Frank | EPIC-002 v1, ticket 1/5. Sidebar listing all `ModelRegistry` entries; up/down to select; state on Application. No placement. (S) |
| PBD-031 | Place instance under cursor | Planned | — | EPIC-002 v1, ticket 2/5. Click/Enter places selected palette model at cursor world XZ. Adds `Streamer::add_instance` (main-thread only). Extends `WorldCollision` for buildings. (M) Blocked by PBD-030. |
| PBD-032 | Delete instance under cursor | Planned | — | EPIC-002 v1, ticket 3/5. Reuses `query_instance_at`; on delete key removes from Streamer + scene + collision. Inspector labels road slabs "visual only" so deletion is honest. (S) Blocked by PBD-031. |
| PBD-033 | Persist edits via `save_ipl` on cell evict | Planned | — | EPIC-002 v1, ticket 4/5. Dirty cells write to `assets/world/cells/cell_X_Z.ipl` before evict. **Bundles PBD-028.** Un-gitignores cells dir as part of this PBD. (M) Blocked by PBD-031. |
| PBD-034 | Undo/redo command stack | Planned | — | EPIC-002 v1, ticket 5/5. `EditCommand` vocabulary (Place/Delete), fixed-depth ring buffer, Ctrl-Z/Ctrl-Y. Cleared on cell evict for v1. (M) Blocked by PBD-031, 032. |

## Epics

| ID | Title | Status | Notes |
|----|-------|--------|-------|
| EPIC-001 | Create Map Builder | v1 shipped 2026-05-16 | Inspector-only v1. 5 PBDs (023–027) shipped. |
| EPIC-002 | Map Builder editing | v1 designed (M overall) | Palette + plop + delete + save + undo. 5 PBDs (030–034). Sequenced. Design checkpoint 2026-05-16. |
| EPIC-003 | Authored road network (engine) | Planned (L, 4–6 PBDs) | Engine work. Replace constants-derived road grid with stored per-segment data. Heightmap reads from `RoadGraph` not `classify_road_band`. Unblocks real road add/remove tools. Not sized into PBDs yet. |
| EPIC-004 | Curved roads (engine) | Spike pending | Major engine work. Touches road representation, mesh gen, AI lane following (rewrite), intersections, heightmap carving, streamer cell-alignment. Architect estimate: 6–10 PBDs, several L+. Next step: a single-PBD spike on the road-representation design, not a full epic scope yet. |

### EPIC-001 — v1 design (signed off 2026-05-16, shipped 2026-05-16)

> v1 of Map Builder is a **read-only, top-down inspector** reached from Dev Tools → Map Builder. It loads cells via the existing `Streamer`, renders them with a steep-pitch camera locked to top-down, overlays the road grid and cell boundaries via `DebugDraw`, displays the model id / position / flags of the instance under a cursor, and exits back to Dev Tools on Esc. **It does not edit, save, undo, place, paint, or modify any world data.**

**Why inspector-first** (premise findings that reshaped the epic):
- **Road graph is not authored.** `RoadGraph` is derived from loaded `CellCoord`s; there is no per-edge data to edit. "Road-graph editing" as a v1 feature has no target.
- **Model palette is 8 cubes.** `ModelRegistry` loads `streets.ide` into 6 building variants + road + sidewalk, all `proc:cube`. An editor v1 would be a placement tool for 8 cubes.
- **`save_ipl` already exists.** `src/world/ipl_loader.h` is read/write-symmetric; the persistence concern is solved when needed.
- **`assets/world/cells/*.ipl` is gitignored** as a runtime cache (`.gitignore:14`). Deferred decision: when edit-mode lands, un-gitignore so authored cells become source. v1 reads whatever is there.
- **No orthographic projection exists.** Top-down is implemented as a steep-pitch (-89°) perspective camera — costs nothing, matches the existing Debug-Fly pattern.

### EPIC-002 — v1 design (signed off 2026-05-16)

> v1 of Map Builder editing is **palette + plop + bulldoze + save + undo for visual instances**. Reached from Map Builder (already exists). A sidebar palette lists every `ModelRegistry` entry. Clicking (or Enter) places one instance of the selected model at the cursor's world XZ. Delete removes the picked instance. Edited cells save through the existing `save_ipl` on evict or on Esc-to-Dev-Tools. Ctrl-Z / Ctrl-Y undo/redo over Place/Delete commands. **It does not add or remove roads as a network**, edit the heightmap, or support curved geometry.

**The road distinction** (load-bearing for what v1 does NOT do):
- **Visual road slabs** are `InstanceDef`s (`model_id=20`) emitted by `generate_city_cell` into IPL. Editable like any other instance in v1. **Deletion is cosmetic only** — the AI still drives where the slab was; the heightmap is still flat-carved.
- **The road network** (grid, intersections, AI navigation, heightmap carving) is implicit from constants in `road_grid.h` and `classify_road_band`. There is no per-edge data. **Real road add/remove requires EPIC-003** to land first.

The inspector readout in PBD-032 labels road slabs "road slab (visual only)" so users understand what deletion does — and doesn't — mean.

**Open-question resolutions** (signed off 2026-05-16):
- *Phase A (palette + plop + bulldoze) as EPIC-002 v1:* approved.
- *Cells gitignore status:* un-gitignore `assets/world/cells/*.ipl` when PBD-033 lands. Coordinate with PBD-012 (LFS).
- *Curved roads:* filed as separate EPIC-004 with a spike-first approach. Not bundled into EPIC-003.

**Sequencing:** PBD-030 → 031 → 032/033 (032 and 033 can technically parallel after 031 but bias sequential given no CI/PR workflow) → 034. Single dev. All PBDs land in `Application.cpp` overlapping regions plus a new mutating surface on `Streamer`.

**Pre-budgeted latent-state surfaces:**
- **`save_ipl` round-trip gaps beyond `lod_pair`.** Quaternion drift, `uv_scale_override == (0,0)` sentinel ambiguity, `%.4f` precision loss. PBD-033 is the load-bearing test of round-trip correctness.
- **`Streamer::loaded_` mutation invariants.** Parallel arrays (`nodes`, `instances`, `instance_world_aabbs`) need length discipline; PBD-031 introduces the mutating API.
- **Cell dirty-tracking interacting with eviction.** Edits behind a panning camera need to survive eviction.
- **Procedural cells losing procedural identity once edited.** Future `generate_city_cell` changes won't propagate to any cell the user has touched in Map Builder. Document this.
- **`WorldCollision` per-instance add/remove.** Today it adds in bulk; PBD-031/032 introduce the per-instance path.

---

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
| PBD-023 | Map Builder AppState scaffold | `d69468a` |
| PBD-024 | Top-down free-pan camera | `3ec451e` |
| PBD-025 | Cell/road/intersection overlay | `fbc1d3b` |
| PBD-026 | Instance pick + inspector readout | `01e24ee` |
| PBD-027 | Cell-jump input | `b8a3f55` |

---

## Notes on gaps

PBD-010, PBD-013, PBD-017 don't appear in any record. If real history exists for these in someone's head or in chat, fold them in. Otherwise they remain unused gaps; future tickets continue from PBD-035.
