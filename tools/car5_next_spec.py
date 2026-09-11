"""Measured anchors for the Car 5-NEXT articulated driver door.

Car 5 is an imported legacy body, not a generated one, so there is no loft to
split at author time.  The door is cut out of the cooked shell instead, and
these numbers are the cut.  They are in Car 5 SOURCE units (+X driver side,
+Y up, +Z nose), which are NOT metres: the catalog fits the body with
scale_x = half_track / wheel_x and scale_y = scale_z = 2*half_wheelbase /
(wheel_front_z + wheel_rear_z).  Keep them in sync with kCar5NextDoor* in
src/app/car5_next_door.h and the layout in vehicle_driver_pose.h.

The Z bounds come off the painted shut lines in textures/vehicles/car5/body.png
(front fender seam and B-pillar), not from round numbers; the side section is
constant along the cabin, so a straight cut lands on both.
"""

# Default chassis the catalog fits every body to (physics/vehicle.h).
HALF_TRACK = 0.78
HALF_WHEELBASE = 1.35

# Car 5 catalog row (player_car_catalog.h).
WHEEL_X = 1.038
WHEEL_FRONT_Z = 2.254
WHEEL_REAR_Z = 1.813
ARCH_CENTRE_Y = 0.45
WHEEL_RADIUS = 0.32

SCALE_X = HALF_TRACK / WHEEL_X
SCALE_Z = 2.0 * HALF_WHEELBASE / (WHEEL_FRONT_Z + WHEEL_REAR_Z)
# Source Y of the ground plane: the arch centre is the axle, one radius up.
GROUND_Y = ARCH_CENTRE_Y - WHEEL_RADIUS / SCALE_Z

DOOR = {
    "hinge": (1.196, 0.62, 1.315),
    "handle": (1.230, 1.24, 0.07),
    "front_z": 1.33,
    "rear_z": -0.18,
    "sill_y": 0.62,
    "belt_y": 1.24,
    "top_y": 1.88,
    "inner_x": 0.70,
    # The A-pillar rakes: the greenhouse's front edge fits z = -1.3836*y +
    # 3.2538 over its whole height (tools measure it off the cooked shell).
    # The window frame follows it, set 0.09 behind so the cut lands in the
    # middle of the glass panel instead of on its shared edge with the screen.
    "pillar_rake": 1.3836,
    "pillar_intercept": 3.2538 - 0.06,
    "open_degrees": -68.0,
}

# Shell thickness for the door panel: enough that the open panel reads as sheet
# metal rather than paper from the side.
THICKNESS = 0.09

# The cabin lining is thinner than the door on purpose.  It sets where the
# headliner sits, and this body is squashed to .664 of its authored height, so
# every centimetre of it comes straight off the driver's headroom.
LINER_THICKNESS = 0.05

# The wing mirrors are already closed solids hanging off the skin, thinner than
# THICKNESS in places, so insetting one turns it inside out and scatters stray
# faces across the doorway -- and each sits inside its front window's outline,
# where glazing it would make a see-through mirror.  They are never lined and
# never glazed; the driver's rides with the door as authored.  Measured off the
# cooked shell on the driver's side and applied to |x|, since the pair mirror.
MIRROR = {"x": (1.15, 1.42), "y": (1.30, 1.55), "z": (0.73, 0.94)}

# Cabin band that gets an inset inner shell, so an open door never looks
# through the far side's back faces into the world.
CABIN = {
    "z_rear": -2.45,
    "z_front": 1.95,
    "y_min": 0.30,
    "y_max": 2.10,
    "x_max": 1.30,
    # The floor is 0.21 source (0.14 m) below the sill, exactly as far down as
    # the 91-C's is below its own: you step over a sill, you do not stand on it.
    "floor_top_y": 0.42,
    "floor_bottom_y": 0.33,
    "floor_half_x": 1.14,
}

# Seats and dashboard, as plain closed blocks.  They are what the player
# actually looks at through an open door, and they stop a grazing view from
# reaching past the far side.
SEATS = [
    # (name, x0, x1, y0, y1, z0, z1) blocks, all closed and outward-facing.
    ("DriverCushion", 0.14, 1.08, 0.42, 0.72, -0.30, 0.52),
    ("DriverBackrest", 0.14, 1.08, 0.42, 1.44, -0.60, -0.30),
    ("PassengerCushion", -1.08, -0.14, 0.42, 0.72, -0.30, 0.52),
    ("PassengerBackrest", -1.08, -0.14, 0.42, 1.44, -0.60, -0.30),
    ("RearBench", -1.08, 1.08, 0.42, 0.72, -1.34, -0.82),
    ("RearBackrest", -1.08, 1.08, 0.42, 1.44, -1.70, -1.34),
    ("Dashboard", -1.14, 1.14, 0.42, 1.30, 1.02, 1.60),
    ("Tunnel", -0.16, 0.16, 0.42, 0.60, -0.82, 1.02),
]

# The catalog squashes Car 5's imported body to .664 along and up, which leaves
# 1.30 m from the cabin floor to the headliner where the 91-C has 1.07 m of
# usable height above a floor at the same height.  A 1.76 m rig seated at the
# 91-C's hip puts its head 10 cm through this roof, so the whole rig drops by
# this much and reclines (kCar5NextRecline).  Measured, not guessed: the
# headroom probe in tests/vehicle_driver_pose_tests.cpp pins the result.
# Feet are exempt -- both cabin floors are 0.30 m off the ground.
SEAT_DROP_M = 0.08

# Car 5 paints its windows onto the shell.  Car 5-NEXT cuts them out and hands
# them to the renderer's glass material instead, which is the only way they can
# be see-through.  Every bound below was read off the paint: the greenhouse is a
# tent (belt ring y=1.44 at x=+/-1.15, roof ring y=2.02 at x=+/-0.87) whose
# windscreen, sides and backlight separate cleanly by face orientation, so each
# region only needs the outline of its own painted glass.
#
#   y:          (low, high)
#   z:          (rear, front) for a side window
#   rake:       z <= a - b*y, the raked front edge of a front door window
#   half_width: |x| <= a - b*y, the tapering pillars of a screen
GLASS = {
    "windscreen": {"face": "front", "y": (1.47, 1.94), "half_width": (1.906, 0.533)},
    "backlight": {"face": "rear", "y": (1.46, 1.84), "half_width": (1.853, 0.533)},
    "front_door": {"face": "side", "y": (1.46, 1.865), "z": (-0.14, None),
                   "rake": (2.307, 1.031)},
    "rear_door": {"face": "side", "y": (1.46, 1.865), "z": (-1.01, -0.38)},
    "quarter": {"face": "side", "y": (1.46, 1.865), "z": (-1.24, -1.07)},
}

# Pane file -> (source shell, window regions, which side of the car).  The
# driver's front window is the only one cut out of the door, because it is the
# only one that has to swing with it; player_car_visual expects it at slot 3.
PANES = {
    "windshield": ("body", ["windscreen"], None),
    "rear_glass": ("body", ["backlight"], None),
    "passenger_glass": ("body", ["front_door"], "passenger"),
    "driver_glass": ("door", ["front_door"], "driver"),
    "driver_rear_glass": ("body", ["rear_door", "quarter"], "driver"),
    "passenger_rear_glass": ("body", ["rear_door", "quarter"], "passenger"),
}

# Driver layout, converted from the Municipal Cruiser 91-C rig.  That car's
# source units are metres exactly (its physical_half_track equals wheel_x and
# its physical_half_wheelbase equals half the wheel span), so the conversion
# below reproduces the same seated human in world space rather than guessing.
CRUISER_91C = {
    "hip": (0.42, 0.73, 0.18),
    "wrists": ((0.31, 1.04, 0.56), (0.53, 1.04, 0.56)),
    "ankles": ((0.34, 0.43, 0.72), (0.54, 0.43, 0.72)),
    "knees": ((0.34, 0.90, 0.38), (0.54, 0.90, 0.38)),
    "elbows": ((0.10, 0.78, 0.16), (0.76, 0.78, 0.16)),
    "handle_elbow": (1.42, 0.94, 0.02),
    "approach_x": 1.08,
    "ground_y": 0.10,      # arch_centre_y 0.42 less the 0.32 wheel radius
    "door_rear_z": -0.12,
    "door_front_z": 0.90,
}


def from_cruiser(point):
    """Map a 91-C metre anchor into Car 5 source space.

    X divides by the body's own X fit.  Y is measured from the ground, not the
    mesh origin, because the two bodies sit at different heights.  Z keeps the
    fraction along the driver door, so the seat stays in the same place
    relative to the opening the character has to walk through.
    """
    x, y, z = point
    span = CRUISER_91C["door_front_z"] - CRUISER_91C["door_rear_z"]
    fraction = (z - CRUISER_91C["door_rear_z"]) / span
    return (
        round(x / SCALE_X, 3),
        round(GROUND_Y + (y - CRUISER_91C["ground_y"]) / SCALE_Z, 3),
        round(DOOR["rear_z"] + fraction * (DOOR["front_z"] - DOOR["rear_z"]), 3),
    )


def seated(point):
    x, y, z = from_cruiser(point)
    return (x, round(y - SEAT_DROP_M / SCALE_Z, 3), z)


def driver_layout():
    pair = lambda key: tuple(seated(p) for p in CRUISER_91C[key])
    return {
        "hip": seated(CRUISER_91C["hip"]),
        "wrists": pair("wrists"),
        "ankles": tuple(from_cruiser(p) for p in CRUISER_91C["ankles"]),
        "knees": pair("knees"),
        "elbows": pair("elbows"),
        "handle_elbow": seated(CRUISER_91C["handle_elbow"]),
        "approach_x": round(CRUISER_91C["approach_x"] / SCALE_X, 3),
        "left_x": round(0.14 / SCALE_X, 3),
    }


if __name__ == "__main__":
    import json
    print(json.dumps({"scale": [SCALE_X, SCALE_Z], "ground_y": round(GROUND_Y, 4),
                      "door": DOOR, "driver": driver_layout()}, indent=2))
