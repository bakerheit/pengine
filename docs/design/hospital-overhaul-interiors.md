# Pinatty Regional Hospital public interior detail

`src/city/hospital_overhaul_interiors.h` adds fine-grain public-facing fixtures
to the north ground floor. The layer is baked into the integrated hospital part
stream after the shell, access systems, and public realm. All coordinates are
local to `kHospitalSite`; no new image assets or material finishes are used.

## Main lobby

- A staffed reception counter and three terminals sit west of the front-door
  axis. Its east end has a lower accessible counter section.
- Three self check-in kiosks sit on the east side of the room.
- Two short waiting rows include six individual chairs, two long benches, and
  two marked wheelchair spaces.
- Pharmacy pick-up and a water-refill station sit against the east edge.
- A suspended directory marks Emergency, Clinics, and Elevators over the rear
  hall with a 3 m clear underside.

The straight 9 m lobby aisle around local `x=0` stays clear from the north
entrance to the rear hall. Reception furniture ends west of `x=-4.8`; other
fixtures stay east of `x=10`.

## Diagnostics check-in and waiting

The diagnostic doorway at local `x=60` keeps an 8 m clear approach. Registration
is placed east of that route, with three terminal screens, a separate sign
band, eight waiting chairs, two benches, and two marked wheelchair spaces.
Fixtures stay on the ground floor and use solid collision only for furniture
bases and seat frames.

## Integration

`bake_polished_hospital_campus()` appends this stream once. It introduces no
new map entries, fitted texture receivers, runtime light names, road geometry,
or changes to the hospital shell. The sidecar remains separate so its furniture
can be adjusted without changing massing and circulation contracts.
