#!/usr/bin/env python3
"""EVERY LIGHTBAR THE GAME CAN SHOW, WIRED TO THE ONE THE SHADER LIGHTS.

A police lightbar is spread across four files that nothing in the build forces
to agree:

  the mesh          the lens boxes are bodywork on the car's own body mesh
  lit.frag          a hardcoded volume per profile; anything outside it is
                    discarded, so the glow only appears where this says
  *_profiles.inc    the model folder -> profile id the shader matches on
  emergency_lighting.h  police_lightbar_centres(), the spot-light origins

Move a bar and update three of the four and nothing fails. The car still
drives, the lights still flash, and the glow is simply in the wrong place --
or on the wrong parts, which is how this guard came to exist: a lightbar box
was left one lens cell too wide for a while and lit a chrome rib with the bank.

So this runs in the gate, before anything is compiled, and asserts the one
thing that ties the four together: THE SPOT-LIGHT CENTRE OF EVERY CAR THAT HAS
A LIGHTBAR FALLS INSIDE A GLOW BOX THE SHADER ACTUALLY CONTAINS. For Car
5-NEXT PATROL, whose bar is generated, it also checks the boxes against the
generator that prints them, cell by cell.

Text parsing, deliberately, like tools/guard_sim_purity.sh: it needs no build,
no cooked assets (assets/models/ is gitignored, so a fresh clone has none) and
nothing outside the standard library. Every parse asserts it found something,
because a regex that silently matches nothing is a guard that silently stops
guarding.

    tools/guard_lightbar_profiles.py
"""
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # never add to the cache described in load_generator

ROOT = Path(__file__).resolve().parents[1]

FRAG = ROOT / "assets/shaders/lit.frag"
INC = ROOT / "assets/shaders/vehicle_headlight_profiles.inc"
CATALOG = ROOT / "src/app/player_car_catalog.h"
LIGHTING = ROOT / "src/app/emergency_lighting.h"

# The generated bar, and the profile it is generated for.
GENERATED_PROFILE = 29
GENERATED_SPEC = "car5_next_police_spec"

TOLERANCE = 5e-4


class Failure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Failure(message)


def glow_boxes():
    """profile id -> [(x0, x1, y0, y1, z0, z1)] as lit.frag spells them.

    Both spellings are matched: the first clause of the chain assigns, the
    rest OR into it. Missing the assigning one would quietly drop a profile.
    """
    source = FRAG.read_text()
    pattern = re.compile(
        r"v_headlight_profile == (\d+) &&\s*"
        r"x>=([-\d.]+) && x<=([-\d.]+) && p\.y>=([-\d.]+) && p\.y<=([-\d.]+) &&\s*"
        r"p\.z>=([-\d.]+) && p\.z<=([-\d.]+)\s*\)?\s*;?")
    found = {}
    for match in pattern.finditer(source):
        found.setdefault(int(match.group(1)), []).append(
            tuple(float(value) for value in match.groups()[1:]))
    require(found, f"{FRAG.name}: no lightbar volume parsed; has the clause changed shape?")
    return found


def profile_of_slug():
    """model folder -> headlight profile id, from the table both sides include."""
    found = {match.group(1): int(match.group(2)) for match in
             re.finditer(r"HEADLIGHT_MODEL\((\w+),\s*(\d+)\)", INC.read_text())}
    require(found, f"{INC.name}: no HEADLIGHT_MODEL rows parsed")
    return found


def slug_of_car():
    """PlayerCarId -> model folder, from the catalog rows."""
    found = {match.group(1): match.group(2) for match in re.finditer(
        r"\{PlayerCarId::(\w+),\s*\"[^\"]*\",\s*\"[^\"]*\",\s*\n?\s*\"models/vehicles/(\w+)/",
        CATALOG.read_text())}
    require(found, f"{CATALOG.name}: no catalog rows parsed")
    return found


def lightbar_centres():
    """PlayerCarId -> the red bank's spot-light centre.

    Only the cars named explicitly: the trailing `return` in that function is a
    fallback for models with no bar of their own, and holding a fallback to a
    real box would assert something the game never draws.
    """
    found = {}
    for match in re.finditer(
            r"model\s*==\s*PlayerCarId::(\w+)\)\s*\n\s*return \{\{\{(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\}",
            LIGHTING.read_text()):
        found[match.group(1)] = tuple(float(value) for value in match.groups()[1:])
    require(found, f"{LIGHTING.name}: no police_lightbar_centres rows parsed")
    return found


def inside(point, box):
    # The shader negates X for the red bank, so one box serves both.
    x, y, z = abs(point[0]), point[1], point[2]
    return (box[0] - TOLERANCE <= x <= box[1] + TOLERANCE and
            box[2] - TOLERANCE <= y <= box[3] + TOLERANCE and
            box[4] - TOLERANCE <= z <= box[5] + TOLERANCE)


class Generator:
    """The bar generator, read off disk and executed in a throwaway namespace.

    NOT an import, and not importlib either -- both go through the bytecode
    cache, and the cache can lie. Python validates a .pyc on the source's mtime
    and SIZE, so editing one digit invalidates neither: CELLS_PER_BANK 3 and 4
    are the same number of bytes. Worse, the stock macOS python writes that
    cache to ~/Library/Caches/com.apple.python/ rather than beside the source,
    so deleting the repo's __pycache__ does not clear it and nothing in the
    tree hints it exists.

    That cost an hour here: this guard reported drift against a spec nobody had
    on disk, and it would just as happily have stayed silent about drift that
    was real. A guard that can read something other than the file in the tree
    is not a guard.
    """

    def __init__(self, path):
        require(path.is_file(), f"{path.name} is missing; the generated bar has no generator")
        namespace = {"__name__": "car5_next_police_spec", "__file__": str(path)}
        exec(compile(path.read_text(), str(path), "exec"), namespace)
        self.namespace = namespace

    def __getattr__(self, name):
        require(name in self.namespace,
                f"{GENERATED_SPEC}.py no longer defines {name}()")
        return self.namespace[name]


def load_generator():
    return Generator(ROOT / "tools" / (GENERATED_SPEC + ".py"))


def check_generated(boxes):
    """The generated bar: boxes cell by cell, and nothing chrome inside one."""
    spec = load_generator()
    declared = [(cell["x"][0], cell["x"][1], cell["y"][0], cell["y"][1],
                 cell["z"][0], cell["z"][1]) for cell in spec.lens_boxes()]
    shader = boxes.get(GENERATED_PROFILE, [])
    require(len(shader) == len(declared),
            f"{FRAG.name} has {len(shader)} boxes for profile {GENERATED_PROFILE}, "
            f"{GENERATED_SPEC} builds {len(declared)} lens cells")
    for spelled, built in zip(sorted(shader), sorted(declared)):
        require(all(abs(a - b) < TOLERANCE for a, b in zip(spelled, built)),
                f"{FRAG.name} box {spelled} does not match the generated cell {built}")
    lit = dark = 0
    for name, x0, x1, y0, y1, z0, z1, _ in spec.lightbar_boxes():
        centre = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
        glows = any(inside(centre, box) for box in shader)
        if "Lens" in name:
            require(glows, f"lightbar part {name} is a lens but sits outside every glow box")
            lit += 1
        else:
            require(not glows, f"lightbar part {name} is not a lens but would light with the bank")
            dark += 1
    require(lit and dark, "the generated bar has no lenses or no trim; check lightbar_boxes()")
    return lit, dark


def main():
    boxes = glow_boxes()
    profiles = profile_of_slug()
    slugs = slug_of_car()
    centres = lightbar_centres()

    checked = []
    for car, centre in sorted(centres.items()):
        slug = slugs.get(car)
        require(slug, f"{car} has a lightbar centre but no catalog row")
        profile = profiles.get(slug)
        require(profile is not None,
                f"{car} rides {slug}, which has no HEADLIGHT_MODEL row")
        owned = boxes.get(profile, [])
        require(owned, f"{car} has a lightbar but {FRAG.name} lights nothing "
                       f"for its profile {profile}")
        require(any(inside(centre, box) for box in owned),
                f"{car}: the spot-light centre {centre} is outside every glow box "
                f"the shader tests for profile {profile}. The lamps would cast "
                f"from somewhere the lenses do not light.")
        checked.append(f"{car} (profile {profile}, {len(owned)} box"
                       f"{'es' if len(owned) != 1 else ''})")

    lit, dark = check_generated(boxes)

    print(f"lightbar guard: {len(checked)} cars wired to their glow volume")
    for line in checked:
        print(f"  {line}")
    print(f"  generated bar: {lit} lens cells lit, {dark} trim parts left dark")
    orphans = sorted(set(boxes) - set(profiles.values()))
    if orphans:
        # Reported, not failed: a glow volume for a profile no model claims is
        # a dead branch, which shows no lightbar rather than a wrong one.
        print(f"  note: {FRAG.name} still lights profile(s) {orphans}, which no "
              f"HEADLIGHT_MODEL row claims")


if __name__ == "__main__":
    try:
        main()
    except Failure as failure:
        print(f"lightbar guard FAILED: {failure}", file=sys.stderr)
        raise SystemExit(1)
