"""Blender -b --python tools/ferrone_mast_blender.py [-- --preview OUT.png [--night] [--far]]

Build the Ferrone Mast -- WFRN 97.3's transmitter tower on the summit of
Ferrone Hill, the island-tier landmark docs/design/pinatty.md has always asked
for -- procedurally, in Blender, and cook it into the ignored
assets/models/props/ferrone_mast/.

    /Applications/Blender.app/Contents/MacOS/Blender -b \\
        --python tools/ferrone_mast_blender.py
    python3 tools/validate_ferrone_mast.py

Nothing here comes from a supplied pack: every member is placed by this file,
and the signs are tracked PNGs from tools/make_ferrone_mast_textures.py (run
that first; this script copies them next to the meshes because materials.txt
names textures without a directory). The output still lands under
assets/models/ like every other cooked mesh, so a fresh checkout must run this
script once before the mast draws. The world logs a warning and leaves the
summit bare rather than failing to start without it.

WHAT IS BUILT. A 52 m four-leg self-supporting lattice tower, tapering from a
7.2 m base to 1.8 m at 40 m and straight above, painted in the seven
alternating aviation-orange and white bands a structure of this height wears,
on concrete piers with anchor-bolted base plates. Angle-iron X bracing on every
face, redundant sub-bracing in the tall lower panels, plan bracing, a climbing
ladder with a safety cable and an anti-climb guard, a cable ladder carrying the
coax, a rest platform at 30 m and a railed work platform at 52 m. Four
side-mounted FM bays spaced one wavelength (3.08 m at 97.3 MHz) apart, three
cellular sectors of panel antennas with their radio units, three microwave
dishes aimed down real sight lines (downtown, Kepler, Nickel Heights), and a
6.8 m top pole carrying the flashing L-864 beacon and a lightning rod to 60 m.
Steady L-810 side lights at mid height on every leg. On the pad: a precast
equipment shelter with its door, wall-pack lamp, twin wall-mount HVAC units and
signage; a diesel generator on a belly tank; a meter and disconnect rack; the
ice bridge carrying the coax from shelter to tower; and a chain-link compound
fence with barbed wire and a padlocked double gate.

THE FAR SILHOUETTE. Structural members are 6-24 cm across. At 600 m that is
under a pixel, and a landmark that vanishes into shimmer at the distance it is
supposed to be navigated by is not a landmark. So the lattice has a second,
far representation: four textured trapezoids whose alpha-cut texture is the
same panel layout drawn at the same widths (rasterised below from the same
member list), which mipmaps down to the orange-and-white haze a real lattice
tower reads as from across a city. World swaps near for far by distance.

COORDINATES. Built in Blender's Z-up metres with the origin at the tower centre
on top of the pad. Cooked as engine Y-up with -Z north: (x, y, z) ->
(x, z, -y), a proper rotation, so triangle winding is preserved as authored.

THE PAD IS NOT HERE. The concrete platform the compound stands on is baked in
C++ from the sampled terrain (src/city/ferrone_mast.h), so it always meets the
real ground. PAD_* below mirrors kFerroneMastPad* there; the fence and the
shelter are laid out inside it.
"""
import bpy, bmesh, hashlib, json, math, shutil, struct, sys
from collections import defaultdict
from pathlib import Path
from mathutils import Matrix, Vector

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/models/props/ferrone_mast'
TEX = ROOT / 'assets/textures/world/ferrone_mast'
CHAIN_LINK = ROOT / 'assets/textures/world/halberd/chain-link.png'

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
PREVIEW = Path(argv[argv.index('--preview') + 1]) if '--preview' in argv else None
PREVIEW_NIGHT = '--night' in argv
PREVIEW_FAR = '--far' in argv

# ---------------------------------------------------------------------------
#  Dimensions. Blender coordinates: +X east, +Y north, +Z up.
# ---------------------------------------------------------------------------
LATTICE_TOP = 52.0      # top of the steel lattice
TAPER_TOP = 40.0        # the taper stops here; straight section above
BASE_HALF = 3.6         # leg centreline half-spacing at the pad
TOP_HALF = 0.9          # ...and from TAPER_TOP up
POLE_TOP = 58.8         # top of the antenna pole, under the beacon
STRUCTURE_TOP = 60.0    # lightning-rod tip == kLandmarks "Ferrone Mast".height_m
PIER_TOP = 0.35         # concrete pier cap above the pad
BANDS = 7               # aviation paint bands over the whole structure

# Pad and fence, mirroring kFerroneMastPadMin/Max in src/city/ferrone_mast.h
# (engine local x, z). In Blender y = -engine z.
PAD_X = (-7.0, 7.0)
MAST_WORLD = (599.0, -1798.0)   # kLandmarks "Ferrone Mast".pos; aims the dishes
PAD_Y = (-15.0, 7.0)
FENCE_INSET = 0.55


def half_width(z):
    return BASE_HALF + (TOP_HALF - BASE_HALF) * min(max(z, 0.0), TAPER_TOP) / TAPER_TOP


def band_of(z):
    return min(BANDS - 1, max(0, int(z / (STRUCTURE_TOP / BANDS))))


def paint(z):
    # Orange at the top and the bottom, as the marking standard requires.
    return 'paint_orange' if band_of(z) % 2 == 0 else 'paint_white'


# ---------------------------------------------------------------------------
#  Materials. uv_m is metres per texture repeat for the triplanar default;
#  emissive: 0 none, 1 steady red (night), 2 flashing red beacon, 3 warm lamp.
# ---------------------------------------------------------------------------
MATERIALS = {
    'paint_orange':  dict(color=(0.88, 0.33, 0.10, 1)),
    'paint_white':   dict(color=(0.88, 0.87, 0.84, 1)),
    'galvanized':    dict(color=(0.60, 0.62, 0.62, 1)),
    'dark_steel':    dict(color=(0.20, 0.21, 0.22, 1)),
    'grating':       dict(color=(0.30, 0.31, 0.31, 1)),
    'coax':          dict(color=(0.05, 0.05, 0.055, 1)),
    'antenna_white': dict(color=(0.85, 0.86, 0.84, 1)),
    'radome':        dict(color=(0.72, 0.74, 0.75, 1)),
    'rru_grey':      dict(color=(0.46, 0.48, 0.50, 1)),
    'concrete':      dict(color=(1, 1, 1, 1), texture='precast-concrete.png', uv_m=1.2),
    'pier':          dict(color=(0.58, 0.57, 0.54, 1)),
    'roof':          dict(color=(0.30, 0.31, 0.30, 1)),
    'door':          dict(color=(0.60, 0.56, 0.47, 1)),
    'hvac':          dict(color=(0.80, 0.78, 0.70, 1)),
    'louvre':        dict(color=(0.14, 0.14, 0.14, 1)),
    'genset':        dict(color=(0.20, 0.33, 0.24, 1)),
    'tank':          dict(color=(0.16, 0.16, 0.16, 1)),
    'copper':        dict(color=(0.70, 0.40, 0.20, 1)),
    'rubber':        dict(color=(0.07, 0.07, 0.07, 1)),
    'yellow':        dict(color=(0.86, 0.68, 0.10, 1)),
    'housing':       dict(color=(0.12, 0.12, 0.12, 1)),
    'beacon_top':    dict(color=(0.95, 0.10, 0.06, 1), emissive=2),
    'beacon_side':   dict(color=(0.95, 0.12, 0.07, 1), emissive=1),
    'lamp_warm':     dict(color=(1.00, 0.84, 0.58, 1), emissive=3),
    'meter_glass':   dict(color=(0.80, 0.88, 0.92, 0.35), glass=True),
    'chain_link':    dict(color=(1, 1, 1, 1), texture='chain-link.png', alpha=True),
    'sign_station':  dict(color=(1, 1, 1, 1), texture='station-sign.png'),
    'sign_danger':   dict(color=(1, 1, 1, 1), texture='danger-sign.png'),
    'sign_rf':       dict(color=(1, 1, 1, 1), texture='rf-caution.png'),
    'sign_asr':      dict(color=(1, 1, 1, 1), texture='asr-plate.png'),
    'far_lattice':   dict(color=(1, 1, 1, 1), texture='far-lattice.png', alpha=True),
    'glow':          dict(color=(1.0, 0.16, 0.08, 1)),
}

# Draw sets. The world gives each its own distance policy.
LOD_ALWAYS, LOD_NEAR, LOD_FAR, LOD_SITE = 0, 1, 2, 3

# ---------------------------------------------------------------------------
#  Geometry builder
# ---------------------------------------------------------------------------


class Part:
    """One Blender object: a bmesh plus the materials its faces use."""

    def __init__(self, name, lod):
        self.name, self.lod = name, lod
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.mats = []

    def mat(self, name):
        if name not in self.mats:
            self.mats.append(name)
        return self.mats.index(name)

    def face(self, pts, mat, smooth=False, uvs=None):
        verts = [self.bm.verts.new(p) for p in pts]
        f = self.bm.faces.new(verts)
        f.material_index = self.mat(mat)
        f.smooth = smooth
        f.normal_update()
        n = f.normal
        uvm = MATERIALS[mat].get('uv_m', 1.0)
        for i, loop in enumerate(f.loops):
            if uvs is not None:
                loop[self.uv].uv = uvs[i]
                continue
            c = loop.vert.co
            ax, ay, az = abs(n.x), abs(n.y), abs(n.z)
            if az >= ax and az >= ay:
                u, v = c.x, c.y
            elif ax >= ay:
                u, v = c.y, c.z
            else:
                u, v = c.x, c.z
            loop[self.uv].uv = (u / uvm, v / uvm)
        return f


PARTS = {}
COLLISION = []   # (centre Vector blender, half Vector blender-axes)
LIGHTS = []      # (Vector, kind)


def part(name, lod=LOD_NEAR):
    if name not in PARTS:
        PARTS[name] = Part(name, lod)
    return PARTS[name]


def collide(lo, hi):
    lo, hi = Vector(lo), Vector(hi)
    COLLISION.append(((lo + hi) * 0.5, (hi - lo) * 0.5))


def frame_for(axis):
    a = axis.normalized()
    ref = Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))
    u = a.cross(ref).normalized()
    v = a.cross(u).normalized()
    return u, v


def prism(p, mat, p0, p1, section, u=None, v=None, smooth=False, caps=True):
    """Extrude a 2D section (in the u, v frame) from p0 to p1."""
    p0, p1 = Vector(p0), Vector(p1)
    axis = p1 - p0
    if axis.length < 1e-5:
        return
    if u is None:
        u, v = frame_for(axis)
    # Keep the section counter-clockwise seen from +axis whatever frame the
    # caller handed in, so every side face comes out facing outward.
    area = sum(section[i][0] * section[(i + 1) % len(section)][1] -
               section[(i + 1) % len(section)][0] * section[i][1]
               for i in range(len(section)))
    if (u.cross(v).dot(axis) < 0) != (area < 0):
        section = list(reversed(section))
    bot = [p0 + u * s + v * t for s, t in section]
    top = [p1 + u * s + v * t for s, t in section]
    n = len(section)
    for i in range(n):
        j = (i + 1) % n
        p.face([bot[i], bot[j], top[j], top[i]], mat, smooth)
    if caps:
        p.face(top, mat)
        p.face(list(reversed(bot)), mat)


def circle(r, sides):
    return [(r * math.cos(2 * math.pi * i / sides), r * math.sin(2 * math.pi * i / sides))
            for i in range(sides)]


def tube(p, mat, p0, p1, r, sides=8, caps=True):
    prism(p, mat, p0, p1, circle(r, sides), smooth=True, caps=caps)


def box(p, mat, lo, hi):
    lo, hi = Vector(lo), Vector(hi)
    cx, cy = (lo.x + hi.x) * .5, (lo.y + hi.y) * .5
    hx, hy = (hi.x - lo.x) * .5, (hi.y - lo.y) * .5
    prism(p, mat, (cx, cy, lo.z), (cx, cy, hi.z),
          [(-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)],
          Vector((1, 0, 0)), Vector((0, 1, 0)))


def beam(p, mat, p0, p1, w, h, up=None):
    """A rectangular member from p0 to p1; `up` orients its h side."""
    p0, p1 = Vector(p0), Vector(p1)
    a = (p1 - p0).normalized()
    if up is None:
        u, v = frame_for(a)
    else:
        u = Vector(up).cross(a).normalized()
        v = a.cross(u).normalized()
    prism(p, mat, p0, p1, [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)], u, v)


def angle_iron(p, mat, p0, p1, out, leg=0.07, t=0.009):
    """An L-section member lying in a face whose outward normal is `out`."""
    p0, p1 = Vector(p0), Vector(p1)
    a = (p1 - p0).normalized()
    w = Vector(out).cross(a).normalized()
    inward = -Vector(out).normalized()
    prism(p, mat, p0, p1, [(-leg / 2, 0), (leg / 2, 0), (leg / 2, t), (-leg / 2, t)], w, inward)
    prism(p, mat, p0, p1, [(-leg / 2, 0), (-leg / 2 + t, 0), (-leg / 2 + t, leg), (-leg / 2, leg)], w, inward)


def lathe(p, mat, base, axis, profile, seg=24, smooth=True):
    """Revolve [(radius, height)] about `axis` from `base`."""
    base, axis = Vector(base), Vector(axis).normalized()
    u, v = frame_for(axis)
    rings = []
    for r, h in profile:
        c = base + axis * h
        rings.append([c + (u * math.cos(2 * math.pi * i / seg) + v * math.sin(2 * math.pi * i / seg)) * r
                      for i in range(seg)])
    for k in range(len(profile) - 1):
        a, b = rings[k], rings[k + 1]
        ra, rb = profile[k][0], profile[k + 1][0]
        for i in range(seg):
            j = (i + 1) % seg
            if ra < 1e-6:
                p.face([a[i], b[j], b[i]], mat, smooth)
            elif rb < 1e-6:
                p.face([a[i], a[j], b[i]], mat, smooth)
            else:
                p.face([a[i], a[j], b[j], b[i]], mat, smooth)
    return rings


def torus(p, mat, centre, axis, R, r, seg=20, sides=6, arc=2 * math.pi, start=0.0):
    centre, axis = Vector(centre), Vector(axis).normalized()
    u, v = frame_for(axis)
    closed = arc >= 2 * math.pi - 1e-6
    n = seg if closed else seg + 1
    rings = []
    for i in range(n):
        th = start + arc * i / seg
        radial = u * math.cos(th) + v * math.sin(th)
        c = centre + radial * R
        rings.append([c + (radial * math.cos(2 * math.pi * k / sides) + axis * math.sin(2 * math.pi * k / sides)) * r
                      for k in range(sides)])
    for i in range(seg):
        a, b = rings[i], rings[(i + 1) % n]
        for k in range(sides):
            m = (k + 1) % sides
            p.face([a[k], b[k], b[m], a[m]], mat, True)


def sphere(p, mat, centre, r, seg=12, rings=7):
    prof = [(r * math.sin(math.pi * i / rings), -r * math.cos(math.pi * i / rings)) for i in range(rings + 1)]
    prof[0] = (0.0, -r)
    prof[-1] = (0.0, r)
    lathe(p, mat, centre, (0, 0, 1), prof, seg)


def sign(p, mat, centre, normal, width, height, thick=0.012, frame_mat='galvanized'):
    """A flat plate facing `normal`, texture across the whole face."""
    c, n = Vector(centre), Vector(normal).normalized()
    right = Vector((0, 0, 1)).cross(n).normalized()
    up = n.cross(right).normalized()
    hw, hh = width / 2, height / 2
    # A backing plate thick enough that depth precision at range can never
    # let the one-sided face show through it mirrored from behind.
    thick = max(thick, 0.02)
    face = c + n * (thick / 2 + 0.004)
    p.face([face - right * hw - up * hh, face + right * hw - up * hh,
            face + right * hw + up * hh, face - right * hw + up * hh],
           mat, uvs=[(0, 0), (1, 0), (1, 1), (0, 1)])
    beam(p, frame_mat, c - up * hh, c + up * hh, width, thick, up=n)


# ---------------------------------------------------------------------------
#  The tower
# ---------------------------------------------------------------------------

def panel_levels():
    """Girt heights: panels roughly as tall as 0.72x the face is wide, so the
    X bracing stays near 55 degrees all the way up the taper."""
    levels, z = [0.0], 0.0
    while z < TAPER_TOP - 0.5:
        z += 1.44 * half_width(z)
        levels.append(z)
    scale = TAPER_TOP / levels[-1]
    levels = [l * scale for l in levels]
    z = TAPER_TOP
    while z < LATTICE_TOP - 1e-3:
        z += 2.0
        levels.append(min(z, LATTICE_TOP))
    return levels


LEVELS = panel_levels()
CORNERS = [(1, 1), (-1, 1), (-1, -1), (1, -1)]          # NE NW SW SE
FACES = [((1, 1), (1, -1), Vector((1, 0, 0))),          # east
         ((-1, 1), (1, 1), Vector((0, 1, 0))),          # north
         ((-1, -1), (-1, 1), Vector((-1, 0, 0))),       # west
         ((1, -1), (-1, -1), Vector((0, -1, 0)))]       # south
FAR_MEMBERS = []   # (u0, z0, u1, z1, width_m, colour) on one face, for the far texture


def leg_point(corner, z):
    h = half_width(z)
    return Vector((corner[0] * h, corner[1] * h, z))


def face_point(face, u, z):
    """u in [0,1] across a face from its first corner to its second."""
    a, b, _ = face
    return leg_point(a, z).lerp(leg_point(b, z), u)


def leg_radius(z):
    return 0.115 - 0.05 * z / LATTICE_TOP


def build_legs():
    breaks = sorted(set(LEVELS + [k * STRUCTURE_TOP / BANDS for k in range(1, BANDS)
                                  if k * STRUCTURE_TOP / BANDS < LATTICE_TOP]))
    legs = part('tower legs')
    for corner in CORNERS:
        for z0, z1 in zip(breaks, breaks[1:]):
            lo = max(z0, PIER_TOP + 0.04)
            if z1 <= lo:
                continue
            tube(legs, paint((lo + z1) * .5), leg_point(corner, lo), leg_point(corner, z1),
                 leg_radius(z0), sides=10, caps=False)
        # Bolted splice flanges at every girt.
        for z in LEVELS[1:-1]:
            tube(legs, paint(z), leg_point(corner, z - 0.05), leg_point(corner, z + 0.05),
                 leg_radius(z) + 0.045, sides=10)
        # Legs as collision: one vertical box per panel around the tube.
        for z0, z1 in zip(LEVELS, LEVELS[1:]):
            a, b = leg_point(corner, max(z0, PIER_TOP)), leg_point(corner, z1)
            r = leg_radius(z0) + 0.02
            collide((min(a.x, b.x) - r, min(a.y, b.y) - r, a.z),
                    (max(a.x, b.x) + r, max(a.y, b.y) + r, b.z))
        # Foundation: pier, grout pad, base plate, anchor bolts with nuts,
        # and the copper ground strap into the pad.
        c = leg_point(corner, 0)
        base = part('tower base', LOD_SITE)
        prism(base, 'pier', (c.x, c.y, -0.3), (c.x, c.y, PIER_TOP), circle(0.55, 16), smooth=True)
        box(base, 'pier', (c.x - .4, c.y - .4, PIER_TOP), (c.x + .4, c.y + .4, PIER_TOP + .025))
        box(base, 'galvanized', (c.x - .32, c.y - .32, PIER_TOP + .025), (c.x + .32, c.y + .32, PIER_TOP + .065))
        for sx in (-1, 1):
            for sy in (-1, 1):
                b = Vector((c.x + sx * .24, c.y + sy * .24, PIER_TOP + .065))
                tube(base, 'galvanized', b, b + Vector((0, 0, .09)), 0.018, 6)
                prism(base, 'galvanized', b + Vector((0, 0, .01)), b + Vector((0, 0, .045)), circle(.034, 6))
        for k in range(4):   # gusset plates welded leg-to-plate
            ang = math.pi / 4 + k * math.pi / 2
            d = Vector((math.cos(ang), math.sin(ang), 0))
            p0 = Vector((c.x, c.y, PIER_TOP + .065)) + d * 0.1
            beam(base, paint(0.2), p0 + Vector((0, 0, .16)), p0 + d * 0.16 + Vector((0, 0, .02)),
                 .01, .12, up=d.cross(Vector((0, 0, 1))))
        out = Vector((c.x, c.y, 0)).normalized()
        g0 = Vector((c.x, c.y, PIER_TOP + 0.3)) + out * 0.12
        tube(base, 'copper', g0, g0 + out * 0.35 - Vector((0, 0, 0.62)), 0.01, 5)
        collide((c.x - .55, c.y - .55, -0.3), (c.x + .55, c.y + .55, PIER_TOP + .065))


def build_bracing():
    br = part('tower bracing')
    for face in FACES:
        a, b, out = face
        for i, (z0, z1) in enumerate(zip(LEVELS, LEVELS[1:])):
            zlo = max(z0, PIER_TOP + 0.25)
            h = z1 - z0
            size = 0.085 if z0 < 20 else (0.07 if z0 < TAPER_TOP else 0.06)
            p00, p10 = face_point(face, 0.03, zlo), face_point(face, 0.97, zlo)
            p01, p11 = face_point(face, 0.03, z1), face_point(face, 0.97, z1)
            mid = (p00 + p11) * 0.5
            angle_iron(br, paint(mid.z), p00, p11, out, size)
            angle_iron(br, paint(mid.z), p10, p01, out, size)
            # The girt at the top of the panel.
            angle_iron(br, paint(z1), face_point(face, 0.0, z1), face_point(face, 1.0, z1), out, size)
            if i == 0:
                angle_iron(br, paint(zlo), face_point(face, 0.0, zlo), face_point(face, 1.0, zlo), out, size)
            # Crossing gusset plate, bolted.
            cross = (face_point(face, 0.5, (zlo + z1) * .5))
            beam(br, paint(cross.z), cross - Vector((0, 0, .09)), cross + Vector((0, 0, .09)),
                 0.2, 0.012, up=out)
            FAR_MEMBERS.append((0.03, zlo, 0.97, z1, size, paint(mid.z)))
            FAR_MEMBERS.append((0.97, zlo, 0.03, z1, size, paint(mid.z)))
            FAR_MEMBERS.append((0.0, z1, 1.0, z1, size, paint(z1)))
            # Tall lower panels get redundant members: a horizontal through
            # the crossing and short diagonals halving the long X arms.
            if h > 3.2:
                zm = (zlo + z1) * .5
                angle_iron(br, paint(zm), face_point(face, 0.0, zm), face_point(face, 1.0, zm), out, 0.055)
                FAR_MEMBERS.append((0.0, zm, 1.0, zm, 0.055, paint(zm)))
                for u0, u1 in ((0.0, 0.25), (1.0, 0.75)):
                    angle_iron(br, paint(zm), face_point(face, u0, (zlo + zm) * .5),
                               face_point(face, u1, (zlo + zm) * .5 + (zm - zlo) * .25), out, 0.05)
    # Plan bracing: a horizontal diagonal pair every other girt in the taper,
    # which is what stops a square lattice racking into a rhombus.
    for k, z in enumerate(LEVELS[1:-1]):
        if k % 2 == 0 or z >= TAPER_TOP:
            ne, sw = leg_point((1, 1), z), leg_point((-1, -1), z)
            nw, se = leg_point((-1, 1), z), leg_point((1, -1), z)
            beam(br, paint(z), ne, sw, 0.06, 0.06, up=(0, 0, 1))
            beam(br, paint(z), nw, se, 0.06, 0.06, up=(0, 0, 1))
    # Structural core as collision, one box per panel, inside the silhouette.
    for z0, z1 in zip(LEVELS, LEVELS[1:]):
        h = half_width(z1) * 0.92
        collide((-h, -h, max(z0, PIER_TOP)), (h, h, z1))


LADDER_X, LADDER_Y = -0.28, -0.52
CABLE_X = 0.30


def build_ladder():
    lad = part('tower ladder')
    for sx in (-0.225, 0.225):
        beam(lad, 'galvanized', (LADDER_X + sx, LADDER_Y, 0.4), (LADDER_X + sx, LADDER_Y, LATTICE_TOP + 1.1),
             0.012, 0.065, up=(0, 1, 0))
    z = 0.7
    while z < LATTICE_TOP + 1.0:
        tube(lad, 'galvanized', (LADDER_X - 0.225, LADDER_Y, z), (LADDER_X + 0.225, LADDER_Y, z), 0.014, 6, False)
        z += 0.3
    # Fall-arrest safety cable and its guides.
    tube(lad, 'dark_steel', (LADDER_X, LADDER_Y + 0.05, 0.6), (LADDER_X, LADDER_Y + 0.05, LATTICE_TOP + 1.1), 0.005, 4)
    for z in LEVELS[1:]:
        if z < 1.0:
            continue
        # Standoff arms to the plan bracing and a cable guide at every girt.
        beam(lad, 'galvanized', (LADDER_X, LADDER_Y - 0.03, z), (LADDER_X, 0.0, z), 0.04, 0.04, up=(0, 0, 1))
        box(lad, 'dark_steel', (LADDER_X - .03, LADDER_Y + .03, z - .04), (LADDER_X + .03, LADDER_Y + .07, z + .04))
    # Anti-climb guard: a hinged sheet cage around the bottom 3 m, padlocked.
    g = part('tower base', LOD_SITE)
    for dx in (-0.36, 0.36):
        box(g, 'galvanized', (LADDER_X + dx - .004, LADDER_Y - .42, 0.4), (LADDER_X + dx + .004, LADDER_Y + .02, 3.4))
    box(g, 'galvanized', (LADDER_X - .36, LADDER_Y - .43, 0.4), (LADDER_X + .36, LADDER_Y - .42, 3.4))
    box(g, 'dark_steel', (LADDER_X + .30, LADDER_Y - .47, 1.45), (LADDER_X + .34, LADDER_Y - .43, 1.55))
    sign(g, 'sign_danger', (LADDER_X, LADDER_Y - .44, 2.3), (0, -1, 0), 0.48, 0.36, 0.006)

    # Cable ladder beside it: rails, rungs, and the coax bundle riding up it.
    cab = part('tower cables')
    for sx in (-0.17, 0.17):
        beam(cab, 'galvanized', (CABLE_X + sx, LADDER_Y, 2.4), (CABLE_X + sx, LADDER_Y, 44.0), 0.01, 0.05, up=(0, 1, 0))
    z = 2.6
    while z < 44.0:
        beam(cab, 'galvanized', (CABLE_X - .17, LADDER_Y, z), (CABLE_X + .17, LADDER_Y, z), .03, .012, up=(0, 1, 0))
        z += 0.9
    lines = [(-0.12, 0.030, 44.0), (-0.06, 0.030, 44.0), (0.0, 0.030, 44.0),
             (0.06, 0.020, 34.2), (0.11, 0.020, 26.0), (0.14, 0.018, 30.8)]
    for dx, r, top in lines:
        x = CABLE_X + dx
        tube(cab, 'coax', (x, LADDER_Y + 0.05, 2.55), (x, LADDER_Y + 0.05, top), r, 6)
        # The bend out of the tower toward the ice bridge.
        tube(cab, 'coax', (x, LADDER_Y + 0.05, 2.55), (x, -half_width(2.55) - 0.1, 2.55), r, 6)
    # The main FM line: 3-1/8" rigid coax up the north face to the bays.
    feed = part('tower cables')
    fx = 0.18
    tube(feed, 'galvanized', (fx, LADDER_Y + 0.12, 2.55), (fx, LADDER_Y + 0.12, 38.0), 0.045, 8)
    tube(feed, 'galvanized', (fx, LADDER_Y + 0.12, 38.0), (fx, TOP_HALF - 0.12, 39.2), 0.045, 8)
    tube(feed, 'galvanized', (fx, TOP_HALF - 0.12, 39.2), (fx, TOP_HALF - 0.12, 50.4), 0.045, 8)
    for z in np.arange(4.0, 38.0, 2.0):
        tube(feed, 'galvanized', (fx, LADDER_Y + 0.12, z - 0.04), (fx, LADDER_Y + 0.12, z + 0.04), 0.06, 8)
    tube(feed, 'coax', (fx, LADDER_Y + 0.12, 2.55), (fx, -half_width(2.55) - 0.1, 2.55), 0.045, 8)


def grating_deck(p, x0, y0, x1, y1, z, hole=None):
    """A bar-grating deck with bearing bars drawn as ribs, optionally with a
    ladder hatch cut out of it."""
    rects = [(x0, y0, x1, y1)]
    if hole:
        hx0, hy0, hx1, hy1 = hole
        rects = [(x0, y0, x1, hy0), (x0, hy1, x1, y1), (x0, hy0, hx0, hy1), (hx1, hy0, x1, hy1)]
    for a, b, c, d in rects:
        if c - a > 0.01 and d - b > 0.01:
            box(p, 'grating', (a, b, z - 0.03), (c, d, z))
    x = x0 + 0.3
    while x < x1:
        box(p, 'dark_steel', (x - .01, y0, z - .09), (x + .01, y1, z - .03))
        x += 0.6


def railing(p, pts, z, closed=True):
    n = len(pts)
    for i in range(n if closed else n - 1):
        a, b = Vector((*pts[i], z)), Vector((*pts[(i + 1) % n], z))
        tube(p, 'galvanized', a + Vector((0, 0, 1.07)), b + Vector((0, 0, 1.07)), 0.022, 6)
        tube(p, 'galvanized', a + Vector((0, 0, 0.55)), b + Vector((0, 0, 0.55)), 0.018, 6)
        d = (b - a)
        beam(p, 'galvanized', a + Vector((0, 0, 0.05)), b + Vector((0, 0, 0.05)), 0.006, 0.1,
             up=d.cross(Vector((0, 0, 1))))
        steps = max(1, int(d.length / 1.2))
        for k in range(steps + (0 if closed else 1)):
            q = a.lerp(b, k / steps)
            tube(p, 'galvanized', q, q + Vector((0, 0, 1.09)), 0.024, 6)


def build_platforms():
    p = part('tower platforms')
    # Rest platform at 30 m: the full square, with the ladder hatch.
    zr = 30.0
    h = half_width(zr) - 0.08
    grating_deck(p, -h, -h, h, h, zr,
                 hole=(LADDER_X - .4, LADDER_Y - .45, LADDER_X + .4, LADDER_Y + .35))
    # Work platform at the top of the lattice, cantilevered on outriggers.
    zt = LATTICE_TOP + 0.02
    s = 1.6
    for k in range(4):
        c = leg_point(CORNERS[k], LATTICE_TOP)
        out = Vector((CORNERS[k][0] * s, CORNERS[k][1] * s, LATTICE_TOP - 0.1))
        beam(p, 'dark_steel', Vector((c.x, c.y, LATTICE_TOP - 0.1)), out, 0.1, 0.12, up=(0, 0, 1))
        brace0 = leg_point(CORNERS[k], LATTICE_TOP - 1.4)
        beam(p, 'dark_steel', brace0, out, 0.06, 0.06)
    grating_deck(p, -s, -s, s, s, zt,
                 hole=(LADDER_X - .4, LADDER_Y - .45, LADDER_X + .4, LADDER_Y + .35))
    railing(p, [(-s, -s), (s, -s), (s, s), (-s, s)], zt)
    collide((-s, -s, zt - 0.12), (s, s, zt))


def build_pole_and_beacon():
    p = part('tower pole', LOD_ALWAYS)
    z0 = LATTICE_TOP - 1.0
    breaks = [z0] + [k * STRUCTURE_TOP / BANDS for k in range(BANDS) if z0 < k * STRUCTURE_TOP / BANDS < POLE_TOP] + [POLE_TOP]
    for a, b in zip(breaks, breaks[1:]):
        r = 0.17 if b <= 55.5 else 0.12
        tube(p, paint((a + b) * .5), (0, 0, a), (0, 0, b), r, 12, caps=True)
    tube(p, paint(55.5), (0, 0, 55.4), (0, 0, 55.6), 0.21, 12)
    collide((-.2, -.2, z0), (.2, .2, POLE_TOP))
    # L-864 beacon: housing, red lens dome, cap, and the lightning rod.
    tube(p, 'housing', (0, 0, POLE_TOP), (0, 0, POLE_TOP + 0.18), 0.19, 16)
    lathe(p, 'beacon_top', (0, 0, POLE_TOP + 0.18),
          (0, 0, 1), [(0.16, 0), (0.17, 0.08), (0.165, 0.2), (0.14, 0.3), (0.08, 0.36), (0.0, 0.38)], 18)
    tube(p, 'galvanized', (0, 0, POLE_TOP + 0.52), (0, 0, STRUCTURE_TOP - 0.06), 0.014, 6)
    lathe(p, 'galvanized', (0, 0, STRUCTURE_TOP - 0.06), (0, 0, 1), [(0.014, 0), (0.0, 0.06)], 6)
    tube(p, 'galvanized', (0, 0, POLE_TOP + 0.3), (0, 0, POLE_TOP + 0.56), 0.012, 6)
    LIGHTS.append((Vector((0, 0, POLE_TOP + 0.36)), 2))
    # Beacon conduit down the pole.
    tube(p, 'dark_steel', (0.2, 0, z0), (0.2, 0, POLE_TOP), 0.016, 5)


def side_light(p, corner, z):
    c = leg_point(corner, z)
    out = Vector((corner[0], corner[1], 0)).normalized()
    base = c + out * (leg_radius(z) + 0.02)
    beam(p, 'dark_steel', base, base + out * 0.22, 0.05, 0.04, up=(0, 0, 1))
    q = base + out * 0.26
    tube(p, 'housing', q - Vector((0, 0, 0.06)), q, 0.07, 10)
    lathe(p, 'beacon_side', q, (0, 0, 1), [(0.065, 0), (0.066, 0.05), (0.05, 0.1), (0.0, 0.12)], 12)
    LIGHTS.append((q + Vector((0, 0, 0.06)), 1))


def build_side_lights():
    p = part('tower lights', LOD_ALWAYS)
    for corner in CORNERS:
        side_light(p, corner, 29.0)


def fm_bay(p, face_y, z):
    """A side-mounted circularly polarised FM bay: standoff arm, feed tee and
    the two bent half-wave dipole arms that make it look like a broken ring."""
    base = Vector((0.18, face_y, z))
    arm_end = base + Vector((0, 0.95, 0))
    tube(p, 'galvanized', base, arm_end, 0.03, 8)
    tube(p, 'galvanized', arm_end - Vector((0, 0, 0.35)), arm_end + Vector((0, 0, 0.35)), 0.045, 8)
    centre = arm_end + Vector((0, 0.05, 0))
    for side in (-1, 1):
        tilt = Matrix.Rotation(math.radians(22) * side, 3, 'Y')
        axis = tilt @ Vector((0, 0, 1))
        torus(p, 'galvanized', centre, axis, 0.42, 0.022, seg=18, sides=6,
              arc=math.radians(150), start=math.radians(105 if side > 0 else -75))
    # The branch feed from the rigid line on the face.
    tube(p, 'galvanized', Vector((0.18, face_y - 0.1, z - 0.4)), base + Vector((0, 0.3, 0)), 0.02, 6)


def build_fm():
    p = part('tower antennas')
    y = TOP_HALF + 0.02
    for k in range(4):
        fm_bay(p, y, 41.2 + 3.08 * k)
    # Mount rails the bays clamp to.
    for dx in (0.05, 0.31):
        tube(p, 'galvanized', (dx, y, 40.6), (dx, y, 51.2), 0.03, 6)


def panel_antenna(p, centre, facing, tilt_deg=3.0):
    f = Vector(facing).normalized()
    right = Vector((0, 0, 1)).cross(f).normalized()
    up = Matrix.Rotation(math.radians(tilt_deg), 3, right) @ Vector((0, 0, 1))
    c = Vector(centre)
    hw, hh, hd = 0.15, 0.95, 0.06
    sec = [(-hw, -hd), (hw, -hd), (hw, hd), (-hw, hd)]
    prism(p, 'antenna_white', c - up * hh, c + up * hh, sec, right, f)
    # Rounded end caps and the connector boots on the bottom.
    for s in (-1, 1):
        prism(p, 'antenna_white', c + up * (s * hh), c + up * (s * (hh + 0.03)),
              [(-hw * .9, -hd * .9), (hw * .9, -hd * .9), (hw * .9, hd * .9), (-hw * .9, hd * .9)], right, f)
    for dx in (-0.06, 0.06):
        q = c - up * (hh + 0.03) + right * dx
        tube(p, 'rubber', q, q - up * 0.08, 0.018, 6)


def build_cell_sectors():
    p = part('tower antennas')
    z = 35.4
    h = half_width(z)
    for facing in (Vector((1, 0, 0)), Vector((0, -1, 0)), Vector((-1, 0, 0))):
        side = Vector((0, 0, 1)).cross(facing).normalized()
        face_c = facing * h
        standoff = face_c + facing * 0.9
        for s in (-0.5, 0.5):
            tube(p, 'galvanized', face_c + side * s * 1.2 + Vector((0, 0, z)),
                 standoff + side * s * 1.2 + Vector((0, 0, z)), 0.035, 8)
        for dz in (-0.7, 0.7):
            tube(p, 'galvanized', standoff - side * 1.3 + Vector((0, 0, z + dz)),
                 standoff + side * 1.3 + Vector((0, 0, z + dz)), 0.04, 8)
        for k, s in enumerate((-0.95, 0.0, 0.95)):
            pipe = standoff + side * s
            tube(p, 'galvanized', pipe + Vector((0, 0, z - 1.3)), pipe + Vector((0, 0, z + 1.3)), 0.036, 8)
            panel_antenna(p, pipe + facing * 0.14 + Vector((0, 0, z + 0.15)), facing, 2.0 + k)
            # Remote radio unit behind each panel, with its jumper.
            rru = pipe - facing * 0.2 + Vector((0, 0, z - 0.75))
            prism(p, 'rru_grey', rru - Vector((0, 0, 0.26)), rru + Vector((0, 0, 0.26)),
                  [(-0.17, -0.07), (0.17, -0.07), (0.17, 0.07), (-0.17, 0.07)], side, facing)
            for fin in np.linspace(-0.14, 0.14, 7):
                q = rru - facing * 0.08 + side * fin
                beam(p, 'rru_grey', q - Vector((0, 0, 0.24)), q + Vector((0, 0, 0.24)), 0.008, 0.03, up=facing)
            tube(p, 'coax', rru + Vector((0, 0, 0.26)), pipe + facing * 0.14 + Vector((0, 0, z - 0.85)), 0.012, 5)
            tube(p, 'coax', rru - Vector((0, 0, 0.26)), Vector((CABLE_X, LADDER_Y, z - 1.4)), 0.014, 5)


def dish(p, mount_corner, z, target_xy, diameter):
    c = leg_point(mount_corner, z)
    out = Vector((mount_corner[0], mount_corner[1], 0)).normalized()
    pipe = c + out * 0.5
    tube(p, 'galvanized', pipe - Vector((0, 0, 1.1)), pipe + Vector((0, 0, 1.1)), 0.05, 10)
    for dz in (-0.8, 0.8):
        tube(p, 'galvanized', c + Vector((0, 0, dz)), pipe + Vector((0, 0, dz)), 0.035, 8)
        tube(p, 'galvanized', pipe + Vector((0, 0, dz - 0.07)), pipe + Vector((0, 0, dz + 0.07)), 0.075, 10)
    aim = Vector((target_xy[0], target_xy[1], 0)).normalized()
    r = diameter / 2
    centre = pipe + aim * 0.45
    # Reflector (a shallow paraboloid, focal ratio ~0.35), shroud, radome.
    depth = r * r / (4 * 0.35 * diameter)
    prof = [(r * i / 8, depth * (i / 8) ** 2) for i in range(9)]
    prof[0] = (0.0, 0.0)
    lathe(p, 'radome', centre - aim * depth, aim, prof, 28)
    lathe(p, 'radome', centre - aim * depth, aim,
                  [(r, depth), (r * 1.02, depth), (r * 1.02, depth + r * 0.45), (r * 0.99, depth + r * 0.45)], 28, smooth=True)
    lathe(p, 'antenna_white', centre + aim * (r * 0.45 - 0.001), aim,
          [(r * 0.99, 0.0), (r * 0.8, 0.05), (r * 0.4, 0.085), (0.0, 0.095)], 28)
    # Back of the reflector, closed, so it reads solid from behind.
    lathe(p, 'radome', centre - aim * depth, -aim, [(0.0, -0.001), (r * 0.5, 0.0), (r, -depth + 0.01)], 28)
    # Outdoor unit on the back, and the struts that hold the aim.
    odu = centre - aim * (depth + 0.18)
    side = Vector((0, 0, 1)).cross(aim).normalized()
    prism(p, 'rru_grey', odu - aim * 0.12, odu + aim * 0.12,
          [(-0.14, -0.14), (0.14, -0.14), (0.14, 0.14), (-0.14, 0.14)], side, Vector((0, 0, 1)))
    tube(p, 'galvanized', odu - aim * 0.12, pipe, 0.03, 6)
    for s in (-1, 1):
        tube(p, 'galvanized', centre + side * s * r * 0.9 - aim * depth * 0.5, pipe + Vector((0, 0, 0.6 * s)), 0.014, 5)
    tube(p, 'coax', odu - Vector((0, 0, 0.14)), Vector((CABLE_X + 0.1, LADDER_Y, z - 0.5)), 0.016, 5)


def build_dishes():
    p = part('tower antennas')
    # Engine-to-Blender for targets: world dx stays x, world dz becomes -y.
    # From the mast: downtown Trinity Tower (60, -60), the Kepler flare
    # (-520, -1800), and Nickel Heights' water tower (1150, 300).
    mx, mz = MAST_WORLD
    dish(p, (-1, -1), 24.0, (60 - mx, -(-60 - mz)), 1.8)
    dish(p, (-1, 1), 31.0, (-520 - mx, -(-1800 - mz)), 1.2)
    dish(p, (1, -1), 27.0, (1150 - mx, -(300 - mz)), 0.9)


# ---------------------------------------------------------------------------
#  The compound
# ---------------------------------------------------------------------------
SHELTER = (-1.0, -13.0, 5.0, -9.4)   # x0, y0, x1, y1
SHELTER_H = 3.0


def build_shelter():
    p = part('compound shelter', LOD_SITE)
    x0, y0, x1, y1 = SHELTER
    # Precast shell on a grade beam, a lipped roof slab with a drip edge.
    box(p, 'pier', (x0 - .08, y0 - .08, 0), (x1 + .08, y1 + .08, 0.18))
    box(p, 'concrete', (x0, y0, 0.18), (x1, y1, SHELTER_H))
    box(p, 'concrete', (x0 - .15, y0 - .15, SHELTER_H), (x1 + .15, y1 + .15, SHELTER_H + 0.2))
    box(p, 'roof', (x0 - .17, y0 - .17, SHELTER_H + .2), (x1 + .17, y1 + .17, SHELTER_H + .24))
    box(p, 'roof', (x0 - .1, y0 - .1, SHELTER_H + .24), (x1 + .1, y1 + .1, SHELTER_H + .33))
    collide((x0, y0, 0), (x1, y1, SHELTER_H + .33))
    # West door: leaf, frame, lever, kick plate, closer, rain hood, lamp.
    dy = -11.2
    box(p, 'dark_steel', (x0 - .03, dy - .56, 0.18), (x0 + .02, dy + .56, 2.36))
    box(p, 'door', (x0 - .05, dy - .48, 0.2), (x0 + .0, dy + .48, 2.3))
    box(p, 'galvanized', (x0 - .06, dy - .46, 0.22), (x0 - .045, dy + .46, 0.5))
    box(p, 'galvanized', (x0 - .11, dy + .30, 1.0), (x0 - .05, dy + .34, 1.06))
    box(p, 'galvanized', (x0 - .12, dy + .22, 1.02), (x0 - .10, dy + .34, 1.04))
    box(p, 'dark_steel', (x0 - .1, dy - .42, 2.14), (x0 - .04, dy - .08, 2.22))
    beam(p, 'dark_steel', (x0 - .5, dy - .7, 2.62), (x0, dy - .7, 2.72), 0.02, 0.06, up=(0, 1, 0))
    beam(p, 'dark_steel', (x0 - .5, dy + .7, 2.62), (x0, dy + .7, 2.72), 0.02, 0.06, up=(0, 1, 0))
    prism(p, 'roof', (x0 - .55, dy - .75, 2.62), (x0 - .55, dy + .75, 2.62),
          [(0, 0), (0.55, 0.1), (0.55, 0.14), (0, 0.04)], Vector((1, 0, 0)), Vector((0, 0, 1)))
    box(p, 'housing', (x0 - .18, dy - .12, 2.78), (x0, dy + .12, 2.98))
    box(p, 'lamp_warm', (x0 - .19, dy - .1, 2.8), (x0 - .17, dy + .1, 2.9))
    LIGHTS.append((Vector((x0 - .3, dy, 2.8)), 3))
    sign(p, 'sign_danger', (x0 - .015, dy - 1.05, 1.6), (-1, 0, 0), 0.5, 0.375, 0.006)
    # Station ID on the south wall, the face you see from the city side.
    sign(p, 'sign_station', ((x0 + x1) / 2, y0 - .02, 2.05), (0, -1, 0), 3.2, 1.0, 0.03, 'dark_steel')
    # Twin wall-mount HVAC units on the east wall: cabinet, intake louvres,
    # condenser grille, and the service disconnects.
    for cy in (-10.25, -12.15):
        box(p, 'hvac', (x1, cy - .5, 0.45), (x1 + .55, cy + .5, 2.4))
        box(p, 'hvac', (x1 - .01, cy - .54, 2.38), (x1 + .6, cy + .54, 2.46))
        for k in range(9):
            zz = 0.62 + k * 0.09
            beam(p, 'louvre', (x1 + .555, cy - .4, zz), (x1 + .555, cy + .4, zz), .012, .05, up=(1, 0, 0))
        box(p, 'louvre', (x1 + .55, cy - .42, 1.5), (x1 + .558, cy + .42, 2.25))
        for k in range(10):
            yy = cy - .38 + k * .084
            box(p, 'hvac', (x1 + .553, yy - .004, 1.52), (x1 + .562, yy + .004, 2.23))
        box(p, 'dark_steel', (x1 + .12, cy + .52, 1.1), (x1 + .32, cy + .64, 1.45))
        collide((x1, cy - .5, 0.45), (x1 + .55, cy + .5, 2.46))
    # Cable entry port on the north wall where the ice bridge lands.
    box(p, 'dark_steel', (1.1, y1, 2.1), (2.0, y1 + .06, 2.75))
    for k in range(6):
        q = Vector((1.22 + k * .13, y1 + .06, 2.42))
        tube(p, 'rubber', q, q + Vector((0, .08, 0)), 0.035, 8)
    # Exhaust vent and the generator plug-in box.
    box(p, 'louvre', (3.4, y1, 2.2), (3.9, y1 + .04, 2.6))
    box(p, 'dark_steel', (x1, -9.8, 0.9), (x1 + .12, -9.6, 1.25))
    # Roof-edge floodlight covering the pad.
    box(p, 'housing', (x1 - .2, y1 - .05, SHELTER_H + .33), (x1, y1 + .18, SHELTER_H + .55))
    # Ground bar and the halo ground wire around the shell.
    box(p, 'copper', (x0 + .3, y1 + .01, 0.35), (x0 + .9, y1 + .04, 0.42))
    for (a, b) in (((x0 - .05, y0 - .05), (x1 + .05, y0 - .05)), ((x1 + .05, y0 - .05), (x1 + .05, y1 + .05)),
                   ((x1 + .05, y1 + .05), (x0 - .05, y1 + .05)), ((x0 - .05, y1 + .05), (x0 - .05, y0 - .05))):
        tube(p, 'copper', (*a, 2.95), (*b, 2.95), 0.006, 4)


def build_ice_bridge():
    p = part('compound bridge', LOD_SITE)
    y_start, y_end = SHELTER[3] + 0.08, -half_width(2.6) - 0.05
    for y in (-7.9, -5.6):
        for x in (1.05, 2.05):
            tube(p, 'galvanized', (x, y, 0), (x, y, 2.95), 0.045, 8)
            prism(p, 'pier', (x, y, 0), (x, y, 0.12), circle(0.14, 10), smooth=True)
            collide((x - .05, y - .05, 0), (x + .05, y + .05, 2.95))
        beam(p, 'galvanized', (1.0, y, 2.45), (2.1, y, 2.45), .045, .045, up=(0, 1, 0))
        beam(p, 'galvanized', (1.0, y, 2.95), (2.1, y, 2.95), .045, .045, up=(0, 1, 0))
    # Waveguide ladder, the coax riding it, and the grating canopy over it
    # that stops ice off the tower cutting the lines.
    for x in (1.15, 1.95):
        beam(p, 'galvanized', (x, y_start, 2.5), (x, y_end, 2.5), 0.01, 0.06, up=(1, 0, 0))
    y = y_start - 0.1
    while y > y_end:
        beam(p, 'galvanized', (1.15, y, 2.5), (1.95, y, 2.5), 0.025, 0.012, up=(0, 1, 0))
        y -= 0.45
    for k in range(6):
        x = 1.22 + k * .13
        r = 0.03 if k < 4 else 0.02
        tube(p, 'coax', (x, y_start + 0.1, 2.53 + r), (x, y_end, 2.53 + r), r, 6)
    box(p, 'grating', (0.95, y_end, 2.96), (2.15, y_start, 3.0))
    # The last metre: from the bridge's end to where the lines turn up.
    for k in range(6):
        x = 1.22 + k * .13
        tube(p, 'coax', (x, y_end, 2.56), (CABLE_X + (x - 1.55) * 0.5, -half_width(2.55) - 0.1, 2.55), 0.028, 6)


def build_generator():
    p = part('compound generator', LOD_SITE)
    x0, x1, y0, y1 = -6.05, -4.75, -7.8, -4.4
    box(p, 'pier', (x0 - .25, y0 - .25, 0), (x1 + .25, y1 + .25, 0.15))
    box(p, 'tank', (x0, y0, 0.15), (x1, y1, 0.45))
    box(p, 'genset', (x0 + .02, y0 + .02, 0.45), (x1 - .02, y1 - .02, 1.85))
    box(p, 'genset', (x0 - .01, y0 - .01, 1.85), (x1 + .01, y1 + .01, 1.92))
    collide((x0, y0, 0), (x1, y1, 1.92))
    for side, xx in ((-1, x0 + 0.015), (1, x1 - 0.015)):
        for k in range(12):
            y = y0 + 0.3 + k * 0.1
            beam(p, 'louvre', (xx + side * 0.005, y, 0.95), (xx + side * 0.005, y, 1.6), .01, .05, up=(1, 0, 0))
        box(p, 'dark_steel', (xx + side * .006 - .004, y1 - 1.35, 0.6), (xx + side * .006 + .004, y1 - .45, 1.75))
        box(p, 'galvanized', (xx + side * .02 - .01, y1 - .55, 1.15), (xx + side * .02 + .01, y1 - .5, 1.3))
    # Exhaust stack with a rain cap, lifting eyes, fill cap and vent.
    ex = Vector((x0 + 0.35, y0 + 0.45, 1.92))
    tube(p, 'dark_steel', ex, ex + Vector((0, 0, 0.55)), 0.055, 10)
    beam(p, 'dark_steel', ex + Vector((-.07, 0, .57)), ex + Vector((.07, 0, .6)), .14, .005, up=(0, 1, 0))
    for y in (y0 + .2, y1 - .2):
        torus(p, 'galvanized', (x0 + 0.65, y, 1.97), (1, 0, 0), 0.05, 0.012, 10, 5)
    tube(p, 'yellow', (x1 - .15, y0 + .2, 0.45), (x1 - .15, y0 + .2, 0.55), 0.045, 10)
    tube(p, 'galvanized', (x1 - .15, y0 + .5, 0.45), (x1 - .15, y0 + .5, 0.9), 0.012, 6)
    # Conduit to the shelter along the pad.
    tube(p, 'dark_steel', (x1, y1 - 0.3, 0.3), (-1.0, y1 - 0.3, 0.3), 0.03, 6)
    tube(p, 'dark_steel', (-1.0, y1 - 0.3, 0.3), (-1.0, SHELTER[3], 0.3), 0.03, 6)


def build_meter_rack():
    p = part('compound utility', LOD_SITE)
    x0, x1, y = 5.1, 6.05, -7.6
    for x in (x0, x1):
        beam(p, 'galvanized', (x, y, 0), (x, y, 2.1), .042, .042, up=(0, 1, 0))
    for z in (0.7, 1.75):
        beam(p, 'galvanized', (x0 - .05, y, z), (x1 + .05, y, z), .042, .042, up=(0, 1, 0))
    collide((x0 - .1, y - .2, 0), (x1 + .1, y + .1, 2.1))
    # Meter base with its glass, the fused disconnect with its handle, and the
    # transfer switch.
    box(p, 'galvanized', (5.15, y - .16, 1.2), (5.5, y - .03, 1.7))
    tube(p, 'meter_glass', (5.325, y - .16, 1.45), (5.325, y - .3, 1.45), 0.09, 14)
    box(p, 'rru_grey', (5.6, y - .2, 0.9), (6.0, y - .03, 1.55))
    beam(p, 'yellow', (6.0, y - .12, 1.3), (6.12, y - .12, 1.42), .025, .025, up=(0, 1, 0))
    box(p, 'rru_grey', (5.2, y - .18, 0.35), (5.95, y - .03, 0.68))
    for x in (5.3, 5.8):
        tube(p, 'dark_steel', (x, y - .1, 0), (x, y - .1, 0.35), 0.022, 6)
    tube(p, 'dark_steel', (5.5, y - .1, 0.05), (5.5, SHELTER[3] + 0.5, 0.05), 0.03, 6)
    sign(p, 'sign_asr', (5.6, y + .03, 1.95), (0, 1, 0), 0.62, 0.23, 0.006)


def fence_post(p, x, y, z_top, r=0.036):
    tube(p, 'galvanized', (x, y, 0), (x, y, z_top), r, 8)
    prism(p, 'pier', (x, y, 0), (x, y, 0.06), circle(r + 0.08, 8), smooth=True)
    lathe(p, 'galvanized', (x, y, z_top), (0, 0, 1), [(r + .008, 0), (r + .008, .03), (0, .06)], 8)


def barb_run(p, a, b, out):
    a, b = Vector(a), Vector(b)
    for k in range(3):
        off = out * (0.1 + 0.12 * k) + Vector((0, 0, 0.1 + 0.12 * k))
        tube(p, 'galvanized', a + off, b + off, 0.0045, 4, caps=False)


def fabric(p, a, b, z0, z1):
    """Double-sided alpha-cut chain link, one 2 m texture tile per 2 m."""
    a, b = Vector(a), Vector(b)
    L = (b - a).length
    n = Vector((b.y - a.y, -(b.x - a.x), 0)).normalized() * 0.004
    for s in (1, -1):
        q = [a + n * s + Vector((0, 0, z0)), b + n * s + Vector((0, 0, z0)),
             b + n * s + Vector((0, 0, z1)), a + n * s + Vector((0, 0, z1))]
        uv = [(0, z0 / 2), (L / 2, z0 / 2), (L / 2, z1 / 2), (0, z1 / 2)]
        if s < 0:
            q, uv = q[::-1], uv[::-1]
        p.face(q, 'chain_link', uvs=uv)


def build_fence():
    p = part('compound fence', LOD_SITE)
    fx0, fx1 = PAD_X[0] + FENCE_INSET, PAD_X[1] - FENCE_INSET
    fy0, fy1 = PAD_Y[0] + FENCE_INSET, PAD_Y[1] - FENCE_INSET
    H = 2.13
    gate = (-12.85, -9.15)        # on the east run
    runs = [((fx0, fy0), (fx1, fy0)), ((fx1, fy1), (fx0, fy1)), ((fx0, fy1), (fx0, fy0)),
            ((fx1, fy0), (fx1, gate[0])), ((fx1, gate[1]), (fx1, fy1))]
    centre = Vector(((fx0 + fx1) / 2, (fy0 + fy1) / 2, 0))
    for a, b in runs:
        a3, b3 = Vector((*a, 0)), Vector((*b, 0))
        L = (b3 - a3).length
        n = max(1, round(L / 2.8))
        out = Vector((b[1] - a[1], -(b[0] - a[0]), 0)).normalized()
        if out.dot(Vector(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, 0)) - centre) < 0:
            out = -out
        for k in range(n + 1):
            q = a3.lerp(b3, k / n)
            end = k in (0, n)
            fence_post(p, q.x, q.y, H, 0.05 if end else 0.036)
            # 45-degree barb arm leaning out of the compound.
            beam(p, 'galvanized', q + Vector((0, 0, H - 0.02)), q + out * 0.36 + Vector((0, 0, H + 0.36)),
                 0.03, 0.006, up=out.cross(Vector((0, 0, 1))))
        tube(p, 'galvanized', a3 + Vector((0, 0, H - 0.06)), b3 + Vector((0, 0, H - 0.06)), 0.021, 6)
        tube(p, 'galvanized', a3 + Vector((0, 0, 0.08)), b3 + Vector((0, 0, 0.08)), 0.004, 4)
        fabric(p, a3, b3, 0.05, H - 0.06)
        barb_run(p, a3 + Vector((0, 0, H)), b3 + Vector((0, 0, H)), out)
        # Collision: a thin wall along the run.
        lo = Vector((min(a[0], b[0]), min(a[1], b[1]), 0)) - Vector((0.04, 0.04, 0))
        hi = Vector((max(a[0], b[0]), max(a[1], b[1]), H + 0.4)) + Vector((0.04, 0.04, 0))
        collide(lo, hi)
    # The double gate, closed and chained: frames, fabric, drop rod, hinges.
    gy0, gy1 = gate
    mid = (gy0 + gy1) / 2
    for a, b in ((gy0 + 0.06, mid - 0.01), (mid + 0.01, gy1 - 0.06)):
        for y in (a, b):
            tube(p, 'galvanized', (fx1, y, 0.06), (fx1, y, H - 0.04), 0.024, 6)
        for z in (0.06, 1.05, H - 0.04):
            tube(p, 'galvanized', (fx1, a, z), (fx1, b, z), 0.024, 6)
        tube(p, 'galvanized', (fx1, a, 0.1), (fx1, b, H - 0.1), 0.012, 5)
        fabric(p, (fx1, a, 0), (fx1, b, 0), 0.08, H - 0.06)
        for z in (0.3, H - 0.3):
            y = gy0 + 0.02 if a < mid else gy1 - 0.02
            tube(p, 'galvanized', (fx1, y, z - .05), (fx1, y, z + .05), 0.045, 8)
    tube(p, 'galvanized', (fx1 + .04, mid - .05, 0.0), (fx1 + .04, mid - .05, 1.2), 0.012, 5)
    torus(p, 'galvanized', (fx1 + 0.04, mid, 1.05), (0, 0, 1), 0.06, 0.008, 10, 4)
    box(p, 'yellow', (fx1 + .05, mid - .03, 0.93), (fx1 + .07, mid + .03, 1.0))
    for s, y in ((1, gy0 - 0.05), (-1, gy1 + 0.05)):
        fence_post(p, fx1, y, H + 0.05, 0.05)
    sign(p, 'sign_danger', (fx1 + 0.02, mid - 0.85, 1.35), (1, 0, 0), 0.6, 0.45, 0.006)
    sign(p, 'sign_rf', (fx1 + 0.02, mid + 0.85, 1.35), (1, 0, 0), 0.6, 0.45, 0.006)
    sign(p, 'sign_rf', (0.0, fy0 - 0.02, 1.4), (0, -1, 0), 0.6, 0.45, 0.006)
    sign(p, 'sign_danger', (fx0 - 0.02, -4.0, 1.4), (-1, 0, 0), 0.6, 0.45, 0.006)
    collide((fx1 - .05, gy0, 0), (fx1 + .05, gy1, H + .1))


# ---------------------------------------------------------------------------
#  The far silhouette
# ---------------------------------------------------------------------------
FAR_W, FAR_H = 128, 2048


def far_texture(path):
    """Rasterise one face's members into an alpha texture: u across the face
    (0..1 at the leg centrelines), v up the lattice (0..LATTICE_TOP)."""
    img = np.zeros((FAR_H, FAR_W, 4), dtype=np.float32)
    colours = {'paint_orange': MATERIALS['paint_orange']['color'],
               'paint_white': MATERIALS['paint_white']['color']}
    ys = (np.arange(FAR_H) + 0.5) / FAR_H * LATTICE_TOP
    xs = (np.arange(FAR_W) + 0.5) / FAR_W
    X, Y = np.meshgrid(xs, ys)
    face_w = 2 * np.vectorize(half_width)(ys)[:, None]          # metres across at each row

    def stamp(mask, colour):
        img[mask, 0:3] = colour[:3]
        img[mask, 3] = 1.0

    # Legs at both edges, as wide in u as the real leg is in metres.
    for z0, z1 in zip(LEVELS, LEVELS[1:]):
        rows = (Y >= z0) & (Y < z1)
        leg_u = (2 * leg_radius(z0)) / face_w
        stamp(rows & ((X < leg_u) | (X > 1 - leg_u)), colours[paint((z0 + z1) / 2)])
    for u0, z0, u1, z1, width, colour in FAR_MEMBERS:
        # Distance from each texel to the member segment, measured in metres
        # so a member keeps its real width however narrow the face gets.
        px, py = X * face_w, Y
        ax, ay = u0 * 2 * half_width(z0), z0
        bx, by = u1 * 2 * half_width(z1), z1
        dx, dy = bx - ax, by - ay
        t = np.clip(((px - ax) * dx + (py - ay) * dy) / max(dx * dx + dy * dy, 1e-9), 0, 1)
        d = np.hypot(px - (ax + t * dx), py - (ay + t * dy))
        # Drawn a touch wider than the steel: mipmapped alpha thins a lattice
        # to nothing at range, and the bands are what read.
        stamp(d < width * 0.8, colours[colour])
    img = img[::-1]                       # Blender images are bottom-up
    im = bpy.data.images.new('far-lattice', FAR_W, FAR_H, alpha=True)
    im.pixels.foreach_set(img[::-1].ravel())
    im.filepath_raw = str(path)
    im.file_format = 'PNG'
    im.save()


def build_far():
    p = part('tower far', LOD_FAR)
    for face in FACES:
        z_steps = sorted(set([0.0] + [k * 1.0 for k in range(1, int(LATTICE_TOP))] + [LATTICE_TOP]))
        for z0, z1 in zip(z_steps, z_steps[1:]):
            q = [face_point(face, 0, z0), face_point(face, 1, z0), face_point(face, 1, z1), face_point(face, 0, z1)]
            # Nudged out a hair so the near set, drawn over it in the swap
            # band, never z-fights with it.
            out = face[2] * 0.01
            q = [v + out for v in q]
            uv = [(0, z0 / LATTICE_TOP), (1, z0 / LATTICE_TOP), (1, z1 / LATTICE_TOP), (0, z1 / LATTICE_TOP)]
            p.face(q, 'far_lattice', uvs=uv)
            p.face(q[::-1], 'far_lattice', uvs=uv[::-1])


def build_glow():
    """A unit sphere the world scales per light to keep a lit lamp a few
    pixels across however far away the camera is."""
    p = part('light glow', LOD_ALWAYS)
    sphere(p, 'glow', (0, 0, 0), 0.5, 10, 6)


# ---------------------------------------------------------------------------
#  Scene, preview and cook
# ---------------------------------------------------------------------------

def make_blender_materials():
    out = {}
    for name, spec in MATERIALS.items():
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        bsdf = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        col = spec['color']
        bsdf.inputs['Base Color'].default_value = (*col[:3], 1)
        bsdf.inputs['Roughness'].default_value = 0.6
        if spec.get('texture'):
            src = OUT / spec['texture']
            if src.is_file():
                tex = m.node_tree.nodes.new('ShaderNodeTexImage')
                tex.image = bpy.data.images.load(str(src), check_existing=True)
                m.node_tree.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
                if spec.get('alpha'):
                    m.node_tree.links.new(tex.outputs['Alpha'], bsdf.inputs['Alpha'])
                    try:
                        m.surface_render_method = 'DITHERED'
                    except (AttributeError, TypeError):
                        pass
        if spec.get('emissive') and PREVIEW_NIGHT:
            bsdf.inputs['Emission Color'].default_value = (*col[:3], 1)
            bsdf.inputs['Emission Strength'].default_value = 40.0
        if spec.get('glass'):
            bsdf.inputs['Alpha'].default_value = 0.35
        out[name] = m
    return out


def to_objects(mats):
    objs = []
    for name, p in PARTS.items():
        me = bpy.data.meshes.new(name)
        p.bm.to_mesh(me)
        p.bm.free()
        for m in p.mats:
            me.materials.append(mats[m])
        ob = bpy.data.objects.new(name, me)
        ob['lod'] = p.lod
        bpy.context.scene.collection.objects.link(ob)
        objs.append(ob)
    return objs


def engine(p):
    return (p[0], p[2], -p[1])


def write_emesh(path, verts, label):
    with path.open('wb') as f:
        name = label.encode() + b'\0'
        f.write(struct.pack('<8I', 0x48534D45, 2, 0, len(verts), len(verts), 1, len(name), 0))
        for v in verts:
            f.write(struct.pack('<12f', *v))
        f.write(struct.pack(f'<{len(verts)}I', *range(len(verts))))
        f.write(struct.pack('<4I', 0, len(verts), 0, 0))
        f.write(name)


def cook(objs):
    groups = defaultdict(list)
    for ob in objs:
        me = ob.data
        me.calc_loop_triangles()
        normals = me.corner_normals
        uv = me.uv_layers.active
        for tri in me.loop_triangles:
            mat = me.materials[tri.material_index].name
            key = (mat, ob['lod'])
            for li in tri.loops:
                loop = me.loops[li]
                p = engine(me.vertices[loop.vertex_index].co)
                n = Vector(engine(normals[li].vector)).normalized()
                t = n.cross(Vector((0, 1, 0)))
                if t.length < .001:
                    t = n.cross(Vector((1, 0, 0)))
                t.normalize()
                tex = uv.data[li].uv
                groups[key].append((*p, *n, tex[0], tex[1], *t, 1.0))
    rows, far_rows, glow_rows = [], [], []
    for i, ((mat, lod), verts) in enumerate(sorted(groups.items())):
        spec = MATERIALS[mat]
        stem = f'part_{i:02}'
        write_emesh(OUT / f'{stem}.emesh', verts, f'{mat}:{lod}')
        texname = spec.get('texture', '-')
        col = spec['color']
        row = (f'{stem}.emesh {texname} {int(spec.get("glass", False))} {spec.get("emissive", 0)} '
               f'{int(spec.get("alpha", False))} {lod} ' + ' '.join(f'{c:.4f}' for c in col))
        (glow_rows if mat == 'glow' else rows).append(row)
    (OUT / 'materials.txt').write_text('\n'.join(rows) + '\n')
    (OUT / 'glow.txt').write_text('\n'.join(glow_rows) + '\n')
    with (OUT / 'collision.txt').open('w') as f:
        for c, h in COLLISION:
            # Blender half-extents (x, y, z) -> engine (x, z, y); centre through engine().
            ec = engine(c)
            f.write(f'{ec[0]:.4f} {ec[1]:.4f} {ec[2]:.4f} {abs(h.x):.4f} {abs(h.z):.4f} {abs(h.y):.4f}\n')
    with (OUT / 'lights.txt').open('w') as f:
        for pos, kind in LIGHTS:
            e = engine(pos)
            f.write(f'{e[0]:.4f} {e[1]:.4f} {e[2]:.4f} {kind}\n')
    tris = {f'{m}:{l}': len(v) // 3 for (m, l), v in sorted(groups.items())}
    (OUT / 'manifest.json').write_text(json.dumps(dict(
        source='tools/ferrone_mast_blender.py (procedural; no supplied asset)',
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        transform='engine=(blender.x, blender.z, -blender.y); origin = tower centre on the pad top',
        structure_top_m=STRUCTURE_TOP, lattice_top_m=LATTICE_TOP,
        base_half_m=BASE_HALF, top_half_m=TOP_HALF, levels_m=[round(l, 3) for l in LEVELS],
        pad_min_engine=[PAD_X[0], -PAD_Y[1]], pad_max_engine=[PAD_X[1], -PAD_Y[0]],
        triangles=sum(tris.values()), triangles_by_part=tris,
        collision_boxes=len(COLLISION), lights=len(LIGHTS),
    ), indent=2))
    print('FERRONE MAST', sum(tris.values()), 'triangles,', len(rows), 'parts,',
          len(COLLISION), 'boxes,', len(LIGHTS), 'lights')


def preview(objs):
    scene = bpy.context.scene
    for ob in objs:
        lod = ob['lod']
        ob.hide_render = (lod == LOD_FAR) != PREVIEW_FAR and lod in (LOD_FAR, LOD_NEAR)
        if ob.name == 'light glow':
            ob.hide_render = True
    # A slab standing in for the pad and the hilltop.
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, -4, -0.5))
    pad = bpy.context.active_object
    pad.scale = (PAD_X[1] - PAD_X[0], PAD_Y[1] - PAD_Y[0], 1)
    pad.location = ((PAD_X[0] + PAD_X[1]) / 2, (PAD_Y[0] + PAD_Y[1]) / 2, -0.5)
    gm = bpy.data.materials.new('pad')
    gm.use_nodes = True
    next(n for n in gm.node_tree.nodes if n.type == 'BSDF_PRINCIPLED').inputs['Base Color'].default_value = (.42, .41, .38, 1)
    pad.data.materials.append(gm)
    world = bpy.data.worlds.new('sky')
    world.use_nodes = True
    bg = next(n for n in world.node_tree.nodes if n.type == 'BACKGROUND')
    bg.inputs['Color'].default_value = (0.02, 0.025, 0.05, 1) if PREVIEW_NIGHT else (0.55, 0.68, 0.85, 1)
    bg.inputs['Strength'].default_value = 1.0
    scene.world = world
    sun = bpy.data.lights.new('sun', 'SUN')
    sun.energy = 0.3 if PREVIEW_NIGHT else 4.0
    so = bpy.data.objects.new('sun', sun)
    so.rotation_euler = (math.radians(50), 0, math.radians(35))
    scene.collection.objects.link(so)
    cam = bpy.data.cameras.new('cam')
    cam.lens = 28 if not PREVIEW_FAR else 200
    co = bpy.data.objects.new('cam', cam)
    scene.collection.objects.link(co)
    scene.camera = co
    target = Vector((0, -3, 22)) if not PREVIEW_FAR else Vector((0, 0, 30))
    co.location = Vector((34, -46, 12)) if not PREVIEW_FAR else Vector((400, -600, 60))
    if '--close' in argv:
        co.location, target = Vector((12, -24, 3.2)), Vector((1, -7, 1.6))
        cam.lens = 24
    if '--top' in argv:
        co.location, target = Vector((9, -9, 58)), Vector((0, 0, 44))
        cam.lens = 24
    if '--cam' in argv:
        co.location = Vector(map(float, argv[argv.index('--cam') + 1].split(',')))
        target = Vector(map(float, argv[argv.index('--look') + 1].split(',')))
        cam.lens = float(argv[argv.index('--lens') + 1]) if '--lens' in argv else 35
    co.rotation_euler = (target - co.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.resolution_x, scene.render.resolution_y = 1280, 1600
    engines = [e.identifier for e in scene.render.bl_rna.properties['engine'].enum_items]
    for e in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT', 'BLENDER_WORKBENCH'):
        try:
            scene.render.engine = e
            break
        except TypeError:
            continue
    scene.render.filepath = str(PREVIEW)
    bpy.ops.render.render(write_still=True)
    print('PREVIEW', PREVIEW, scene.render.engine, engines)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ('station-sign.png', 'danger-sign.png', 'rf-caution.png', 'asr-plate.png',
                 'precast-concrete.png'):
        src = TEX / name
        if not src.is_file():
            raise SystemExit(f'missing {src}; run python3 tools/make_ferrone_mast_textures.py first')
        shutil.copyfile(src, OUT / name)
    shutil.copyfile(CHAIN_LINK, OUT / 'chain-link.png')
    for stale in OUT.glob('part_*.emesh'):
        stale.unlink()

    build_legs()
    build_bracing()
    build_ladder()
    build_platforms()
    build_pole_and_beacon()
    build_side_lights()
    build_fm()
    build_cell_sectors()
    build_dishes()
    build_shelter()
    build_ice_bridge()
    build_generator()
    build_meter_rack()
    build_fence()
    build_far()
    build_glow()
    far_texture(OUT / 'far-lattice.png')

    mats = make_blender_materials()
    objs = to_objects(mats)
    cook(objs)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'ferrone_mast.blend'))
    if PREVIEW:
        preview(objs)


main()
