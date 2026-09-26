# Neighborhood work

## In progress

- No active implementation work.

## Built and checked in-game

- Filled ten vacant Pinatty Row perimeter blocks with a varied low/mid-rise L: old industrial buildings, shops, flats, stepped roofs, gables, rooftop tanks/HVAC, service alleys, and street-level shopfronts. The same 950 authored pieces feed rendering and 150 collision pieces. A whole-city audit now checks all 57 active lots across 1,596 pairs; daylight overhead, two street approaches, and the rebuilt city map pass with clean GL queues.
- Replanned the Halloway eastbound ramp terminal and the Plaza Ring north junction. The old authored points made two signal boxes only 7 m apart; the ramp signal now sits at (-46.207, 550), the Ring lands exactly on the North Arm at (-56.552, 650), and a full 100 m block separates them. The ramp terminal clears 36 vehicles with zero collision reactions, all four physical ramp drives arrive, and both street approaches were checked in the rebuilt game with a clean GL queue.
- Eleven authored Pinatty Row towers now form a dense east-side skyline: Halloway Annex, Ashford Exchange and Northline Tower join Juniper Spire, Cinder Pinnacle, Wren Financial, Eastbank Crown, Pinatty Gate, Mercer Exchange, Halloway Centre and Ashford House. The eight new parcels use unused 92 x 62 m grid blocks, retain sidewalk clearance, and appear as labeled lots on the city map. Focused tower geometry/access checks cover all eleven sites and their pairwise separation; daylight overhead and street-level runtime captures pass with a clean GL queue.
- Pinatty District Construction Yard now fills an unused east-side parcel with a 14-of-20-floor open steel frame, working tower crane, orange safety fencing, plywood site office/board, haul pad, dumpster, rebar and pipe racks, mixer, pallets, barrels and floodlight. The generated safety-mesh and plywood textures are loaded through the real start-site material path; focused geometry/collision checks and daylight runtime captures pass with a clean GL queue.
- Replaced the square asphalt shelves at the Halloway auxiliary viaduct forks with 60 m shoulder/gore tapers, then buried each lane-connected ramp throat 40 m inside the auxiliary deck so its square mesh cap cannot show beside the freeway. Through lanes and their paint stay fixed while the deck narrows, each entry ramp exclusively owns its new auxiliary lane, and both junction simulations clear with zero collision reactions. The exact eastbound approach and an overhead interchange view were checked in the rebuilt game with a clean GL queue.
- Brassline Arms across Sixth Street from the car wash, two blocks east and one block north of Second Chance Pawn: enterable staffed shop, display weapons, four parking bays, Sixth-only curb access, day/night lights, and both businesses on the map. Its current cell clears the hospital garage. Six focused suites pass. The counter sells the pistol and pistol ammunition (`--gun-store-check`); see `docs/design/gun-store.md`.
- Fixed the exact Spine / Sycamore Loop junction shown in the user's screenshot: removed crossed sidewalk strips from the visible road and collision surfaces. Eight 25m/s driving passes stay grounded with zero impacts; the rebuilt game view is clear. Outer sidewalks and separate bridge levels remain intact.
- Removed the raised sidewalk wedge from Route 1 / Kestrel Close near Sycamore. Twelve 25m/s lane passes stay grounded with zero impacts; before/after game views confirm the wedge is gone.
- Refined traffic lights with round lenses, deeper housings, visors, outlined backplates and mounting details. Their full cantilever assemblies now break from a hard player-vehicle hit, fall in the impact direction with every part attached, go dark, and leave no invisible upright collider. Low-speed brushes stay solid. The near-parallel Sycamore Loop return and Spine approach share one arterial mast instead of drawing two stacked heads. A deterministic in-game check covers the impact, streaming out and back, pass-through, reset, and clean GL state.
- Three-level airport garage across from drop-off, with driveable ramps, stairs, preserved entrances and terminal crossings.
- Six houses on Sycamore Loop, about 1.69km by road from the gas station, including a furnished enterable target house. Mission scripting remains to be designed.
- Pawn-shop parking faces the road, with wheel stops on the road side and its entrance moved beside the parking row.
- Alder Pip compact, Vesper Scythe sports car, and Halcyon Sovereign limousine, including opening doors and fitted entry/exit animations.
- Reduced road and forecourt shine, checked in dry and rainy conditions.
- Cloggers kitchen equipment aligned against walls, with clear working aisles.
- Expanded neighborhood plots to the sidewalks and moved affected parking rows. All 17 driveways pass.
- Retracted Ostend launch parking 8 m toward the docks while keeping its ramp edge fixed. Boatworks Road now branches from the north arm of Berth 2, follows the lot's north/east side with a 10 m centreline / 6 m pavement-edge gap, then bends south through the east entrance; the outbound wheel stops remain at the kerb-facing ends. Its road-level curb-cut T keeps Berth 2 and the opposite sidewalk whole, while the densely eased service-road centreline removes the old asphalt spikes through the bend. The throat blends from Boatworks Road's slightly skewed cap to Berth 2's edge instead of leaving a grass sliver. At the lot, Boatworks Road itself finishes with a straight full-width section that continues 2 m past the parking edge; no separate apron covers the join.
- Detailed pawn-shop televisions, radios and shaped guitars.
- Corrected gas-station clerk/register facing and shared the bank ATM.

- Halloway Fire Station has usable engine bays and clear road access.
- Halloway Police Station has a public entrance and vehicle parking.
- The Bent Elbow is a shabby neighborhood dive bar with a worn exterior, faded signs, grimy windows, a small bar room, pool table, and back-alley access south of the fire station.
- The Bent Elbow now has a five-space customer lot on its left side, with a dedicated curb-cut inlet from Mercer Avenue and a clear walk across the front pavement.

## Built and tested

- Crash audio increased by 25%; ongoing leak warnings repeat every four simulated seconds. Focused mixer/cadence tests and shared build pass. Repair, pause, and exit behavior checked.

## Later

- ~~Resolve the existing engine-profile uniqueness test failure: multiple cars currently share a pitch.~~ **Done — verified 2026-09-10.** All 27 catalog cars carry distinct pitches (0.80–1.115, no duplicates), and `catalog_models_have_distinct_rendered_engine_notes` in `tests/audio_vehicle_runtime_tests.cpp` asserts uniqueness across `kPlayerCars` and passes in the gate. Nothing to resolve; the line was outliving the failure.
- Create a beach, tentatively across the island from the docks. Confirm the shoreline location against the map before authoring terrain, paths and beach props.

The beach remains a planning item.
