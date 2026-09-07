#!/usr/bin/env python3
"""Build the original rigged PS2-era 1930s young man character."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFERENCE = ROOT / "assets/textures/characters/young_man_1930s/body-reference.png"
DEFAULT_TEXTURE = ROOT / "assets/textures/characters/young_man_1930s/body.png"
DEFAULT_MODEL = ROOT / "assets/models/characters/young_man_1930s/young_man_1930s.fbx"
DEFAULT_PREVIEW = ROOT / "build/young-man-1930s-preview.png"
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")


def orchestrate() -> None:
    from PIL import Image

    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--texture", type=Path, default=DEFAULT_TEXTURE)
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--preview", type=Path, default=DEFAULT_PREVIEW)
    args = parser.parse_args()

    if not BLENDER.exists():
        raise SystemExit(f"Blender not found at {BLENDER}")
    with Image.open(args.reference) as source:
        atlas = source.convert("RGBA").resize(
            (512, 512), Image.Resampling.LANCZOS
        )
    args.texture.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(args.texture, optimize=True)
    args.model.parent.mkdir(parents=True, exist_ok=True)
    args.preview.parent.mkdir(parents=True, exist_ok=True)

    subprocess.run([
        str(BLENDER), "--background", "--python", str(Path(__file__).resolve()),
        "--", "--blender-stage", str(args.texture.resolve()),
        str(args.model.resolve()), str(args.preview.resolve()),
    ], check=True)


def blender_stage(texture_path: Path, model_path: Path,
                  preview_path: Path) -> None:
    import bpy
    from math import pi
    from mathutils import Vector

    bpy.ops.wm.read_factory_settings(use_empty=True)

    # Rectangles are in Blender/OpenGL bottom-left UV coordinates and point at
    # semantic panels in the generated straight-on atlas.
    face_front = (0.020, 0.775, 0.180, 0.985)
    face_side = (0.195, 0.775, 0.350, 0.985)
    face_back = (0.365, 0.775, 0.505, 0.985)
    ear = (0.670, 0.785, 0.760, 0.930)
    hair = (0.775, 0.775, 0.960, 0.985)
    jacket_front = (0.015, 0.440, 0.305, 0.745)
    jacket_back = (0.315, 0.440, 0.615, 0.745)
    jacket_side = (0.630, 0.440, 0.705, 0.745)
    denim_front_upper = (0.015, 0.280, 0.125, 0.415)
    denim_side_upper = (0.265, 0.280, 0.380, 0.415)
    denim_back_upper = (0.625, 0.280, 0.705, 0.415)
    denim_front_lower = (0.015, 0.145, 0.125, 0.280)
    denim_side_lower = (0.265, 0.145, 0.380, 0.280)
    denim_back_lower = (0.625, 0.145, 0.705, 0.280)
    hand = (0.015, 0.010, 0.115, 0.120)
    boot_front = (0.530, 0.010, 0.620, 0.130)
    boot_back = (0.245, 0.010, 0.390, 0.130)
    boot_side = (0.630, 0.010, 0.780, 0.130)

    image = bpy.data.images.load(str(texture_path))
    image.colorspace_settings.name = "sRGB"
    material = bpy.data.materials.new("young_man_1930s")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = image
    tex.interpolation = "Linear"
    shader = nodes.get("Principled BSDF")
    material.node_tree.links.new(tex.outputs["Color"],
                                 shader.inputs["Base Color"])
    shader.inputs["Roughness"].default_value = 0.92

    parts = []

    def map_uv(obj, front_rect, back_rect=None, side_rect=None):
        back_rect = back_rect or front_rect
        side_rect = side_rect or front_rect
        mesh = obj.data
        if not mesh.uv_layers:
            mesh.uv_layers.new(name="UVMap")
        layer = mesh.uv_layers.active.data
        coords = [vertex.co for vertex in mesh.vertices]
        mins = [min(co[i] for co in coords) for i in range(3)]
        maxs = [max(co[i] for co in coords) for i in range(3)]

        def fraction(value, low, high):
            return 0.5 if high - low < 1e-7 else (value - low) / (high - low)

        for polygon in mesh.polygons:
            normal = polygon.normal
            if normal.y < -0.35:
                rect = front_rect
            elif normal.y > 0.35:
                rect = back_rect
            else:
                rect = side_rect
            u0, v0, u1, v1 = rect
            for loop_index in polygon.loop_indices:
                co = mesh.vertices[mesh.loops[loop_index].vertex_index].co
                if abs(normal.y) >= max(abs(normal.x), abs(normal.z)):
                    fu = fraction(co.x, mins[0], maxs[0])
                    fv = fraction(co.z, mins[2], maxs[2])
                    if normal.y > 0.0:
                        fu = 1.0 - fu
                elif abs(normal.x) >= abs(normal.z):
                    fu = fraction(co.y, mins[1], maxs[1])
                    fv = fraction(co.z, mins[2], maxs[2])
                else:
                    fu = fraction(co.x, mins[0], maxs[0])
                    fv = fraction(co.y, mins[1], maxs[1])
                layer[loop_index].uv = (u0 + fu * (u1 - u0),
                                        v0 + fv * (v1 - v0))

    def bind_part(obj, bone_name):
        obj.data.materials.append(material)
        group = obj.vertex_groups.new(name=bone_name)
        group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
        for polygon in obj.data.polygons:
            polygon.use_smooth = False
        parts.append(obj)

    def make_box(name, location, dimensions, bone_name,
                 front_rect, back_rect=None, side_rect=None):
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
        obj = bpy.context.object
        obj.name = name
        obj.dimensions = dimensions
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        map_uv(obj, front_rect, back_rect, side_rect)
        bind_part(obj, bone_name)
        return obj

    def make_cylinder(name, start, end, radius, bone_name,
                      front_rect, back_rect=None, side_rect=None, vertices=12):
        start_v = Vector(start)
        end_v = Vector(end)
        axis = end_v - start_v
        bpy.ops.mesh.primitive_cylinder_add(
            vertices=vertices, radius=radius, depth=axis.length,
            location=(start_v + end_v) * 0.5,
        )
        obj = bpy.context.object
        obj.name = name
        obj.rotation_mode = "QUATERNION"
        obj.rotation_quaternion = Vector((0.0, 0.0, 1.0)).rotation_difference(axis)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        map_uv(obj, front_rect, back_rect, side_rect)
        bind_part(obj, bone_name)
        return obj

    # Jacketed torso, jeans block, and the small shirt/collar silhouette are
    # deliberately chunky. Fine seams, pockets and buttons live in the
    # near-photographic diffuse atlas.
    make_box("JacketTorso", (0.0, 0.0, 1.285), (0.50, 0.24, 0.52),
             "mixamorig:Spine2", jacket_front, jacket_back, jacket_side)
    make_box("JeansWaist", (0.0, 0.0, 0.955), (0.35, 0.21, 0.18),
             "mixamorig:Hips", denim_front_upper, denim_back_upper,
             denim_side_upper)
    make_cylinder("Neck", (0.0, 0.0, 1.535), (0.0, 0.0, 1.625), 0.075,
                  "mixamorig:Neck", face_side, face_back, face_side, 8)

    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,
                                          location=(0.0, -0.008, 1.735))
    head = bpy.context.object
    head.name = "Head"
    head.scale = (0.145, 0.125, 0.175)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    map_uv(head, face_front, face_back, face_side)
    bind_part(head, "mixamorig:Head")

    # Open low-poly hair cap: two octagonal rings and a crown, with the front
    # edge lowered slightly to suggest a neat side-parted 1930s cut.
    verts = []
    faces = []
    for ring, (z, rx, ry) in enumerate(((1.710, 0.151, 0.130),
                                        (1.820, 0.112, 0.102))):
        for i in range(8):
            angle = 2.0 * pi * i / 8.0
            front_drop = 0.018 if i in (5, 6, 7) else 0.0
            verts.append((rx * __import__("math").cos(angle),
                          ry * __import__("math").sin(angle), z - front_drop))
    verts.append((0.022, 0.005, 1.868))
    for i in range(8):
        j = (i + 1) % 8
        faces.append((i, j, 8 + j, 8 + i))
        faces.append((8 + i, 8 + j, 16))
    hair_mesh = bpy.data.meshes.new("HairCapMesh")
    hair_mesh.from_pydata(verts, [], faces)
    hair_mesh.update()
    hair_obj = bpy.data.objects.new("HairCap", hair_mesh)
    bpy.context.scene.collection.objects.link(hair_obj)
    map_uv(hair_obj, hair, hair, hair)
    bind_part(hair_obj, "mixamorig:Head")

    for side in (-1.0, 1.0):
        prefix = "Left" if side > 0.0 else "Right"
        make_box(f"{prefix}Ear", (side * 0.148, 0.0, 1.735),
                 (0.026, 0.045, 0.072), "mixamorig:Head",
                 ear, ear, ear)
        make_cylinder(f"{prefix}UpperSleeve",
                      (side * 0.235, 0.0, 1.430),
                      (side * 0.565, 0.0, 1.430), 0.095,
                      f"mixamorig:{prefix}Arm",
                      jacket_side, jacket_side, jacket_side)
        make_cylinder(f"{prefix}ForeSleeve",
                      (side * 0.565, 0.0, 1.430),
                      (side * 0.835, 0.0, 1.430), 0.078,
                      f"mixamorig:{prefix}ForeArm",
                      jacket_side, jacket_side, jacket_side)
        make_box(f"{prefix}Cuff", (side * 0.820, 0.0, 1.430),
                 (0.075, 0.18, 0.17), f"mixamorig:{prefix}ForeArm",
                 jacket_side, jacket_side, jacket_side)
        make_box(f"{prefix}Hand", (side * 0.930, -0.005, 1.430),
                 (0.22, 0.13, 0.060), f"mixamorig:{prefix}Hand",
                 hand, hand, hand)

        leg_x = side * 0.100
        make_cylinder(f"{prefix}Thigh", (leg_x, 0.0, 0.900),
                      (leg_x, 0.0, 0.520), 0.105,
                      f"mixamorig:{prefix}UpLeg",
                      denim_front_upper, denim_back_upper, denim_side_upper)
        make_cylinder(f"{prefix}Shin", (leg_x, 0.0, 0.520),
                      (leg_x, 0.0, 0.145), 0.088,
                      f"mixamorig:{prefix}Leg",
                      denim_front_lower, denim_back_lower, denim_side_lower)
        make_box(f"{prefix}Boot", (leg_x, -0.055, 0.085),
                 (0.185, 0.310, 0.170), f"mixamorig:{prefix}Foot",
                 boot_front, boot_back, boot_side)

    # Join all visible pieces into one game-ready mesh while keeping the rigid
    # per-piece weights that give early-2000s low-poly animation its firm bends.
    bpy.ops.object.select_all(action="DESELECT")
    for obj in parts:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    character = bpy.context.object
    character.name = "YoungMan1930s"

    arm_data = bpy.data.armatures.new("YoungMan1930s_Rig")
    armature = bpy.data.objects.new("Armature", arm_data)
    bpy.context.scene.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bones = {}

    def bone(name, head_pos, tail_pos, parent=None):
        edit = arm_data.edit_bones.new(name)
        edit.head = head_pos
        edit.tail = tail_pos
        if parent:
            edit.parent = bones[parent]
        bones[name] = edit

    bone("mixamorig:Hips", (0, 0, 0.91), (0, 0, 1.01))
    bone("mixamorig:Spine", (0, 0, 1.01), (0, 0, 1.17), "mixamorig:Hips")
    bone("mixamorig:Spine1", (0, 0, 1.17), (0, 0, 1.34), "mixamorig:Spine")
    bone("mixamorig:Spine2", (0, 0, 1.34), (0, 0, 1.50), "mixamorig:Spine1")
    bone("mixamorig:Neck", (0, 0, 1.50), (0, 0, 1.62), "mixamorig:Spine2")
    bone("mixamorig:Head", (0, 0, 1.62), (0, 0, 1.82), "mixamorig:Neck")
    bone("mixamorig:HeadTop_End", (0, 0, 1.82), (0, 0, 1.89), "mixamorig:Head")

    for side in (-1.0, 1.0):
        prefix = "Left" if side > 0.0 else "Right"
        shoulder = f"mixamorig:{prefix}Shoulder"
        arm = f"mixamorig:{prefix}Arm"
        fore = f"mixamorig:{prefix}ForeArm"
        hand_bone = f"mixamorig:{prefix}Hand"
        bone(shoulder, (0, 0, 1.45), (side * 0.235, 0, 1.43),
             "mixamorig:Spine2")
        bone(arm, (side * 0.235, 0, 1.43), (side * 0.565, 0, 1.43), shoulder)
        bone(fore, (side * 0.565, 0, 1.43), (side * 0.835, 0, 1.43), arm)
        bone(hand_bone, (side * 0.835, 0, 1.43),
             (side * 0.995, 0, 1.43), fore)
        parent = hand_bone
        for index in range(1, 5):
            name = f"mixamorig:{prefix}HandIndex{index}"
            start = 0.995 + (index - 1) * 0.025
            bone(name, (side * start, -0.015, 1.43),
                 (side * (start + 0.023), -0.015, 1.43), parent)
            parent = name

        upper = f"mixamorig:{prefix}UpLeg"
        lower = f"mixamorig:{prefix}Leg"
        foot = f"mixamorig:{prefix}Foot"
        toe = f"mixamorig:{prefix}ToeBase"
        bone(upper, (side * 0.10, 0, 0.94),
             (side * 0.10, 0, 0.52), "mixamorig:Hips")
        bone(lower, (side * 0.10, 0, 0.52),
             (side * 0.10, 0, 0.14), upper)
        bone(foot, (side * 0.10, 0, 0.14),
             (side * 0.10, -0.16, 0.07), lower)
        bone(toe, (side * 0.10, -0.16, 0.07),
             (side * 0.10, -0.25, 0.07), foot)
        bone(f"mixamorig:{prefix}Toe_End",
             (side * 0.10, -0.25, 0.07),
             (side * 0.10, -0.30, 0.07), toe)

    bpy.ops.object.mode_set(mode="OBJECT")
    character.parent = armature
    modifier = character.modifiers.new("Armature", "ARMATURE")
    modifier.object = armature
    # Match the supplied pack's roughly 1.7-1.8 m authored height instead of
    # letting the exaggerated PSX head push this figure over 1.9 m.
    armature.scale = (0.93, 0.93, 0.93)
    bpy.context.view_layer.update()
    armature["asset_name"] = "Young Man 1930s"
    armature["era"] = 1930
    armature["source"] = "original procedural mesh and generated texture"

    # The two supplied packs use 33-bone light rigs or 65-bone finger rigs.
    # Keep the light contract for traffic-scale use.
    if len(armature.data.bones) != 33:
        raise RuntimeError(f"expected 33 bones, got {len(armature.data.bones)}")

    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = character.evaluated_get(depsgraph)
    evaluated_mesh = evaluated.to_mesh()
    evaluated_mesh.calc_loop_triangles()
    triangle_count = len(evaluated_mesh.loop_triangles)
    vertex_count = len(evaluated_mesh.vertices)
    evaluated.to_mesh_clear()
    if not 550 <= triangle_count <= 1000:
        raise RuntimeError(f"triangle budget missed: {triangle_count}")

    bpy.ops.object.select_all(action="DESELECT")
    character.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(
        filepath=str(model_path), use_selection=True, object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True, bake_space_transform=False, add_leaf_bones=False,
        bake_anim=False, path_mode="RELATIVE", axis_forward="-Z", axis_up="Y",
    )

    # One neutral reference render for visual QA; it is not a game asset.
    camera_data = bpy.data.cameras.new("PreviewCamera")
    camera = bpy.data.objects.new("PreviewCamera", camera_data)
    bpy.context.scene.collection.objects.link(camera)
    camera.location = (2.75, -4.8, 1.60)
    target = Vector((0.0, 0.0, 0.96))
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 2.28
    bpy.context.scene.camera = camera

    world = bpy.data.worlds.new("PreviewWorld")
    bpy.context.scene.world = world
    world.color = (0.025, 0.030, 0.040)
    sun_data = bpy.data.lights.new("PreviewSun", "SUN")
    sun = bpy.data.objects.new("PreviewSun", sun_data)
    bpy.context.scene.collection.objects.link(sun)
    sun.rotation_euler = (0.65, -0.45, -0.55)
    sun.data.energy = 2.1

    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 700
    scene.render.resolution_y = 700
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(preview_path)
    scene.view_settings.look = "AgX - Medium High Contrast"
    bpy.ops.render.render(write_still=True)
    print(f"CHARACTER_RESULT vertices={vertex_count} triangles={triangle_count} "
          f"bones={len(armature.data.bones)} model={model_path} texture={texture_path}")


if __name__ == "__main__":
    if "--blender-stage" in sys.argv:
        marker = sys.argv.index("--blender-stage")
        blender_stage(Path(sys.argv[marker + 1]), Path(sys.argv[marker + 2]),
                      Path(sys.argv[marker + 3]))
    else:
        orchestrate()
