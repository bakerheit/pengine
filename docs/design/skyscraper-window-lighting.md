# Skyscraper window lighting

The finished Pinatty skyline has twenty rendered skyscraper shafts: sixteen
active neighborhood towers and four towers on the two Pinatty twin blocks.
Mercer Exchange is authored but not rendered because the hospital campus owns
its parcel. The open construction frames and the airport control cab are not
office-occupancy towers and stay outside this system.

## Facade layer

The original towers used one dark glass sheet per whole face and tier. Those
sheets remain as the reflection/dark-room backing. A second shallow, non-solid
box is authored for every suite window:

- neighborhood towers: 8 bays on front and rear, 6 on each side, 28 per floor;
- twin towers: 5 bays on all four faces, 20 per floor;
- live finished skyline: 13,468 independently addressable tower panes;
- occupied Pinatty infill: 393 existing upper-floor apartment panes use the same
  schedule, without adding duplicate geometry.

Every overlay shares the ordinary box mesh and opaque material. Per-instance
tint and visibility preserve batching. Off overlays disappear and expose the
dark glass sheet; the existing infill pane itself instead returns to dark-cyan
glass.

## Occupancy schedule

The underlying dynamic window decision is a pure function of:

1. session seed;
2. absolute 120 Hz simulation step;
3. stable authored building, floor and suite keys;
4. the visible time of day;
5. the visible day-clock rate, used to lock occupancy to each suite's beat;
6. visible darkness.

This gives every new session a different but deterministic base pattern. The
runtime LOD then caches each pane's latest occupancy while the player moves
around the city. A building's lamp temperature comes only from its stable
authored building key, so every lit window on that building shares one
permanent colour across sessions and nights. The building palette is limited
to yellowish and whiteish lamp shades; blue and orange are deliberately
excluded. Absolute step drives the staggered room timers, so nearby windows
keep changing during the fixed-midnight `--night` QA mode. Visible sky darkness
is the final gate, so emissive alpha is exactly one and no overlay is shown in
daylight.

Office, residential and mixed-use profiles have different evening and
late-night occupancy curves. Each suite owns stable vacancy, security,
intensity and dwell traits; colour temperature belongs to the whole building.
Floor-scale bias creates small clusters, but suite timers remain staggered so
a facade never pulses as one unit. Each nearby suite holds its state for 60 to
180 seconds.

## Distance LOD handoff

A tower enters dynamic lighting inside 300 metres and stays dynamic until it
passes 420 metres. The 120-metre hysteresis band prevents repeated LOD swaps
when a car or camera hovers near one cutoff.

The first far view starts from a deterministic evening pattern. Approaching a
tower loads the dynamic panes in those exact lit and dark positions. They hold
for one full per-suite interval before updating. Leaving the 420-metre radius
copies no new pattern and causes no facade swap: the latest dynamic occupancy
is already cached and simply becomes that tower's static runtime LOD. Time and
schedule changes stop there, while the shared daylight/darkness gate remains
live.

## Lighting budget

Upper-floor panes are emissive skyline surfaces, not tiled point or spot
lights. Thousands of overlapping real lights would dominate the tiled forward
pass. Street-level lobby or canopy fixtures may add a small, distance-culled
spill rig separately; that budget is not coupled to the window count.

## Validation

Headless checks cover exact facade cell counts, bounded dimensions and
non-solid geometry, deterministic sampling, seed/address variation, profile
density, minimum dwell, fixed-midnight changes, exact near/far handoff,
hysteresis behavior, static far-state retention and the daylight emission gate.
Runtime QA uses fixed-seed day and night captures. Headless timing coverage
proves the longer dwell because waiting through a full three-minute maximum is
not useful for a visual smoke. The images must show dark and lit panes mixed on
every visible face, yellow/white variation between buildings, one colour within
each building, no blue or orange, no full-face pulse, no z-fighting and a clean
GL queue.
