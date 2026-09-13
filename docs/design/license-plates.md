# State license plates

Road vehicles carry a registration consisting of a home state, plate series,
and serial number. Both ends of a car show the same registration; motorcycles
have a smaller rear plate. This includes the player car, moving traffic,
ambient parked cars, stolen cars, and cars the player leaves parked.

## Designs and formats

The authored catalog is `assets/data/license_plates.json`. `@` means a letter
from A–Z excluding I, O, and Q; `#` means a digit, including zero. All other
characters are fixed. Leading zeroes are significant.

| State | Series / design | Pattern |
| --- | --- | --- |
| O'Haven | Standard / Harbor Light | `@@@@-####` |
| O'Haven | Heritage / Evergreen | `####-@@@@` |
| O'Haven | Commercial / Working Harbor | `C@@@-#####` |
| O'Haven | Government / Public Service | `G@@@-#####` |
| Florangia | Standard / Sun Coast | `##@@-##@@` |
| Florangia | Heritage / Coral Sunset | `@@##-@@##` |
| Florangia | Commercial / Coast Carrier | `#####-@@@C` |
| Florangia | Government / State Service | `#####-@@@G` |

O'Haven uses a lighthouse, harbor blues, evergreen and work-truck gold.
Florangia uses a sun and shoreline, teal, coral and warm cream. Private cars
receive standard plates or a 20% heritage selection. Freight vehicles use
commercial plates; municipal and emergency vehicles use government plates.
The Rodeo Grazer is a private pickup.

## Assignment and persistence

Issuance is a pure hash of vehicle identity, slot, departure generation and
identity domain. It does not consume simulation RNG. Owned, moving and
ambient parked cars use separate domains. Negative departure generations are
supported. Traffic origin states are resolved once from each authored lane's
start position, using the same Florangia land mask as world content. Owned
cars use their initial spawn position. Driving across a state boundary does
not issue another plate.

Traffic rigs refresh when a new departure takes over the same lane/slot.
Theft copies the actual traffic registration to the player vehicle. Parked
clones retain their registration and share the plate mesh; repainting affects
body materials independently. Selecting a new vehicle model or mechanical
identity issues a registration for that new vehicle.

Checkpoint version 3 saves the registration explicitly. Versions 1 and 2
migrate deterministically from the saved vehicle key, model and position.
Malformed states, series, negative numbers and numbers outside the pattern's
capacity are rejected before applying the checkpoint. Existing state/series
IDs and shipped patterns must remain stable to preserve saved plates.

Serial spaces contain 2,798,410,000 private or 1,216,700,000 fleet numbers per
design. Hash issuance is reproducible, but is not a global collision-resolving
registration database. Plate text must not replace the full vehicle identity
in police, ownership or traffic bookkeeping. Trailers, boats and aircraft do
not use this road-vehicle registration system.

## Assets and mounting

Run `python3 tools/make_license_plates.py` after editing designs. It bakes the
C++ catalog, `assets/textures/vehicles/common/license_plates.png`, and the
review sheet `build/license-plates/designs.png`. `--check` detects stale
committed outputs. Fonts are the repository's licensed Bebas Neue and Roboto.

The shared atlas contains plate blanks and padded character strips. A small
continuous mesh grid selects the characters, avoiding per-car textures,
transparent layering, and cracks under PSX vertex snapping. Plate nodes
follow the body's transform and damage deformation. GPU meshes are released
after their last parked/active reference is removed.

`src/app/vehicle_plate_mesh.h` owns source-space mounts. The Grazer uses its
authored 305 × 152 mm bracket anchors. Legacy models are fitted against their
actual cooked front/rear surfaces, with multiple samples across each plate's
footprint, a slope fit, and a maximum gap of 45 mm across the backing surface.
The Fang Venom uses its existing 240 × 110 mm tail mount. The procedural
snowplow has explicit mounts for its opposite
forward axis. More authored mounts can replace a model's fallback here.

## Verification

`license_plate_tests` covers catalog completeness, format boundaries,
identity domains and generations, all available catalog vehicle mounts,
mesh winding/UVs, and checkpoint migration/rejection. Existing save and
tractor/trailer suites cover checkpoint integration.

The production car lab supports `--plate-design 0..7`, `--view plate-front`
and `--view plate-rear`. Add `--plate-check` to exercise cross-state retention,
parked cloning, 256 registration replacements and GPU resource release.
The game's `--vehicle-entry-check` also verifies registrations survive real
traffic theft, parking and re-entry.

Validated on September 12, 2026:

- Full CI: 200/202 passed. BWC snow/audio profiles changed during that run;
  both failing suites passed after a fresh full build. The final focused run
  passed all five plate, checkpoint, trailer, snow and audio suites.
- Four 60-frame production renders: O'Haven and Florangia on the Grazer,
  O'Haven heritage on the Halcyon, and Florangia heritage on the Fang. All
  reported zero GL errors. The Grazer run also passed the 256-swap lifecycle
  and GPU ownership check.
- The 1,800-frame real-game theft check preserved the traffic plate
  `CULC-33077` and original plate `UFWF-4273` through takeover, parking and
  re-entry. The GL error queue stayed clean. Its isolated save slot did not
  touch the normal checkpoint.

Artifacts and logs live in `build/license-plates/`.
