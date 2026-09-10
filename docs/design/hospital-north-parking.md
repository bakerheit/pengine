# Pinatty Regional Hospital North Visitor Parking

The north lot is one open 146 x 38 m surface lot directly across Tenth Street
from the hospital. It uses the same six-degree Pinatty basis as the campus but
is a separate `StartSite` centred at hospital-local `(46, -62)`. Its south
edge is `z = -43`; the Tenth Street centreline is `z = -31`, leaving the full
road and sidewalk ribbon between parking and clinical frontage.

`src/city/hospital_north_parking.h` authors the lot as one collision-supported
asphalt surface with three long parking bands, one 11.2 m cross aisle, a 5 m
pedestrian spine, a painted lobby crosswalk, four protected light islands, and
curbs split around the vehicle throat and pedestrian opening. Roughly 100
marked spaces make the scale legible while deliberate gaps keep entrances,
walking space, and lamp islands clear.

The exact `hospital parking lot light lens` pieces feed real downward tiled
lights after dusk. The sign receiver
`hospital north parking wayfinding fitted face` maps its generated image once
at UV `[0,1]`; it is never tiled, mirrored, or reused on another model face.
The receiver sits on the north/readable side of its steel backing so the panel
faces approaching visitor traffic instead of being hidden behind the sign.

The vehicle aisle aligns with the short surviving Rook Lane north stub. It ends
inside the lot and does not restore the road through the hospital. The lot's
east edge stops before the diagonal arterial, which remains untouched.
Ambulances use the separate receiving loop on the south side of Tenth Street.
