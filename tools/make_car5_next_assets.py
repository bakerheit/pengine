#!/usr/bin/env python3
"""Cook Car 5-NEXT: Car 5's shell with an independently hinged driver door.

Every other articulated car in this tree is generated in Blender, so its door
is simply a separate object that was never joined.  Car 5 is an import from the
Probable Cause alpha and there is no generator to change, so the door is cut
out of the cooked body instead:

  body.emesh       byte-identical to Car 5 -- the closed shell the catalog fits,
                   samples headlight origins from, and tags for snow
  body_open.emesh  that shell with the driver door removed, plus a cabin the
                   opening can actually show: an inset inner skin, a floor,
                   seats and a dashboard
  driver_door.emesh the removed panel, closed with an inner skin and a rim
  six pane files  the windows, cut off the shell so the renderer's glass
                  material can make them see-through; driver_glass is the one
                  that rides with the door

The paint is Car 5's own -- nothing is copied, nothing is repacked.

Read-only inputs.  Run from anywhere:  python3 tools/make_car5_next_assets.py
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np

import car5_next_spec as spec

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/models/vehicles/car5"
TARGET = ROOT / "assets/models/vehicles/car5_next"
# Car 5-NEXT wears Car 5's paint; the cut does not touch a UV, so there is
# nothing to repack and nothing to copy.
TEXTURE = ROOT / "assets/textures/vehicles/car5"

MAGIC = 0x48534D45
EPSILON = 1e-6


# --------------------------------------------------------------------------
# emesh io


def read_emesh(path):
    data = path.read_bytes()
    magic, version, flags, nv, ni, submeshes, strings, _ = struct.unpack_from("<8I", data)
    if (magic, version, flags, submeshes) != (MAGIC, 2, 0, 1):
        raise ValueError(f"unsupported static emesh: {path}")
    vertices = np.frombuffer(data, "<f4", count=nv * 12, offset=32).reshape(-1, 12).astype(np.float64)
    indices = np.frombuffer(data, "<u4", count=ni, offset=32 + nv * 48).reshape(-1, 3)
    material = data[32 + nv * 48 + ni * 4 + 16:32 + nv * 48 + ni * 4 + 16 + strings]
    return vertices, np.asarray(indices, dtype=np.int64), material


def write_emesh(path, triangles, material):
    """Write an unindexed triangle soup. `triangles` is (n, 3, 12)."""
    vertices = np.asarray(triangles, dtype="<f4").reshape(-1, 12)
    count = len(vertices)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as out:
        out.write(struct.pack("<8I", MAGIC, 2, 0, count, count, 1, len(material), 0))
        out.write(vertices.tobytes())
        out.write(np.arange(count, dtype="<u4").tobytes())
        out.write(struct.pack("<4I", 0, count, 0, 0))
        out.write(material)
    return count


# --------------------------------------------------------------------------
# geometry helpers


def smooth_normals(vertices, indices):
    """Area-weighted normal per welded POSITION, not per vertex index.

    The import splits vertices at every UV seam, so per-index normals leave
    cracks in an inset shell.  Offsetting along a position-keyed normal keeps
    the inner skin closed across those seams.
    """
    accumulated = {}
    for tri in indices:
        points = vertices[tri, :3]
        cross = np.cross(points[1] - points[0], points[2] - points[0])
        for index in tri:
            key = tuple(np.round(vertices[index, :3], 4))
            accumulated[key] = accumulated.get(key, np.zeros(3)) + cross
    out = {}
    for key, total in accumulated.items():
        length = np.linalg.norm(total)
        out[key] = total / length if length > 1e-12 else np.array([1.0, 0.0, 0.0])
    return out


def in_mirror(tri):
    """True when a whole triangle belongs to either authored wing mirror.

    Tested on |x|: the car has one on each side and the spec measures the
    driver's.
    """
    box = spec.MIRROR
    absolute = np.abs(tri[:, 0])
    return bool(np.all((absolute >= box["x"][0]) & (absolute <= box["x"][1]) &
                       (tri[:, 1] >= box["y"][0]) & (tri[:, 1] <= box["y"][1]) &
                       (tri[:, 2] >= box["z"][0]) & (tri[:, 2] <= box["z"][1])))


def offset_point(vertex, normals, thickness):
    """Push one 12-float vertex along the inward welded normal."""
    key = tuple(np.round(vertex[:3], 4))
    normal = normals.get(key)
    if normal is None:
        normal = vertex[3:6] / max(np.linalg.norm(vertex[3:6]), 1e-12)
    moved = vertex.copy()
    moved[:3] = vertex[:3] - normal * thickness
    moved[3:6] = -vertex[3:6]
    moved[8:11] = -vertex[8:11]
    return moved


# A half-space is (unit normal, offset): normal . point >= offset is inside.
# Most are axis aligned; the door's upper front edge is not, because Car 5's
# A-pillar rakes and a vertical cut there saws through the windscreen header.
def half_space(normal, offset):
    normal = np.asarray(normal, dtype=np.float64)
    length = np.linalg.norm(normal)
    return normal / length, offset / length


def door_planes(door):
    rake = door["pillar_rake"]
    return [
        half_space((1, 0, 0), door["inner_x"]),
        half_space((0, 0, -1), -door["front_z"]),
        half_space((0, 0, 1), door["rear_z"]),
        half_space((0, 1, 0), door["sill_y"]),
        half_space((0, -1, 0), -door["top_y"]),
        # Behind the A-pillar: z + rake*y <= intercept.
        half_space((0, -rake, -1), -door["pillar_intercept"]),
    ]


def box_planes(x_max, y_min, y_max, z_rear, z_front):
    return [half_space((1, 0, 0), -x_max), half_space((-1, 0, 0), -x_max),
            half_space((0, 1, 0), y_min), half_space((0, -1, 0), -y_max),
            half_space((0, 0, 1), z_rear), half_space((0, 0, -1), -z_front)]


def clip_polygon(polygon, plane):
    """Split a convex polygon by one half-space. Returns (inside, outside)."""
    normal, offset = plane
    distances = [float(normal @ point[:3]) - offset for point in polygon]
    if all(d >= -EPSILON for d in distances):
        return polygon, []
    if all(d <= EPSILON for d in distances):
        return [], polygon
    inside, outside = [], []
    count = len(polygon)
    for i in range(count):
        current, following = polygon[i], polygon[(i + 1) % count]
        here, there = distances[i], distances[(i + 1) % count]
        if here >= -EPSILON:
            inside.append(current)
        if here <= EPSILON:
            outside.append(current)
        if (here > EPSILON and there < -EPSILON) or (here < -EPSILON and there > EPSILON):
            t = here / (here - there)
            crossing = current + (following - current) * t
            crossing[:3] -= normal * (float(normal @ crossing[:3]) - offset)
            inside.append(crossing)
            outside.append(crossing)
    return inside, outside


def split_by_region(vertices, indices, planes):
    """Split a triangle soup into the part inside every half-space and the rest."""
    inside, outside = [], []
    for tri in indices:
        pending = [[vertices[i].copy() for i in tri]]
        for plane in planes:
            following = []
            for polygon in pending:
                keep, drop = clip_polygon(polygon, plane)
                if len(keep) >= 3:
                    following.append(keep)
                if len(drop) >= 3:
                    outside.append(drop)
            pending = following
        inside.extend(pending)
    return inside, outside


def split_by_region_polygons(triangles, planes):
    """split_by_region for an already-triangulated soup."""
    inside, outside = [], []
    for tri in triangles:
        pending = [[point.copy() for point in tri]]
        for plane in planes:
            following = []
            for polygon in pending:
                keep, drop = clip_polygon(polygon, plane)
                if len(keep) >= 3:
                    following.append(keep)
                if len(drop) >= 3:
                    outside.append(drop)
            pending = following
        inside.extend(pending)
    return inside, outside


def fan(polygons):
    """Triangulate convex polygons, dropping degenerate slivers."""
    triangles = []
    for polygon in polygons:
        for i in range(1, len(polygon) - 1):
            tri = np.array([polygon[0], polygon[i], polygon[i + 1]])
            cross = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
            if np.linalg.norm(cross) > 1e-9:
                triangles.append(tri)
    return triangles


def boundary_edges(triangles, planes=None):
    """Every directed edge used once, with the face normal of its triangle.

    With no `planes` the whole outline comes back.  Car 5's greenhouse ends on
    a raked A-pillar edge that has nothing to do with the cut, and leaving that
    edge unskinned opens a hairline you can see straight through the car along,
    so the door and the aperture both take the unfiltered outline.  `planes`
    narrows it to edges a given clip produced, which is how the cabin liner
    skins back out to the paint without redoing the aperture rim.
    """
    used = {}
    for tri in triangles:
        keys = [tuple(np.round(point[:3], 4)) for point in tri]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            used[(keys[a], keys[b])] = used.get((keys[a], keys[b]), 0) + 1
    edges = []
    for tri in triangles:
        keys = [tuple(np.round(point[:3], 4)) for point in tri]
        face = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
        length = np.linalg.norm(face)
        if length < 1e-12:
            continue
        face = face / length
        for a, b in ((0, 1), (1, 2), (2, 0)):
            if used.get((keys[b], keys[a]), 0) or used[(keys[a], keys[b])] != 1:
                continue
            if planes is not None and not all(
                    any(abs(float(normal @ point[:3]) - offset) < 1e-4
                        for normal, offset in planes)
                    for point in (tri[a], tri[b])):
                continue
            edges.append((tri[a], tri[b], face))
    return edges


def rim(edges, normals, thickness):
    """Skin an open outline with a band back to its inset copy.

    The band faces the way the surface itself opens: outward in the plane of
    the triangle that owns the edge, which is (edge) x (face normal).  That is
    the right answer for both sides of the cut without a special case -- on the
    door panel it points away from the panel, and on the body it points into
    the doorway, which is where a real jamb faces.
    """
    triangles = []
    for a, b, face in edges:
        inner_a = offset_point(a, normals, thickness)
        inner_b = offset_point(b, normals, thickness)
        outward = np.cross(b[:3] - a[:3], face)
        length = np.linalg.norm(outward)
        if length < 1e-9:
            continue
        outward /= length
        quad = [inner_a, inner_b, b.copy(), a.copy()]
        for point in quad:
            point[3:6] = outward
        triangles.append(np.array([quad[0], quad[1], quad[2]]))
        triangles.append(np.array([quad[0], quad[2], quad[3]]))
    return triangles


FACES = {
    "driver": lambda n: n[0] > 0.5,
    "passenger": lambda n: n[0] < -0.5,
    "front": lambda n: n[2] > 0.3,
    "rear": lambda n: n[2] < -0.3,
}


def window_planes(region, side):
    """Half-spaces bounding one painted window."""
    planes = []
    low, high = region["y"]
    planes.append(half_space((0, 1, 0), low))
    planes.append(half_space((0, -1, 0), -high))
    if "z" in region:
        rear, front = region["z"]
        planes.append(half_space((0, 0, 1), rear))
        if front is not None:
            planes.append(half_space((0, 0, -1), -front))
    if "rake" in region:
        offset, slope = region["rake"]
        planes.append(half_space((0, -slope, -1), -offset))
    if "half_width" in region:
        offset, slope = region["half_width"]
        planes.append(half_space((-1, -slope, 0), -offset))
        planes.append(half_space((1, -slope, 0), -offset))
    if side is not None:
        planes.append(half_space((1, 0, 0), 0) if side == "driver"
                      else half_space((-1, 0, 0), 0))
    return planes


def face_of(tri):
    normal = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
    length = np.linalg.norm(normal)
    return normal / length if length > 1e-12 else None


def cut_glass(triangles, region, side):
    """Lift one window out of a shell. Returns (shell without it, the pane)."""
    wanted = FACES["driver" if side == "driver" and region["face"] == "side" else
                   "passenger" if side == "passenger" and region["face"] == "side" else
                   region["face"]]
    planes = window_planes(region, None if region["face"] != "side" else side)
    shell, pane = [], []
    for tri in triangles:
        normal = face_of(tri)
        # The mirror faces outboard like the door skin does and sits inside the
        # front window's outline. Glazing it would hand the renderer a
        # see-through mirror and punch a mirror-shaped hole in the door.
        if normal is None or not wanted(normal) or in_mirror(tri):
            shell.append(tri)
            continue
        inside, outside = split_by_region_polygons([tri], planes)
        pane.extend(fan(inside))
        shell.extend(fan(outside))
    return shell, pane


def block(x0, x1, y0, y1, z0, z1, uv):
    """Closed axis-aligned box with outward normals and one flat UV."""
    corners = {
        (0, -1): ([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0)),
        (0, +1): ([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0)),
        (1, -1): ([(x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)], (0, -1, 0)),
        (1, +1): ([(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)], (0, 1, 0)),
        (2, -1): ([(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)], (0, 0, -1)),
        (2, +1): ([(x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (x0, y0, z1)], (0, 0, 1)),
    }
    triangles = []
    for quad, normal in corners.values():
        points = [np.array([*point, *normal, *uv, 1.0, 0.0, 0.0, 1.0]) for point in quad]
        triangles.append(np.array([points[0], points[1], points[2]]))
        triangles.append(np.array([points[0], points[2], points[3]]))
    return triangles


def darkest_uv(texture_path, patch=6):
    """UV of the darkest uniform patch in the paint atlas, for interior parts."""
    from PIL import Image
    image = np.asarray(Image.open(texture_path).convert("RGB"), dtype=np.float64)
    height, width, _ = image.shape
    best, best_uv = None, (0.5, 0.5)
    for y in range(0, height - patch, patch):
        for x in range(0, width - patch, patch):
            tile = image[y:y + patch, x:x + patch]
            score = tile.mean() + tile.std() * 2.0
            if best is None or score < best:
                best = score
                best_uv = ((x + patch / 2) / width, 1.0 - (y + patch / 2) / height)
    return best_uv


# --------------------------------------------------------------------------
# build


def build(report_only=False):
    vertices, indices, material = read_emesh(SOURCE / "body.emesh")
    normals = smooth_normals(vertices, indices)
    planes = door_planes(spec.DOOR)

    door_polygons, body_polygons = split_by_region(vertices, indices, planes)
    door_outer = fan(door_polygons)
    body_outer = fan(body_polygons)
    if not door_outer:
        raise ValueError("the door cut selected no geometry; check DOOR in car5_next_spec")

    thickness = spec.THICKNESS
    lining = spec.LINER_THICKNESS

    # Lift the painted windows out of both shells FIRST. Everything downstream
    # then follows for free: the lining is built from the already-holed shell,
    # so it does not stand behind the glass, and the rim pass skins each window
    # opening exactly as it skins the doorway.
    shells = {"body": body_outer, "door": door_outer}
    panes = {}
    for name, (source, regions, side) in spec.PANES.items():
        glass = []
        for region in regions:
            shells[source], cut = cut_glass(shells[source], spec.GLASS[region], side)
            glass.extend(cut)
        if not glass:
            raise ValueError(f"the {name} cut selected no geometry; check GLASS")
        panes[name] = glass
    body_outer, door_outer = shells["body"], shells["door"]

    # The door: its own skin, an inner skin, and a rim that joins the two.
    door_inner = [np.array([offset_point(p, normals, thickness) for p in tri])[::-1]
                  for tri in door_outer if not in_mirror(tri)]
    door = door_outer + door_inner + rim(
        boundary_edges(door_outer), normals, thickness)

    # The body: the same rim the other way round, an inset cabin skin so the
    # opening never shows the far side's back faces, then floor and furniture.
    # The skin is CLIPPED to the cabin band rather than filtered by it: Car 5's
    # side is one triangle from rocker to belt, so a whole-triangle test drops
    # the very panel an open door looks at.
    cabin = spec.CABIN
    band = box_planes(cabin["x_max"], cabin["y_min"], cabin["y_max"],
                      cabin["z_rear"], cabin["z_front"])
    inside, _ = split_by_region_polygons(body_outer, band)
    banded = fan(inside)
    liner = []
    for tri in banded:
        face = np.cross(tri[1, :3] - tri[0, :3], tri[2, :3] - tri[0, :3])
        length = np.linalg.norm(face)
        # The underbody and the arch ceilings point down; lining those would
        # raise a second floor above the one the driver's feet stand on.
        if length < 1e-9 or face[1] / length < -0.5 or in_mirror(tri):
            continue
        liner.append(np.array([offset_point(p, normals, lining) for p in tri])[::-1])
    # Where the band cuts the liner short, skin the gap back out to the paint.
    # An open 9 cm cavity between skin and lining is a slot you can sight
    # straight down the length of the car through once the door is off.
    liner += rim(boundary_edges(banded, band), normals, lining)

    uv = darkest_uv(TEXTURE / "body.png")
    furniture = block(-cabin["floor_half_x"], cabin["floor_half_x"],
                      cabin["floor_bottom_y"], cabin["floor_top_y"],
                      cabin["z_rear"] + .05, cabin["z_front"] - .05, uv)
    for _, x0, x1, y0, y1, z0, z1 in spec.SEATS:
        furniture += block(x0, x1, y0, y1, z0, z1, uv)

    body_open = body_outer + liner + rim(
        boundary_edges(body_outer), normals, lining) + furniture

    report = {
        "door_cut": spec.DOOR,
        "thickness": thickness,
        "liner_thickness": lining,
        "interior_uv": [round(value, 5) for value in uv],
        "triangles": {
            "body": len(indices),
            "body_open": len(body_open),
            "driver_door": len(door),
            "door_outer_skin": len(door_outer),
            "cabin_liner": len(liner),
            "furniture": len(furniture),
            **{name: len(pane) for name, pane in sorted(panes.items())},
        },
        "glass": spec.GLASS,
    }
    if report_only:
        return report

    TARGET.mkdir(parents=True, exist_ok=True)
    (TARGET / "body.emesh").write_bytes((SOURCE / "body.emesh").read_bytes())
    write_emesh(TARGET / "body_open.emesh", body_open, material)
    write_emesh(TARGET / "driver_door.emesh", door, material)
    for name, pane in panes.items():
        write_emesh(TARGET / (name + ".emesh"), pane, material)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="report the split without writing any asset")
    arguments = parser.parse_args()
    report = build(report_only=arguments.dry_run)
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build/car5-next-cook.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
