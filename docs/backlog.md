# Bulldog backlog

Single source of truth for tickets in flight, planned, retired, and grouped epics. PBD numbering is continuous; gaps and retired numbers are preserved for traceability. EPIC numbering tracks larger initiatives that span multiple PBDs.

Risk-register entries (in `docs/risk-register.md`) become tickets when their named trigger fires; until then they live in the register, not here.

Last updated: 2026-05-16.

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

## Epics

| ID | Title | Status | Notes |
|----|-------|--------|-------|
| EPIC-001 | Create Map Builder | Planned (not sized) | Entry point landed in PBD-016 (`activate_menu_selection`). See open questions below. |

**EPIC-001 — open questions to resolve before sizing:**

- **Output format.** IPL files directly? A new editor-native format with an export step? In-memory only for v1, serialization deferred?
- **Camera model.** Top-down 2D vs free-fly 3D vs both. Top-down is cheaper and likely sufficient for layout work.
- **Tooling scope.** Placement only, or also road-graph / heightmap / cell-boundary editing?
- **Persistence.** Round-trip through the asset cooker, or direct write to `assets/world/`?
- **Inspector vs editor for v1.** Read-only inspector is much cheaper and would still be valuable as a debugging tool — may be the right v1.
- **Undo/redo.** v1 or post-v1?

Probable PBD breakdown (rough, pending design): inspector-mode → edit-mode v1 (placement) → road-graph editing → terrain authoring (likely its own sub-epic).

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

PBD-010, PBD-013, PBD-017 don't appear in any record. If real history exists for these in someone's head or in chat, fold them in. Otherwise they remain unused gaps; future tickets continue from PBD-023.
