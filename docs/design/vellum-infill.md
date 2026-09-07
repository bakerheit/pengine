# Vellum perimeter infill

Twelve finished background buildings occupy genuinely empty west, south, and
central Vellum Row cells. Ten form one L around the existing construction
sites; two close visible holes in the older central fabric without replacing a
road, sidewalk, active parcel, or terrain transition.

| Building | Vellum grid centre | Floors | Lot |
|---|---:|---:|---:|
| Briar Needleworks | `(-322, -279)` | 4 | 54 x 38 m |
| Briar Cold Storage | `(-322, -217)` | 6 | 54 x 38 m |
| West Vellum Mercantile | `(-322, -155)` | 8 | 54 x 38 m |
| Foundry Court Apartments | `(-322, -93)` | 5 | 54 x 38 m |
| Copperleaf House | `(-322, 31)` | 7 | 54 x 30 m |
| Vellum Printworks | `(-322, 93)` | 9 | 54 x 38 m |
| Mercer Arcade | `(-230, 155)` | 6 | 54 x 38 m |
| Bellweather Rooms | `(-138, 155)` | 4 | 54 x 38 m |
| Rookery House | `(-46, 155)` | 8 | 54 x 38 m |
| Juniper Market Flats | `(46, 155)` | 7 | 54 x 38 m |
| Mercer Textile Exchange | `(-230, -155)` | 6 | 54 x 38 m |
| Bellweather Pharmacy Offices | `(-230, 93)` | 5 | 54 x 38 m |

The `(-322, 31)` lot is only 30 m deep because a standard 38 m parcel would
enter the Halloway arterial ribbon. The other candidate cells on the outer
east and far south edges were rejected because the Nickel Heights and
Halloway Square terrain feathers pull them away from Vellum's 12 m datum.

Each building has its own floor count, facade/trim pair, bay rhythm, roofline,
and rooftop silhouette. Repeated authored details include shopfront panes,
solid non-enterable doors, entrance canopies, planters, rear loading steps,
service doors, utilities, refuse bins, and fire escapes. All geometry is baked
from `kVellumInfillParcels`; the same solid pieces feed collision, and the lot
plus building footprints feed the city map.

## Validation

`vellum_infill_tests` checks the perimeter and central cells, terrain support, road-and-sidewalk
clearance, low-piece parcel bounds, geometry validity, collision presence,
variation, and a bounded node budget. `authored_city_layout_tests`
checks all 73 active lots across 2,628 pairs and verifies the 63 Vellum lots
against terrain.

The rebuilt game was checked from a district overhead and two daylight street
approaches. Each 300-frame run completed with a clean GL queue. The rebuilt map
lab also rendered the new lots/buildings, plus the previously missing Bent
Elbow, emergency-station, and Sycamore-house footprints, with clean GL state.

```sh
./build/bin/apricot --start-at -113.0 -59.2 --start-heading -174 --road-start --overhead --frames 300 --clear --save-file /tmp/apricot-vellum-overhead-save.json --screenshot build/vellum-infill-overhead.bmp
./build/bin/apricot --start-at -237.3 -197.0 --start-heading -174 --road-start --start-driving --frames 300 --daylight --clear --save-file /tmp/apricot-vellum-west-save.json --screenshot build/vellum-infill-west-street.bmp
./build/bin/apricot --start-at 156.5 93.8 --start-heading -84 --road-start --start-driving --frames 300 --daylight --clear --save-file /tmp/apricot-vellum-south-save.json --screenshot build/vellum-infill-south-street.bmp
./build/bin/apricot_map_lab --zoom 5 --x -80 --z -60 --layer 0 --width 1280 --height 800 --screenshot build/vellum-infill-map.png
```
