#!/usr/bin/env python3
"""Blender source builder for the original neon-green GLM Lunge."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from glm_lunge_spec import REGIONS
from vesper_vx91_blender import VehicleBuilder, assign_uvs, export_emesh


def body_ring(width: float, shoulder: float,
              top: float) -> list[tuple[float, float]]:
    """A hard octagonal section with a narrow, ruler-flat upper deck."""
    return [
        (-width * 0.78, 0.26), (-width, 0.38), (-width, shoulder),
        (-width * 0.56, top), (width * 0.56, top),
        (width, shoulder), (width, 0.38), (width * 0.78, 0.26),
    ]


def canopy_ring(shoulder: float, roof_width: float,
                roof_z: float) -> list[tuple[float, float]]:
    """A narrow trapezoid canopy instead of the Vesper's broad fastback."""
    return [
        (-shoulder, 0.78), (-shoulder, 0.91),
        (-roof_width, roof_z - 0.05), (-roof_width * 0.72, roof_z),
        (roof_width * 0.72, roof_z), (roof_width, roof_z - 0.05),
        (shoulder, 0.91), (shoulder, 0.78),
    ]


def side_panel(builder: VehicleBuilder, name: str, side: float,
               yz: list[tuple[float, float]], material: str,
               x: float) -> None:
    builder.panel(name, [(side * x, y, z) for y, z in yz], material,
                  (side, 0.0, 0.0))


def build_vehicle():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    builder = VehicleBuilder()

    builder.box("ChassisCore", (-0.67, -2.52, 0.27),
                (0.67, 2.47, 0.58), "BODY_SHADOW")
    builder.box("FlatFloor", (-0.79, -2.42, 0.20),
                (0.79, 2.40, 0.30), "CLADDING")

    # The Lunge is a cab-forward mid-engine wedge: almost no hood, a broad
    # rear deck, and a waist that pinches ahead of the cockpit. It should read
    # as an exotic from the silhouette alone, not as another long-nose coupe.
    builder.loft("Monowedge", [
        (-2.75, body_ring(1.18, 0.72, 0.84)),
        (-2.24, body_ring(1.25, 0.82, 0.91)),
        (-1.62, body_ring(1.25, 0.85, 0.94)),
        (-0.66, body_ring(1.18, 0.79, 0.87)),
        (0.46, body_ring(1.10, 0.67, 0.73)),
        (1.58, body_ring(1.18, 0.58, 0.65)),
        (2.25, body_ring(1.10, 0.49, 0.54)),
        (2.75, body_ring(0.94, 0.40, 0.43)),
    ], "BODY_TOP")

    builder.loft("ForwardCanopy", [
        (-1.02, canopy_ring(0.96, 0.69, 0.98)),
        (-0.66, canopy_ring(0.91, 0.66, 1.17)),
        (-0.08, canopy_ring(0.86, 0.62, 1.34)),
        (0.58, canopy_ring(0.85, 0.59, 1.36)),
        (1.30, canopy_ring(0.92, 0.72, 0.92)),
    ], "BODY_SIDE")

    for side in (-1.0, 1.0):
        suffix = "L" if side < 0 else "R"
        builder.arch(f"FrontArch{suffix}", side, 1.58, 0.50, 0.52, 0.60, 10)
        builder.arch(f"RearArch{suffix}", side, -1.62, 0.50, 0.52, 0.60, 10)
        x0, x1 = ((-1.20, -0.79) if side < 0 else (0.79, 1.20))
        builder.box(f"KnifeRocker{suffix}", (x0, -1.03, 0.25),
                    (x1, 1.02, 0.40), "CLADDING")
        builder.box(f"FrontBlade{suffix}", (x0, 2.16, 0.30),
                    (x1, 2.75, 0.52), "BODY_SIDE")
        builder.box(f"RearHaunch{suffix}", (x0, -2.75, 0.31),
                    (x1, -2.10, 0.82), "BODY_SIDE")

        # A single-piece canopy and a huge rising intake are the side-view
        # signature. The diagonal door cut hints at a scissor-door hinge.
        side_panel(builder, f"DoorGlass{suffix}", side, [
            (1.18, 0.89), (0.55, 1.31), (-0.03, 1.30), (-0.18, 0.89),
        ], "GLASS", 0.865)
        side_panel(builder, f"QuarterGlass{suffix}", side, [
            (-0.24, 0.89), (-0.25, 1.28), (-0.67, 1.15), (-0.94, 0.89),
        ], "GLASS", 0.925)
        side_panel(builder, f"RisingIntake{suffix}", side, [
            (-1.48, 0.44), (-0.18, 0.49), (-0.31, 0.88), (-1.16, 0.78),
        ], "BLACK", 1.248)
        side_panel(builder, f"IntakeBlade{suffix}", side, [
            (-1.33, 0.50), (-0.35, 0.55), (-0.43, 0.61), (-1.24, 0.58),
        ], "BODY_SHADOW", 1.249)
        side_panel(builder, f"ScissorDoorCut{suffix}", side, [
            (1.03, 0.45), (1.07, 0.47), (0.49, 0.89), (0.45, 0.89),
        ], "SEAM", 1.205)
        side_panel(builder, f"DoorSillCut{suffix}", side, [
            (-0.20, 0.44), (1.04, 0.44), (1.04, 0.47), (-0.20, 0.47),
        ], "SEAM", 1.207)
        side_panel(builder, f"FlushHandle{suffix}", side, [
            (0.03, 0.83), (0.28, 0.83), (0.28, 0.86), (0.03, 0.86),
        ], "BLACK", 1.209)
        side_panel(builder, f"Marker{suffix}", side, [
            (2.02, 0.52), (2.18, 0.52), (2.18, 0.59), (2.02, 0.59),
        ], "MARKER_AMBER", 1.152)

    builder.panel("PanoramicWindshield", [
        (-0.87, 1.265, 0.86), (0.87, 1.265, 0.86),
        (0.58, 0.605, 1.325), (-0.58, 0.605, 1.325),
    ], "GLASS", (0.0, 1.0, 0.5))
    builder.panel("ShortRearGlass", [
        (0.62, -0.645, 1.145), (-0.62, -0.645, 1.145),
        (-0.82, -1.005, 0.89), (0.82, -1.005, 0.89),
    ], "GLASS", (0.0, -1.0, 0.4))
    builder.box("LowDashboard", (-0.61, 0.72, 0.84),
                (0.61, 0.84, 0.92), "DASH")
    builder.box("SeatLeft", (-0.56, -0.10, 0.79),
                (-0.10, 0.15, 1.08), "INTERIOR")
    builder.box("SeatRight", (0.10, -0.10, 0.79),
                (0.56, 0.15, 1.08), "INTERIOR")

    # Razor-thin lamps and a split lower mouth replace the Vesper's closed
    # pop-ups. The nose stays flat and nearly featureless from above.
    builder.box("FrontSplitter", (-1.25, 2.66, 0.22),
                (1.25, 2.75, 0.31), "CLADDING")
    builder.panel("FrontMouth", [
        (-0.72, 2.752, 0.32), (0.72, 2.752, 0.32),
        (0.58, 2.752, 0.43), (-0.58, 2.752, 0.43),
    ], "BLACK", (0.0, 1.0, 0.0))
    for side in (-1.0, 1.0):
        x0, x1 = sorted((side * 0.36, side * 1.04))
        builder.panel(f"HeadlampSlash{side}", [
            (x0, 2.754, 0.49), (x1, 2.754, 0.49),
            (x1 * 0.98, 2.754, 0.56), (x0 * 0.98, 2.754, 0.56),
        ], "REVERSE", (0.0, 1.0, 0.0))

    # The rear is one heavy black slab with four square lamps. A louvered
    # engine deck does the visual work, so this version needs no rear wing.
    builder.box("RearBumper", (-1.25, -2.75, 0.24),
                (1.25, -2.66, 0.36), "CLADDING")
    builder.panel("RearBlackout", [
        (-1.16, -2.752, 0.41), (1.16, -2.752, 0.41),
        (1.16, -2.752, 0.72), (-1.16, -2.752, 0.72),
    ], "BLACK", (0.0, -1.0, 0.0))
    for side in (-1.0, 1.0):
        for inner in (0.34, 0.70):
            x0, x1 = sorted((side * inner, side * (inner + 0.27)))
            builder.panel(f"TailCell{side}_{inner}", [
                (x0, -2.754, 0.49), (x1, -2.754, 0.49),
                (x1, -2.754, 0.66), (x0, -2.754, 0.66),
            ], "TAIL_RED", (0.0, -1.0, 0.0))
    for index, y in enumerate((-2.20, -2.00, -1.80, -1.60)):
        builder.box(f"EngineLouver{index}", (-0.66, y, 0.925),
                    (0.66, y + 0.07, 0.955), "BLACK")
    builder.cylinder("ExhaustL", (-0.18, -2.70, 0.265),
                     0.065, 0.10, 8, "EXHAUST")
    builder.cylinder("ExhaustR", (0.18, -2.70, 0.265),
                     0.065, 0.10, 8, "EXHAUST")

    body = builder.join()
    assign_uvs(body, REGIONS)
    for name, location in {
        "WHEEL_FL": (-1.08, 1.58, 0.50),
        "WHEEL_FR": (1.08, 1.58, 0.50),
        "WHEEL_RL": (-1.08, -1.62, 0.50),
        "WHEEL_RR": (1.08, -1.62, 0.50),
    }.items():
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = "SPHERE"
        empty.empty_display_size = 0.12
        empty.location = location
        bpy.context.collection.objects.link(empty)
    return body


def main() -> None:
    source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(source)
    body = build_vehicle()
    args.blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    vertices, triangles = export_emesh(body, args.mesh, "glm_lunge")
    print(f"GLM_BLENDER body=BODY vertices={vertices} triangles={triangles} "
          f"blend={args.blend} mesh={args.mesh}")


if __name__ == "__main__":
    main()
