"""1991 Saddle Tango design contract. Blender: X side, Y forward, Z up."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "assets/models/vehicles/saddle_tango"
TEXTURE = ROOT / "assets/textures/vehicles/saddle_tango/body.png"
REFERENCE = ROOT / "docs/design/references/saddle_tango"

# The selected concept's side view controls length, axle spacing and doors.
SHAPE = {
    "year": 1991,
    "brand": "Saddle",
    "model": "Tango",
    "shell_length": 4.87,
    "overall_length": 5.04,
    "body_width": 1.91,
    "overall_width_with_mirrors": 2.00,
    "height": 1.47,
    "front_end": 2.20,
    "rear_end": -2.67,
    "wheelbase": 2.75,
    "belt_height": 1.00,
    "hood_height": (0.94, 0.80),
    "deck_height": 0.91,
    "roof_height": 1.47,
    "cabin_lower": (-1.65, 0.57),
    "roof_span": (-1.26, 0.07),
    "traits": [
        "long low hood and four-door notchback",
        "paired rectangular lamps with amber corners",
        "black horizontal grille and narrow silver side molding",
        "deep burgundy paint, dark lower cladding and modest trunk lip",
        "six transparent panes and textured cabin visible from every side",
    ],
}
WHEELS = {"x": 0.84, "front_z": 1.37, "rear_z": -1.38,
          "arch_y": 0.35, "radius": 0.35}

# Semantic atlas cells. The body cells are large enough for graded paint.
REGIONS = {
    "BODY_SIDE": (2, 2, 254, 82),
    "BODY_TOP": (2, 86, 126, 158),
    "BODY_END": (130, 86, 254, 158),
    "GLASS": (2, 162, 82, 218),  # export UVs only; runtime glass has its own pass
    "DOOR_CARD": (2, 162, 82, 218),
    "DARK": (86, 162, 130, 186),
    "DASHBOARD": (86, 190, 130, 218),
    "METAL": (134, 162, 178, 218),
    "HEADLIGHT": (182, 162, 218, 190),
    "AMBER": (222, 162, 254, 190),
    "TAIL": (182, 194, 218, 222),
    "REVERSE": (222, 194, 254, 222),
    "SEAT_FABRIC": (2, 222, 42, 254),
    "HEADLINER": (44, 222, 82, 238),
    "CARPET": (44, 240, 82, 254),
    "RUBBER": (86, 222, 130, 254),
    "SILVER_DARK": (134, 222, 178, 254),
    "SEAM": (182, 226, 218, 254),
}
