"""Measured shape and texture contract for the second FANG VENOM attempt."""

ATLAS_SIZE = 256

# Labels live in the top 11 pixels of each cell.  Geometry maps only to the
# inset content area, so the labeled imagegen guide can never print words on
# the bike by accident.
REGIONS = {
    "PAINT_SIDE": (0, 0, 112, 96),
    "PAINT_TOP": (112, 0, 192, 96),
    "PAINT_LOWER": (192, 0, 256, 96),
    "FRAME": (0, 96, 64, 144),
    "ENGINE": (64, 96, 128, 144),
    "METAL": (128, 96, 192, 144),
    "BLACK": (192, 96, 256, 144),
    "SEAT": (0, 144, 64, 192),
    "TIRE": (64, 144, 128, 192),
    "RIM": (128, 144, 192, 192),
    "BRAKE": (192, 144, 256, 192),
    "HEADLIGHT": (0, 192, 48, 224),
    "TAIL_RED": (48, 192, 96, 224),
    "AMBER": (96, 192, 144, 224),
    "GAUGE": (144, 192, 192, 224),
    "EXHAUST": (192, 192, 256, 224),
    "DECAL": (0, 224, 96, 256),
    "CHAIN": (96, 224, 160, 256),
    "RUBBER": (160, 224, 208, 256),
    "PLATE": (208, 224, 256, 256),
}

BASE = {
    "PAINT_SIDE": (18, 111, 92, 255),
    "PAINT_TOP": (27, 144, 111, 255),
    "PAINT_LOWER": (12, 68, 64, 255),
    "FRAME": (38, 40, 48, 255),
    "ENGINE": (62, 67, 72, 255),
    "METAL": (144, 149, 151, 255),
    "BLACK": (10, 12, 16, 255),
    "SEAT": (28, 23, 34, 255),
    "TIRE": (13, 15, 18, 255),
    "RIM": (126, 132, 137, 255),
    "BRAKE": (104, 111, 114, 255),
    "HEADLIGHT": (225, 216, 181, 255),
    "TAIL_RED": (174, 25, 38, 255),
    "AMBER": (212, 92, 16, 255),
    "GAUGE": (31, 75, 84, 255),
    "EXHAUST": (78, 82, 87, 255),
    "DECAL": (90, 31, 120, 255),
    "CHAIN": (77, 68, 55, 255),
    "RUBBER": (20, 20, 24, 255),
    "PLATE": (203, 199, 174, 255),
}

WHEELS = {
    "front_z": 0.76,
    "rear_z": -0.72,
    "centre_y": 0.35,
    "radius": 0.345,
    "front_half_width": 0.052,
    "rear_half_width": 0.074,
}

SHAPE = {
    "attempt": 2,
    "period": 1991,
    "archetype": "full-fairing 750-class street superbike",
    "length_m": 2.24,
    "fairing_width_m": 0.56,
    "handlebar_width_m": 0.76,
    "mirror_width_m": 0.82,
    "height_m": 1.16,
    "seat_height_m": 0.86,
    "wheelbase_m": 1.48,
    "front_axle_z_m": 0.76,
    "rear_axle_z_m": -0.72,
    "wheel_radius_m": 0.345,
    # Attempt two deliberately spends more geometry on actual mechanical parts
    # than the generic car budget: radiator fins, chain drive, controls and
    # suspension are silhouette/depth details, not texture noise.
    "body_triangle_budget": (1400, 1900),
    "wheel_triangle_budget": (550, 850),
    "signature": [
        "low wedge nose with a split rectangular endurance headlamp",
        "sculpted fuel tank flowing into a narrow stepped tail",
        "real negative space around both wheels and beneath the seat",
        "visible twin-spar frame, transverse four-cylinder engine and radiator",
        "separate front brake and rear chain-drive wheel assemblies",
        "original sea-green, violet and bone 1991 racing graphics",
    ],
    "stations_m": {
        "rear_tip": -1.12,
        "seat_start": -0.68,
        "tank_peak": -0.06,
        "steering_head": 0.43,
        "nose_tip": 1.12,
    },
}

DRIVER = {
    "hip": (0.0, 0.90, -0.20),
    "wrists": ((-0.25, 1.04, 0.34), (0.25, 1.04, 0.34)),
    "ankles": ((-0.225, 0.52, -0.08), (0.225, 0.52, -0.08)),
    "knees": ((-0.245, 0.80, 0.17), (0.245, 0.80, 0.17)),
    "elbows": ((-0.36, 1.10, 0.12), (0.36, 1.10, 0.12)),
    "approach_x": 0.58,
}

LAMPS = {
    "headlight": (0.035, 0.815, 0.33, 0.975, 1.104, 1.125),
    "rear_red": (0.035, 0.805, 0.20, 0.925, -1.125, -1.095),
}
