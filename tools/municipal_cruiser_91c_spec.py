"""Shape, articulation and model-space UV contract for cruiser attempt 91C."""

import math

ATLAS_SIZE = 256

# Cooked coordinates: X right, Y up, Z forward.  The body remains wheel-less;
# Apricot attaches the existing shared wheel mesh at these four anchors.
#
# "x" is half-track.  It was 0.94 against a 1.05 body half-width, which put the
# tyre's outer sidewall 1 mm PROUD of the flank: the car had no fender at all
# and the wheels read as bolted on.  Legacy Car 5 keeps its tyre 0.169 m inside
# the flank.  0.845 buys 0.099 m of real fender while keeping the track at a
# believable 1.69 m for a full-size pursuit sedan.  `physical_half_track` in
# src/app/player_car_catalog.h must stay equal to it (municipal_cruiser_91_tests).
WHEELS = {
    "x": 0.845,
    "front_z": 1.63,
    "rear_z": -1.53,
    "arch_y": 0.42,
    "radius": 0.355,
}

# The shared wheel cook is 0.918 m across and 0.274 m wide, and the runtime
# scales it uniformly to `radius` (src/app/player_car_visual.cpp).  Anything
# that has to clear the tyre derives from this, never from a typed-in number.
TYRE_HALF_WIDTH = 0.137 * (WHEELS["radius"] / 0.459)

# Cruisers are capped at 0.64 rad of lock in src/app/vehicle_model_tuning.h and
# the strut travels 0.16 m either way in src/physics/vehicle.h.
STEER_LOCK = 0.64
SUSPENSION_TRAVEL = 0.16

# Swept tyre envelope at full lock, in the body frame.
SWEPT_ALONG = (WHEELS["radius"] * math.cos(STEER_LOCK) +
               TYRE_HALF_WIDTH * math.sin(STEER_LOCK))
SWEPT_ACROSS = (WHEELS["radius"] * math.sin(STEER_LOCK) +
                TYRE_HALF_WIDTH * math.cos(STEER_LOCK))

# The arch only has to clear the travel the wheel has LEFT from its static
# pose, not the whole strut.  The 91-C profile is 1885 kg on a spring rate of
# 51000 * (1885/1250) * 1.07 N/m per corner, so static sag is
# m*g/(4k) = 0.056 m and the wheel can rise SUSPENSION_TRAVEL - sag = 0.104 m
# before the bumpstop takes over.  Clearing the full 0.16 m opened a 0.16 m
# void over the tread that read as a hole punched through the flank.
BUMP_ALLOWANCE = 0.105

# Wheel-opening half-axes.
#
# Longitudinal: turning the wheel SHRINKS its longitudinal extent
# (SWEPT_ALONG = 0.348 < radius = 0.355), so the binding case is straight
# ahead, not full lock, and the .59 the predecessor cut was pure daylight --
# 0.217 m of visible hole in front of and behind the tread on both sides.
# 0.06 m of margin puts the mouth where Legacy Car 5 puts it.
ARCH_MARGIN = 0.06
WELL_ALONG = round(max(WHEELS["radius"], SWEPT_ALONG) + ARCH_MARGIN, 3)
# Vertical is NOT styling: tyre radius plus the bump travel above, and
# shrinking it further makes the tread clip the arch on ordinary movement.
WELL_UP = round(WHEELS["radius"] + BUMP_ALLOWANCE, 3)

# Buried structure must clear the inboard swing at full lock.  The old
# CentralChassis box reached x = +/-0.58 and is what forced the 0.94 half-track;
# it is gone, and the lofted shell floor does its job.
WELL_INNER_X = 0.52

# +X is the driver's side.  These anchors are intentionally in cooked space
# so runtime articulation can consume them without a Blender-axis conversion.
DOOR = {
    "hinge": (1.006, 0.55, 0.88),
    "handle": (1.025, 1.00, 0.04),
    "front_z": 0.90,
    "rear_z": -0.12,
    "sill_y": 0.54,
    "belt_y": 0.98,
    "top_y": 1.49,
    "open_degrees": -68.0,
}

DRIVER = {
    "hip": (0.42, 0.73, 0.18),
    "wrists": ((0.31, 1.04, 0.56), (0.53, 1.04, 0.56)),
    "ankles": ((0.34, 0.43, 0.72), (0.54, 0.43, 0.72)),
    "knees": ((0.34, 0.90, 0.38), (0.54, 0.90, 0.38)),
    "elbows": ((0.10, 0.78, 0.16), (0.76, 0.78, 0.16)),
    "handle_elbow": (1.42, 0.94, 0.02),
    "approach_x": 1.08,
    "floor_y": 0.40,
    "cushion_top_y": 0.72,
    "back_z": -0.20,
    "dashboard_z": 0.69,
    "roof_inner_y": 1.47,
}

SHAPE = {
    "name": "Municipal Cruiser 91C",
    "year": 1991,
    "length": 5.42,
    "width": 2.10,
    "body_roof": 1.55,
    "lightbar_height": 1.77,
    "wheelbase": 3.16,
    "track": round(2 * WHEELS["x"], 3),
    "body_bottom": 0.185,
    "beltline": 0.935,
    "hood_crest": 1.035,
    "hood_nose": 0.905,
    "deck_height": 1.005,
    "cabin": (-1.23, 0.90),
    # Raised from (900, 1500) because the body genuinely gained surface: a
    # fourteen-sided section in place of ten, and two recessed fascia bays with
    # return walls in place of decals on a flat plane.  Still far under the
    # 1800 the C++ suite allows, and the mesh is smaller than it looks because
    # CentralChassis and both Rocker boxes are gone.
    "triangle_budget": (900, 1560),
    "atlas": (256, 256, "RGBA"),
    "traits": [
        "low wide 1991 pursuit-sedan stance",
        "broad nearly level stamped hood and four inset rectangular headlamps",
        "formal four-door roof with a restrained fastback rear screen",
        "squared rear quarters and a short raised deck",
        "heavy black push bumper, A-pillar spotlight and whip antenna",
        "period red and blue roof lightbar",
        "semi-realistic Legacy Car 5 proportions, restrained pixel clusters and narrow chrome trim",
    ],
}

# Exact cooked-space lamp receivers used by the renderer and fit report.
LAMPS = {
    # The lamps now sit on the recessed fascia at |z| = 2.592, not on the nose
    # skin at 2.669.  assets/shaders/vehicle_headlight_profiles.inc and
    # vehicle_brakelight_profiles.inc carry the same numbers and are compiled
    # into both the C++ header and the shader; they move together or traffic
    # refuses to load the police car.
    "headlights": {
        "x": ((-0.830, -0.648), (-0.638, -0.460),
              (0.460, 0.638), (0.648, 0.830)),
        "y": (0.600, 0.790),
        "z": (2.589, 2.595),
    },
    "brakelights": {
        "x": ((-0.83, -0.52), (0.52, 0.83)),
        "y": (0.560, 0.735),
        "z": (-2.595, -2.589),
    },
    "lightbar_red": {
        "x": (-0.70, -0.05), "y": (1.61, 1.77), "z": (-0.35, -0.14),
    },
    "lightbar_blue": {
        "x": (0.05, 0.70), "y": (1.61, 1.77), "z": (-0.35, -0.14),
    },
}

# Inclusive pixel rectangles.  Each final UV is inset two pixels so imagegen
# can change value structure without leaking across semantic receivers.
REGIONS = {
    "SIDE_DRIVER": (4, 4, 252, 46),
    "SIDE_PASSENGER": (4, 50, 252, 92),
    "BODY_TOP": (4, 96, 92, 196),
    "BODY_FRONT": (96, 96, 172, 140),
    "BODY_REAR": (176, 96, 252, 140),
    "GLASS_SIDE": (96, 144, 214, 174),
    # Painted cladding: the receiver for faces that are edge-on to their
    # livery plane -- arch-lip rims, boolean cut walls, box ends.  Taken from
    # GLASS_SIDE, which was 157 px wide for a 2.04 x 0.51 m projection (5.1:1
    # against a 4.0:1 surface) and is better proportioned at 119.
    "CLADDING": (218, 144, 252, 174),
    "GLASS_FRONT": (96, 178, 170, 204),
    "GLASS_REAR": (174, 178, 252, 204),
    "GRILLE": (40, 228, 72, 252),
    "BLACK": (4, 200, 36, 252),
    "METAL": (40, 200, 72, 224),
    "INTERIOR": (76, 208, 108, 252),
    "SEAT": (112, 208, 144, 252),
    "HEADLIGHT": (148, 208, 180, 228),
    "TAIL_RED": (184, 208, 216, 228),
    "TAIL_AMBER": (220, 208, 252, 228),
    "LIGHTBAR_RED": (148, 232, 180, 252),
    "LIGHTBAR_BLUE": (184, 232, 216, 252),
    "LENS_CLEAR": (220, 232, 252, 252),
}

# A face whose normal is this far from its receiver's projection plane normal
# is projected honestly on its own best axes instead.  0.42 caps the planar
# stretch any face can suffer at 1 / 0.42 = 2.4 : 1.
GRAZING_FACING = 0.42

# Where a grazing face goes instead.  The livery receivers carry POLICE
# lettering and the cream band, so a face that lands on them by accident shows
# a slice of a word; the neutral cladding swatch cannot.  Glass rims are the
# dark edge of the pane and belong on black.
GRAZING_FALLBACK = {
    "SIDE_DRIVER": "CLADDING",
    "SIDE_PASSENGER": "CLADDING",
    "BODY_TOP": "CLADDING",
    "BODY_FRONT": "CLADDING",
    "BODY_REAR": "CLADDING",
    "GLASS_SIDE": "BLACK",
    "GLASS_FRONT": "BLACK",
    "GLASS_REAR": "BLACK",
}

# Blender source coordinates: X right, Y forward, Z up.
UV_PROJECTIONS = {
    "SIDE_DRIVER": ((1, 2), ((-2.71, 2.71), (0.18, 1.55))),
    "SIDE_PASSENGER": ((1, 2), ((-2.71, 2.71), (0.18, 1.55))),
    "BODY_TOP": ((0, 1), ((-1.05, 1.05), (-2.71, 2.71))),
    "BODY_FRONT": ((0, 2), ((-1.05, 1.05), (0.18, 1.10))),
    "BODY_REAR": ((0, 2), ((-1.05, 1.05), (0.18, 1.10))),
    "GLASS_SIDE": ((1, 2), ((-1.18, 0.86), (0.99, 1.50))),
    "GLASS_FRONT": ((0, 2), ((-0.92, 0.92), (0.99, 1.50))),
    "GLASS_REAR": ((0, 2), ((-0.92, 0.92), (0.99, 1.50))),
}

# Reversing the driver's side receiver keeps generic POLICE lettering readable
# from both exterior side cameras without sharing a mirrored word.
UV_FLIP_U = {"SIDE_DRIVER"}
