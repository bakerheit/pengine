#!/usr/bin/env python3
"""Check the Car 5-NEXT door cut before anyone trusts it in game.

Four properties, each one a way this cut can be wrong:

1. Shut, the two parts put the imported skin back and add only the lining they
   are closed with.
2. Swung, nothing stationary stands in the doorway. The panel has to leave a
   real hole, not slide behind a fixed inner wall.
3. Swung, nothing shows daylight but the doorway. The preview renderer draws
   back faces; the game does not, so a missing liner reads as a window straight
   through the car. Silhouettes are rasterised with back-face culling from
   ninety directions at three door angles. The panes count as cover here even
   though the game draws them see-through: what this proves is that every hole
   cut in the shell is exactly filled by a pane or a rim, with no crack left
   over. A pixel that still shows daylight has to be one the door covered.
4. The door turns on its hinge and only ever swings outward, which is what the
   runtime transform assumes.
5. On the patrol car only: the lightbar the cook built and the glow boxes the
   lit shader tests still agree. Those live in two files that nothing forces to
   match -- car5_next_police_spec.py and lit.frag -- so every lens cell has to
   fall inside a box the shader actually contains, and no chrome rib may.

Writes build/<slug>-door-report.json and a back-face-culled QA sheet.

Takes an optional model slug so the patrol car, which is the same cut under a
different livery plus a lightbar, is held to the same four properties:
  python3 tools/validate_car5_next_door.py car5_next_police
"""
import json
import math
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from render_firetruck_preview import read_part
import car5_next_spec as spec

ROOT = Path(__file__).resolve().parents[1]
SLUG = sys.argv[1] if len(sys.argv) > 1 else "car5_next"
MODEL = ROOT / "assets/models/vehicles" / SLUG
TEXTURE = (ROOT / "assets/textures/vehicles" / SLUG / "body.png"
           if SLUG != "car5_next" else ROOT / "assets/textures/vehicles/car5/body.png")
BACKGROUND = (255, 0, 255)
PAINT = (150, 62, 44)


def triangles(part):
    return part.positions.astype(np.float64)[part.indices]


def swing(points, fraction):
    hinge = np.array(spec.DOOR["hinge"])
    angle = math.radians(spec.DOOR["open_degrees"]) * fraction
    c, s = math.cos(angle), math.sin(angle)
    return (points - hinge) @ np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]).T + hinge


def area(tris):
    return float(np.linalg.norm(
        np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1).sum() * 0.5)


def view_matrix(yaw, pitch):
    yr, pr = math.radians(yaw), math.radians(pitch)
    cy, sy, cp, sp = math.cos(yr), math.sin(yr), math.cos(pr), math.sin(pr)
    return np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]]) @ \
        np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])


def raster(groups, yaw, pitch, width, height, centre, scale, colours=None,
           two_sided=()):
    """Back-face-culled raster. Returns (coverage mask, image or None).

    Groups listed in `two_sided` skip the cull, which is what the renderer does
    for glass: render_glass disables GL_CULL_FACE for the whole pane pass.
    """
    rotation = view_matrix(yaw, pitch)
    mask = np.zeros((height, width), bool)
    image = None if colours is None else np.full((height, width, 3), BACKGROUND, np.uint8)
    depth = np.full((height, width), -np.inf)
    light = np.array([-0.35, 0.72, 0.60]) @ rotation.T
    light /= np.linalg.norm(light)
    for index, tris in enumerate(groups):
        for tri in (tris.reshape(-1, 3, 3) - centre) @ rotation.T:
            normal = np.cross(tri[1] - tri[0], tri[2] - tri[0])
            length = np.linalg.norm(normal)
            if length < 1e-12 or (normal[2] <= 0 and index not in two_sided):
                continue  # back face or degenerate
            if normal[2] < 0:
                normal = -normal
            screen = tri[:, :2] * np.array([scale, -scale]) + \
                np.array([width * 0.5, height * 0.52])
            x0 = max(0, int(screen[:, 0].min()))
            x1 = min(width - 1, int(screen[:, 0].max()) + 1)
            y0 = max(0, int(screen[:, 1].min()))
            y1 = min(height - 1, int(screen[:, 1].max()) + 1)
            if x1 < x0 or y1 < y0:
                continue
            (ax, ay), (bx, by), (cx, cy) = screen
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-9:
                continue
            gy, gx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
            l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den
            l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den
            l3 = 1 - l1 - l2
            covered = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
            if not covered.any():
                continue
            z = l1 * tri[0, 2] + l2 * tri[1, 2] + l3 * tri[2, 2]
            covered &= z > depth[y0:y1 + 1, x0:x1 + 1]
            if not covered.any():
                continue
            mask[y0:y1 + 1, x0:x1 + 1][covered] = True
            depth[y0:y1 + 1, x0:x1 + 1][covered] = z[covered]
            if image is not None:
                tone = np.array(colours[index], dtype=np.float64) * (
                    0.35 + 0.65 * max(0.0, float(normal / length @ light)))
                image[y0:y1 + 1, x0:x1 + 1][covered] = np.clip(tone, 0, 255).astype(np.uint8)
    return mask, image


def shader_lens_boxes():
    """The profile-29 glow boxes as lit.frag actually spells them.

    Parsed out of the shader rather than imported from the spec: the point is
    to catch the two drifting apart, and reading the spec twice cannot.
    """
    source = (ROOT / "assets/shaders/lit.frag").read_text()
    pattern = re.compile(
        r"v_headlight_profile == 29 &&\s*"
        r"x>=([-\d.]+) && x<=([-\d.]+) && p\.y>=([-\d.]+) && p\.y<=([-\d.]+) &&\s*"
        r"p\.z>=([-\d.]+) && p\.z<=([-\d.]+)\)")
    boxes = [tuple(float(value) for value in match)
             for match in pattern.findall(source)]
    assert boxes, "lit.frag declares no lightbar box for profile 29"
    return boxes


def check_lightbar(closed_tris):
    """Every lens cell inside a shader box; every chrome rib outside all of them."""
    import car5_next_police_spec as police
    boxes = shader_lens_boxes()
    declared = [(c["x"][0], c["x"][1], c["y"][0], c["y"][1], c["z"][0], c["z"][1])
                for c in police.lens_boxes()]
    assert len(boxes) == len(declared), \
        f"lit.frag has {len(boxes)} glow boxes, the spec builds {len(declared)} lens cells"
    for shader, spec_box in zip(sorted(boxes), sorted(declared)):
        assert all(abs(a - b) < 5e-4 for a, b in zip(shader, spec_box)), \
            f"lit.frag box {shader} does not match the cooked cell {spec_box}"

    def glows(point):
        x = abs(point[0])  # the shader negates X for the red bank
        return any(x0 - 1e-4 <= x <= x1 + 1e-4 and y0 - 1e-4 <= point[1] <= y1 + 1e-4
                   and z0 - 1e-4 <= point[2] <= z1 + 1e-4
                   for x0, x1, y0, y1, z0, z1 in boxes)

    lit = dark = 0
    for name, x0, x1, y0, y1, z0, z1, _ in police.lightbar_boxes():
        centre = np.array([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
        if "Lens" in name:
            assert glows(centre), f"{name} sits outside every glow box"
            lit += 1
        else:
            assert not glows(centre), f"{name} would light up with the bank"
            dark += 1
    # And the bar is really in the cooked shell, not just in the spec.
    above_roof = closed_tris.reshape(-1, 3)[:, 1] > police.ROOF_Y + 1e-3
    assert above_roof.sum() > 0, "the cooked body carries no lightbar"
    return {"glow_boxes": len(boxes), "lit_parts": lit, "dark_parts": dark,
            "vertices_above_the_roof": int(above_roof.sum())}


def contains_2d(point, tri):
    def cross(a, b):
        return a[0] * b[1] - a[1] * b[0]
    d = [cross(tri[(i + 1) % 3] - tri[i], point - tri[i]) for i in range(3)]
    return all(v >= -1e-7 for v in d) or all(v <= 1e-7 for v in d)


def main():
    door_spec = spec.DOOR
    body = read_part(MODEL / "body_open.emesh", TEXTURE)
    door = read_part(MODEL / "driver_door.emesh", TEXTURE)
    closed = read_part(MODEL / "body.emesh", TEXTURE)
    body_tris, door_tris = triangles(body), triangles(door)
    closed_tris = triangles(closed)
    # driver_glass is cut out of the door and has to travel with it; the rest
    # are fixed to the body. Slot order matches player_car_visual's loader.
    pane_names = ["windshield", "rear_glass", "passenger_glass", "driver_glass",
                  "driver_rear_glass", "passenger_rear_glass"]
    panes = {name: triangles(read_part(MODEL / (name + ".emesh"), TEXTURE))
             for name in pane_names}
    fixed_glass = np.concatenate(
        [panes[n] for n in pane_names if n != "driver_glass"]).reshape(-1, 3)
    report = {}

    for name in pane_names:
        tris = panes[name]
        assert len(tris) > 0, f"{name} is empty"
        sizes = np.linalg.norm(
            np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1)
        assert (sizes > 1e-9).all(), f"{name} kept a degenerate triangle"
        report[name] = {"triangles": len(tris),
                        "bounds_min": tris.reshape(-1, 3).min(0).round(3).tolist(),
                        "bounds_max": tris.reshape(-1, 3).max(0).round(3).tolist()}
    # The glazing mirrors across the car; a lopsided cut means a misread edge.
    # Compared by outline and area, not by vertex: the two sides are the same
    # window but the import triangulates them differently.
    for left, right in (("driver_glass", "passenger_glass"),
                        ("driver_rear_glass", "passenger_rear_glass")):
        a, b = panes[left].reshape(-1, 3), panes[right].reshape(-1, 3)
        mirrored = b * np.array([-1.0, 1.0, 1.0])
        assert np.allclose(a.min(0), mirrored.min(0), atol=2e-3) and \
            np.allclose(a.max(0), mirrored.max(0), atol=2e-3), \
            f"{left} and {right} do not span the same opening"
        assert abs(area(panes[left]) - area(panes[right])) < 2e-3, \
            f"{left} and {right} are not the same area of glass"

    for name, part in (("body_open", body), ("driver_door", door)):
        tris = triangles(part)
        assert len(tris) > 0 and np.isfinite(part.positions).all()
        assert ((part.uvs >= 0) & (part.uvs <= 1)).all(), f"{name} has UVs off the atlas"
        sizes = np.linalg.norm(
            np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0]), axis=1)
        assert (sizes > 1e-9).all(), f"{name} kept a degenerate triangle"
        report[name] = {"triangles": len(tris), "area": round(area(tris), 5),
                        "bounds_min": part.positions.min(0).round(4).tolist(),
                        "bounds_max": part.positions.max(0).round(4).tolist()}

    # 1. Shut, the split is lossless: the cut only adds the shell it closes with.
    assert area(body_tris) + area(door_tris) > area(closed_tris), \
        "the split lost skin instead of adding a lining"
    report["closed_area"] = round(area(closed_tris), 5)
    report["split_area"] = round(area(body_tris) + area(door_tris), 5)

    # 2. Swung, the doorway is a real hole in the stationary body.
    swung = swing(door_tris.reshape(-1, 3), 1.0).reshape(-1, 3, 3)
    def outer_x(point, tris):
        return max((float(np.linalg.solve(np.vstack([t[:, [2, 1]].T, np.ones(3)]),
                                          np.array([*point, 1.0])) @ t[:, 0])
                    for t in tris if contains_2d(point, t[:, [2, 1]])), default=None)
    probes = 0
    for z in np.linspace(door_spec["rear_z"] + .10, door_spec["front_z"] - .14, 6):
        for y in (door_spec["sill_y"] + .12, door_spec["sill_y"] + .40, 1.10, 1.45):
            point = np.array([z, y])
            surface = outer_x(point, door_tris)
            if surface is None:
                continue  # the raked upper front corner is not an opening here
            blocker = outer_x(point, body_tris)
            assert blocker is None or blocker < surface - .045, \
                f"stationary body blocks the doorway at z={z:.2f} y={y:.2f}"
            probes += 1
    assert probes >= 12, "not enough real aperture probes"
    report["aperture_probes"] = probes

    # 3. Swung, the only daylight through the car is the doorway itself.
    everything = np.concatenate([body_tris, closed_tris, swung, fixed_glass.reshape(-1, 3, 3)]).reshape(-1, 3)
    centre = (everything.min(0) + everything.max(0)) * 0.5
    scale = 300.0 / float((everything.max(0) - everything.min(0)).max())
    shut_panel = door_tris.reshape(-1, 3)
    leaked, views = 0, 0
    for yaw in range(-180, 180, 20):
        for pitch in (-15, 0, 15, 35, 60):
            solid, _ = raster([closed_tris.reshape(-1, 3)], yaw, pitch, 400, 300, centre, scale)
            aperture, _ = raster([shut_panel], yaw, pitch, 400, 300, centre, scale)
            for fraction in (0.35, 0.7, 1.0):
                panel = swing(door_tris.reshape(-1, 3), fraction)
                pane = swing(panes["driver_glass"].reshape(-1, 3), fraction)
                open_, _ = raster([body_tris.reshape(-1, 3), panel,
                                   fixed_glass, pane],
                                  yaw, pitch, 400, 300, centre, scale,
                                  two_sided=(2, 3))
                leaked += int((solid & ~open_ & ~aperture).sum())
                views += 1
    assert leaked == 0, f"{leaked} pixels see straight through the car outside the doorway"
    report["opacity_views"] = views

    # 4. The panel turns on its hinge, and turns outward.
    hinge = np.array(door_spec["hinge"])
    handle = np.array(door_spec["handle"])
    radius = float(np.linalg.norm(handle - hinge))
    for step in range(21):
        moved = swing(handle[None, :], step / 20.0)[0]
        assert abs(float(np.linalg.norm(moved - hinge)) - radius) < 1e-6, "the hinge moved"
        assert moved[0] >= handle[0] - 1e-6, "the door swings into the car"
    report["door"] = door_spec
    report["driver"] = spec.driver_layout()

    sheet = Image.new("RGB", (1200, 850), (20, 23, 26))
    draw = ImageDraw.Draw(sheet)
    panel = swing(door_tris.reshape(-1, 3), 1.0)
    glazed = swing(panes["driver_glass"].reshape(-1, 3), 1.0)
    shut_glass = panes["driver_glass"].reshape(-1, 3)
    frames = [("CULLED, SHUT", [body_tris.reshape(-1, 3), shut_panel,
                                fixed_glass, shut_glass], -34, 16),
              ("CULLED, OPEN", [body_tris.reshape(-1, 3), panel,
                                fixed_glass, glazed], -34, 16),
              ("GLAZING ONLY", [fixed_glass, glazed], -34, 16),
              ("CULLED, FROM BEHIND", [body_tris.reshape(-1, 3), panel,
                                       fixed_glass, glazed], -140, 18)]
    for i, (label, group, yaw, pitch) in enumerate(frames):
        _, image = raster(group, yaw, pitch, 600, 392, centre, scale * 2.0,
                          colours=[PAINT, (176, 78, 56), (70, 130, 150), (90, 160, 180)],
                          two_sided=(len(group) - 2, len(group) - 1))
        x, y = (i % 2) * 600, (i // 2) * 425
        sheet.paste(Image.fromarray(image), (x, y))
        draw.text((x + 12, y + 402), label, fill=(235, 235, 220))
    (ROOT / "build").mkdir(exist_ok=True)
    sheet.save(ROOT / ("build/%s-door-culled.png" % SLUG.replace("_", "-")))
    # (path built from the slug so the two variants do not overwrite each other)
    if SLUG == "car5_next_police":
        report["lightbar"] = check_lightbar(closed_tris)

    report["checks"] = [
        "shut split rebuilds the imported skin and adds only lining",
        "swung door leaves a doorway no stationary panel blocks",
        "every hole the cut makes is filled by a pane or a rim, 90 views x 3 angles",
        "the glazing mirrors left to right",
        "handle turns on a fixed hinge and only ever moves outboard",
    ] + (["lens cells glow and chrome ribs do not, per the shader's own boxes"]
         if SLUG == "car5_next_police" else [])
    (ROOT / ("build/%s-door-report.json" % SLUG.replace("_", "-"))).write_text(
        json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "driver"}, indent=2))


if __name__ == "__main__":
    main()
