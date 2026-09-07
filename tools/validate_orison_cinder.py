#!/usr/bin/env python3
"""Check the standalone Orison Cinder body and its shared-wheel fit.

This reads asset files only. It writes a JSON report and UV preview under build;
it neither changes the model nor touches vehicle registration or gameplay.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from make_vesper_vx91_assets import read_emesh


ROOT = Path(__file__).resolve().parents[1]
WHEELS = {"x": 0.80, "height": 0.34, "front": 1.30, "rear": -1.24,
          "radius": 0.326}
SPACING = 0.025
MAX_LOCK = 0.82
FIT_MARGIN = 0.002


def projected_hits(point: np.ndarray, triangles: np.ndarray,
                   axes: tuple[int, int]) -> tuple[np.ndarray, np.ndarray]:
    """Return projected receiver indices and barycentric weights, edges included."""
    projected = triangles[:, :, axes]
    a = projected[:, 0]
    ab = projected[:, 1] - a
    ac = projected[:, 2] - a
    ap = point - a
    determinant = ab[:, 0] * ac[:, 1] - ab[:, 1] * ac[:, 0]
    valid = np.abs(determinant) > 1e-10
    u = np.zeros(len(triangles))
    v = np.zeros(len(triangles))
    u[valid] = (ap[valid, 0] * ac[valid, 1]
                - ap[valid, 1] * ac[valid, 0]) / determinant[valid]
    v[valid] = (ab[valid, 0] * ap[valid, 1]
                - ab[valid, 1] * ap[valid, 0]) / determinant[valid]
    weights = np.stack((1 - u - v, u, v), axis=1)
    ids = np.flatnonzero(valid & (weights >= -1e-7).all(axis=1))
    return ids, weights[ids]


def sample_surfaces(triangles: np.ndarray) -> np.ndarray:
    """Barycentric grid with adjacent samples at most SPACING metres apart."""
    clouds = []
    for tri in triangles:
        longest = max(float(np.linalg.norm(tri[a] - tri[b]))
                      for a, b in ((0, 1), (1, 2), (2, 0)))
        steps = max(1, math.ceil(longest / SPACING))
        for row in range(steps + 1):
            columns = np.arange(steps + 1 - row)[:, None] / steps
            clouds.append(tri[0] + row / steps * (tri[1] - tri[0])
                          + columns * (tri[2] - tri[0]))
    return np.concatenate(clouds) if clouds else np.empty((0, 3))


def validate(args: argparse.Namespace) -> dict:
    report = {"model": "orison_cinder", "mesh": str(args.mesh),
              "texture": str(args.texture), "coordinate_order": ["x", "up", "forward"],
              "wheels": WHEELS, "checks": {},
              "limits": ["Body-only source collection is checked by the Blender builder.",
                         "Tire clearance uses sampled surfaces, not exact solid intersection."]}

    def check(name: str, passed: bool, **details) -> None:
        report["checks"][name] = {"passed": bool(passed), **details}

    try:
        vertex_records, index_records = read_emesh(args.mesh)
        vertices = np.asarray(vertex_records, dtype=np.float64).reshape(-1, 12)
        indices = np.asarray(index_records, dtype=np.int64)
        report["mesh_sha256"] = hashlib.sha256(args.mesh.read_bytes()).hexdigest()
        check("static_mesh_readable", True)
    except Exception as exc:
        check("static_mesh_readable", False, error=str(exc))
        return report

    report.update(vertices=len(vertices), triangles=len(indices) // 3)
    valid_indices = (len(vertices) > 0 and len(indices) > 0 and len(indices) % 3 == 0
                     and indices.min() >= 0 and indices.max() < len(vertices))
    check("valid_triangle_indices", valid_indices)
    finite = bool(np.isfinite(vertices).all())
    check("finite_vertex_data", finite)
    if not valid_indices or not finite:
        return report

    triangles = vertices[indices.reshape(-1, 3), :3]
    doubled_area = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                          triangles[:, 2] - triangles[:, 0]), axis=1)
    bad_faces = np.flatnonzero(doubled_area <= 1e-9)
    check("nondegenerate_triangles", len(bad_faces) == 0,
          invalid_count=len(bad_faces), first_invalid_faces=bad_faces[:12].tolist(),
          minimum_area=float(doubled_area.min() * 0.5))
    check("triangle_budget", 650 <= len(triangles) <= 1800,
          actual=len(triangles), minimum=650, maximum=1800)
    uv = vertices[:, 6:8]
    check("uv_bounds", bool(((uv >= -1e-7) & (uv <= 1 + 1e-7)).all()),
          minimum=uv.min(axis=0).tolist(), maximum=uv.max(axis=0).tolist())
    lo = vertices[:, :3].min(axis=0)
    hi = vertices[:, :3].max(axis=0)
    report.update(bounds_min=lo.tolist(), bounds_max=hi.tolist(), dimensions=(hi - lo).tolist())
    positions = {tuple(p) for p in np.round(vertices[:, :3], 4)}
    asymmetry = [p for p in positions if (-p[0], p[1], p[2]) not in positions]
    check("left_right_symmetry", len(asymmetry) == 0,
          missing_mirror_count=len(asymmetry), first_missing_mirrors=asymmetry[:12])

    atlas = None
    try:
        with Image.open(args.texture) as source:
            atlas = source.copy()
        alpha = atlas.getchannel("A").getextrema() if atlas.mode == "RGBA" else None
        check("opaque_256_rgba_atlas", atlas.size == (256, 256)
              and atlas.mode == "RGBA" and alpha == (255, 255),
              size=list(atlas.size), mode=atlas.mode, alpha_extrema=alpha)
        report["atlas_sha256"] = hashlib.sha256(args.texture.read_bytes()).hexdigest()
    except Exception as exc:
        check("opaque_256_rgba_atlas", False, error=str(exc))

    opening_failures = []
    opening_samples = 0
    for axle_name in ("front", "rear"):
        axle = WHEELS[axle_name]
        for side in (-1, 1):
            # The outer wall must leave actual space for the tire. The narrower
            # central chassis can remain behind it without closing the arch.
            outer = triangles[(side * triangles[:, :, 0] > 0.70).all(axis=1)]
            for dz, dy in ((0.013, 0.017), (-0.15, 0.017), (0.15, 0.017),
                           (0.013, -0.14), (0.013, 0.16)):
                point = np.array([axle + dz, WHEELS["height"] + dy])
                hit_ids, _ = projected_hits(point, outer, (2, 1))
                opening_samples += 1
                if len(hit_ids):
                    opening_failures.append({"axle": axle_name, "side": side,
                                             "forward_height": point.tolist(),
                                             "intersecting_faces": len(hit_ids)})
    check("four_open_wheel_arches", not opening_failures, samples=opening_samples,
          outboard_x_threshold=0.70, failures=opening_failures)

    roof_failures = []
    roof_samples = 0
    for axle_name, offsets in (("front", (-0.25, 0.25)), ("rear", (-0.20, 0.20))):
        for offset in offsets:
            forward = WHEELS[axle_name] + offset
            for across in (-0.55, -0.20, 0.20, 0.55):
                point = np.array([across, forward])
                hit_ids, weights = projected_hits(point, triangles, (0, 2))
                heights = (weights * triangles[hit_ids, :, 1]).sum(axis=1)
                roof_samples += 1
                if not (heights > 0.68).any():
                    roof_failures.append({"across_forward": point.tolist(),
                                          "highest_surface": float(heights.max()) if len(heights) else None})
    check("continuous_top_coverage", not roof_failures, samples=roof_samples,
          minimum_height=0.68, failures=roof_failures)

    try:
        wheel_records, _ = read_emesh(args.wheel)
        wheel_positions = np.asarray(wheel_records, dtype=np.float64)[:, :3]
        native_radius = float(max(np.ptp(wheel_positions[:, 1]),
                                  np.ptp(wheel_positions[:, 2])) * 0.5)
        wheel_scale = WHEELS["radius"] / native_radius
        native_halfwidth = float(np.abs(wheel_positions[:, 0]).max())
        halfwidth = native_halfwidth * wheel_scale
        report["shared_wheel"] = {"path": str(args.wheel), "native_radius": native_radius,
                                  "native_halfwidth": native_halfwidth, "scale": wheel_scale,
                                  "fitted_radius": WHEELS["radius"], "fitted_halfwidth": halfwidth}
        check("shared_wheel_readable", True)
        near_tires = triangles[triangles[:, :, 1].min(axis=1)
                               <= WHEELS["height"] + WHEELS["radius"] + FIT_MARGIN]
        cloud = sample_surfaces(near_tires)
        failures = []
        poses = 0
        for axle_name in ("front", "rear"):
            for side in (-1, 1):
                centre = np.array([side * WHEELS["x"], WHEELS["height"], WHEELS[axle_name]])
                relative = cloud - centre
                angles = np.linspace(-MAX_LOCK, MAX_LOCK, 17) if axle_name == "front" else [0.0]
                for angle in angles:
                    width = relative[:, 0] * math.cos(angle) + relative[:, 2] * math.sin(angle)
                    radial = -relative[:, 0] * math.sin(angle) + relative[:, 2] * math.cos(angle)
                    contacts = ((np.abs(width) < halfwidth + FIT_MARGIN)
                                & (radial ** 2 + relative[:, 1] ** 2
                                   < (WHEELS["radius"] + FIT_MARGIN) ** 2))
                    poses += 1
                    if contacts.any():
                        failures.append({"axle": axle_name, "side": side, "angle": float(angle),
                                         "contact_samples": int(contacts.sum()),
                                         "first_contact_positions": cloud[contacts][:6].tolist()})
        check("sampled_tire_clearance", not failures, surface_samples=len(cloud),
              maximum_sample_spacing=SPACING, maximum_lock=MAX_LOCK,
              fit_margin=FIT_MARGIN, poses=poses, failures=failures)
    except Exception as exc:
        check("shared_wheel_or_clearance_readable", False, error=str(exc))

    guide = atlas.convert("RGBA") if atlas is not None else Image.new("RGBA", (256, 256), (20, 23, 27, 255))
    draw = ImageDraw.Draw(guide)
    width, height = guide.size
    for tri in indices.reshape(-1, 3):
        points = [(float(vertices[i, 6] * width), float((1 - vertices[i, 7]) * height)) for i in tri]
        draw.line(points + points[:1], fill=(246, 177, 72, 220), width=1)
    args.uv_guide.parent.mkdir(parents=True, exist_ok=True)
    guide.save(args.uv_guide)
    report["uv_guide"] = str(args.uv_guide)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesh", type=Path, default=ROOT / "assets/models/vehicles/orison_cinder/body.emesh")
    parser.add_argument("--texture", type=Path, default=ROOT / "assets/textures/vehicles/orison_cinder/body.png")
    parser.add_argument("--wheel", type=Path, default=ROOT / "assets/models/vehicles/common/wheel.emesh")
    parser.add_argument("--report", type=Path, default=ROOT / "build/orison-cinder-fit-report.json")
    parser.add_argument("--uv-guide", type=Path, default=ROOT / "build/orison-cinder-uv.png")
    args = parser.parse_args()
    report = validate(args)
    failures = [name for name, value in report["checks"].items() if not value["passed"]]
    report["passed"] = not failures
    report["failed_checks"] = failures
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    for name, result in report["checks"].items():
        print(f"{'PASS' if result['passed'] else 'FAIL'} {name}")
    print(f"Orison Cinder: {report.get('triangles', 0)} triangles; report {args.report}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
