# Hospital interior detail team contract

All work uses the existing four-story ring-and-spine hospital on local `main`.
Coordinates are `kHospitalSite` local; +Z is south. Ground floor top is 0.30 m;
upper slab underside is 3.50 m. New floor skin top is 0.315 m. Furniture bases
may start at 0.315 m. Preserve the shell and existing entrances.

## Ownership and layout

Each geometry module owns a new header `src/city/hospital_detail_NAME.h`, with
`inline std::vector<StartPart> bake_hospital_detail_NAME()`. Include only
`city/hospital_campus.h` and needed standard headers. Parent appends these
modules through `hospital_overhaul_interiors.h` and owns that existing file.

- reception: x[-14,-5], z[8,18]. Back-office detail behind existing desk at
  z=6.8; no duplicate counter. 80 parts maximum.
- pharmacy: x[28,43], z[8,11]. Shelves/storage behind existing pickup counter
  at z=5.8. Keep pickup approach north of counter open. 80 parts maximum.
- waiting: main lobby x[8,43], z[11,28]. Existing chairs at x11/14/17,
  z13.2/20.2; benches at x34 on those rows; wheelchair spaces at x23.2.
  Add arms, tables, literature and distinct comfort detail; do not duplicate
  seats. Parent changes seats to bottom0.66/top0.78 and backs bottom0.76.
  Bench seat bottom0.66/top0.80, back bottom0.78. 80 parts maximum.
- diagnostics: x[70,90], z[16,31]. A compact imaging/preparation suite, with
  real access gaps and meaningful medical equipment. Keep x56..64 entry axis
  clear and existing seating starting at x98 untouched. 100 parts maximum.
- emergency: east bar x[153,195], z[45,112]. Treatment stations north of
  z74 or south of z84. Keep z74..84 as continuous clear trauma corridor and
  x196..204 clear for east entrance circulation. 110 parts maximum.
- ward: west bar x[-11,27], z[95,125]. Four readable patient stations with
  beds, storage, visitor items and privacy. Keep x30..45 and z84..92 clear
  for circulation to the court and public bar. 110 parts maximum.
- surfaces: ground-floor skins/ceiling panels in the five clinical bars,
  selective wall finish and crash rails with real door gaps, and at most 40
  fixtures named exactly `hospital interior ceiling light lens`. Floor skin
  bottom0.30 height0.015, non-solid; ceiling bottom3.43 height0.04, non-solid.
  Parent connects these lenses to real lights, on during day and night.
  Keep ceilings off both open courts. 120 parts maximum.

Preserve main central aisle x[-4.5,4.5] through the north bar and diagnostic
approach x[56,64]. New props must not occupy these routes. Use actual physical
dimensions: beds about2.2x1m, seat tops0.48m above floor. High fixtures stay
below3.43m. Small garnish and fabric need no collision; furniture bases do.

## Shared texture protocol

Every new name starts `hospital detail `. Append one exact suffix to select a
texture: ` tex terrazzo`, ` tex upholstery`, ` tex laminate`, ` tex curtain`,
` tex wallpaint`, ` tex ceiling`, ` tex steel`, ` tex screen`,
` tex reception-sign`, ` tex pharmacy-sign`, ` tex diagnostics-sign`,
` tex emergency-sign`, ` tex ward-sign`, or ` tex directory-sign`.
The final screen pass also adds ` tex records-screen`, ` tex vitals-screen`,
and ` tex scale-screen` for front desk terminals, bedside monitors and scales.
Use `StartFinish::White` for textured receivers so art is not double tinted.
Signs/screens are single image faces; signs are north-facing thin boxes unless
specified otherwise and use width:height4:1. Screens use4:3. Do not put whole
sign compositions on whole furniture bodies. Surface textures tile at a real
world scale. Existing generic hospital names are remapped by the parent.

Texture author owns `tools/make_hospital_interior_detail_textures.py`,
`assets/textures/world/hospital/interior_detail/` and its provenance note.
Make deterministic original PNG textures: seven tileable material swatches
named terrazzo/upholstery/laminate/curtain/wallpaint/ceiling/steel.png; six signs
named reception-sign/pharmacy-sign/diagnostics-sign/emergency-sign/ward-sign/
directory-sign.png (4:1); screen.png (4:3), a fictional 1991 CRT display.
Use warm off-white, subdued teal, pale oak and brushed steel. Legible actual
sign copy, subtle wear, no protected red-cross emblem. No new external art.

Material-routing author owns only `src/city/hospital_interior_materials.h`:
define a headless descriptor table with suffix, asset-relative path, fitted
flag, tile span in metres and helper returning descriptor index for a name.
Use namespace apricot::city, names kHospitalInteriorMaterials and
hospital_interior_material_index(const char*). Return -1 for nonmatches.
Parent owns world.cpp/app.cpp loading, UV application and lights.

Do not edit other owners' files. You are sharing a checkout: preserve others'
changes. No nested agents. Report geometry bounds, piece count, names and any
remaining concern. Parent builds, runs focused checks and captures rooms.
