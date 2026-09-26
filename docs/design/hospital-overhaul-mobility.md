# Pinatty Regional Hospital public mobility layer

This layer implements the public-facing circulation from the hospital overhaul
master contract. Every coordinate is local to `kHospitalSite`; `+x` is grid
east and `+z` is grid south. It does not edit the road graph, north parking,
garage, ambulance loop, service court, clinical massing, or texture assets.

## Public arrival

The main doors remain on the shared `x=0` lobby axis. The north visitor lot's
existing pedestrian spine and Tenth Street crossing land on a 5.2 m protected
walk that continues straight to the lobby threshold at `z=-8`.

Public vehicles enter from a western Tenth Street curb cut, travel east once
past the passenger curb, and exit through a separate eastern Tenth curb cut.
Both mouths are spaced away from the Bellweather and Juniper junctions. This
creates one simple decision path instead of sending visitors around the
clinical campus. The clear swept envelopes are exported as constants in
`hospital_overhaul_mobility.h`:

- Main curbside lane: `x[-41,42]`,
  `z[-18.2,-11.2]`.
- Tenth entry and 8.5 m-radius swept turn: `x[-39,-22.5]`,
  `z[-31,-10.7]`.
- Tenth exit: `x[35.5,42.5]`, `z[-31,-18.2]`.
- Lobby walk: 5.2 m clear on `x=0`, crossing the vehicle route once.

The covered curb has one accessible short-stay bay, one general short-stay
bay, and two taxi-length spaces. The crossing is visible as a flush concrete
table with six white bars, paired tactile strips, and guard bollards outside
the clear width. Canopy columns are on the refuge islands and behind the
passenger curb, never in any vehicle sweep.

Two low planted refuge islands organize the forecourt while preserving the
north-lot crossing gap and both Tenth Street sight lines. Direction
arrows show the eastbound lane and northbound exit without requiring a raster
texture. The teal and yellow curb bands distinguish short-stay and taxi use.
The entry uses nine overlapping 18-sided paving sections to form one smooth
7.5 m-wide quarter-circle instead of a square throat-to-lane intersection.

Paired teal inlays trace the protected walk from the north-lot landing through
the vehicle crossing to the lobby threshold. A flush teal destination tile
marks the door landing. The arrival wayfinding pylon stands beside the
crossing on the forecourt apron, outside both refuge planters and the public
vehicle sweep.

## Walking, transit, and bicycles

A connected 3.2 m public path follows the north and west clinical faces, wraps
the southwest bar, and reaches the garage on the protected `x=46` ground-walk
axis. The garage path has clear landings at the hospital south opening and the
existing garage north door. It stops at the `x=144` operational boundary;
there is deliberately no public furniture or paving across the ambulance and
southeast service zones.

The Bellweather transit stop sits near local `z=50`, well south of the public
arrival sight triangle. It has a 6.8 m boarding pad, tactile boarding edge,
step-free transverse connector, shelter, seating, and stop marker. A separate
four-stand bicycle pad sits north of it beside the west perimeter path. Neither
facility narrows a vehicle lane or the protected pedestrian route.
The accessible connector has paired teal edge inlays and a flush clinic arrow.
The shelter bench sits at its north end so its solid base clears the `z=50`
connector. A small bus pictogram tops the existing stop pole.

Three material-built wayfinding pylons mark the arrival, transit/bicycle, and
garage decisions. Their hospital crosses and color bands use existing
`StartFinish` materials; no new or repeated image texture is introduced.

This pass adds fourteen details, bringing the sidecar from 162 to 176 parts.
The edited pylon and bench positions received a static geometry review, but
the revised layer has not been built or checked in the running game.

## Integration notes

- Bake `bake_hospital_overhaul_mobility()` into the shared hospital part stream
  after the replacement massing and before collision/GPU upload.
- Keep the north lot and garage modules separate; this layer only supplies
  their landing connections.
- `hospital grounds path light lens` and `hospital arrival canopy light lens`
  intentionally reuse existing runtime light receiver names. Parent
  integration must keep routing those visible lenses to real dusk lights.
- Parent tests should assert that every ground-level solid mobility part stays
  outside `hospital_overhaul_public_vehicle_sweep_contains()`, except curbs
  that define its outer boundary. Overhead canopy structure must retain vehicle
  clearance. Tests should also walk the route from the north lot to the lobby
  and from the southwest path to the garage door.
- Street-level QA should check both Tenth Street approaches, the single
  protected lobby crossing, transit boarding clearance, and the
  garage ground walk in both day and night scenes.
