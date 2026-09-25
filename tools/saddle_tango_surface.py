"""Shared 1991 Tango stampings. Coordinates: X across, Y forward, Z up.

Every side panel, arch return and molding evaluates these same surfaces. This
keeps the four doors and the fenders on one continuous pressed body contour.
"""
from __future__ import annotations

import math

FRONT = 2.20
REAR = -2.67
FRONT_AXLE = 1.37
REAR_AXLE = -1.38
BELT = 1.00
SILL = .21
FRONT_DOOR_EDGE = .61
B_PILLAR = -.40
ARCH_RADIUS = .435
ARCH_CENTRE = .35


def smooth_profile(stations: tuple[tuple[float, float], ...], q: float) -> float:
    """Monotone cubic profile, so the bonnet and deck do not kink at stations."""
    if q <= stations[0][0]:
        a, b = stations[:2]
        return a[1] + (q-a[0]) * (b[1]-a[1]) / (b[0]-a[0])
    if q >= stations[-1][0]:
        a, b = stations[-2:]
        return b[1] + (q-b[0]) * (b[1]-a[1]) / (b[0]-a[0])

    def slope(i: int) -> float:
        if i == 0:
            a, b = stations[:2]
            return (b[1]-a[1]) / (b[0]-a[0])
        if i == len(stations)-1:
            a, b = stations[-2:]
            return (b[1]-a[1]) / (b[0]-a[0])
        a, b, c = stations[i-1:i+2]
        left = (b[1]-a[1]) / (b[0]-a[0])
        right = (c[1]-b[1]) / (c[0]-b[0])
        return 0.0 if left*right <= 0 else 2*left*right/(left+right)

    for i in range(1, len(stations)):
        a, b = stations[i-1:i+1]
        if q <= b[0]:
            d = b[0]-a[0]
            t = (q-a[0])/d
            return ((2*t**3-3*t*t+1)*a[1]
                    +(t**3-2*t*t+t)*d*slope(i-1)
                    +(-2*t**3+3*t*t)*b[1]
                    +(t**3-t*t)*d*slope(i))
    raise AssertionError(q)


WIDTH = ((REAR, .87), (-2.40, .925), (-1.77, .95),
         (-1.15, .955), (-.40, .955), (.61, .955),
         (1.37, .945), (1.95, .905), (FRONT, .855))
TOP = ((REAR, .89), (-2.40, .92), (-1.77, .98),
       (-1.55, 1.00), (-.40, 1.00), (.61, 1.00),
       (1.37, .92), (1.95, .82), (FRONT, .77))


def width_at(q: float) -> float:
    return smooth_profile(WIDTH, q)


def top_at(q: float) -> float:
    return smooth_profile(TOP, q)


def arch_bottom(q: float) -> float:
    bottom = SILL
    for axle in (REAR_AXLE, FRONT_AXLE):
        distance = abs(q-axle)
        if distance < ARCH_RADIUS:
            bottom = max(bottom, ARCH_CENTRE + math.sqrt(
                ARCH_RADIUS**2-distance**2))
        elif distance < ARCH_RADIUS+.055:
            t=(ARCH_RADIUS+.055-distance)/.055
            t=t*t*(3-2*t)
            bottom=max(bottom,SILL+(ARCH_CENTRE-SILL)*t)
    return bottom


def side_x(q: float, height: float) -> float:
    fraction = max(0.0, min(1.0, (height-SILL)/(top_at(q)-SILL)))
    # A quiet shoulder crease and a fuller lower door give Tango its formal
    # early-1990s section. Pizaz has a lower, narrower sports-sedan section.
    inset = smooth_profile(((0, .062), (.15, .037), (.42, .010),
                            (.65, 0), (.84, .018), (1, .082)), fraction)
    flare = 0.0
    for axle in (REAR_AXLE, FRONT_AXLE):
        radius = math.hypot(q-axle, height-ARCH_CENTRE)
        flare = max(flare, .018*math.exp(-((radius-.46)/.085)**2))
    return width_at(q)-inset+flare


def rear_door_edge(v: float) -> float:
    return smooth_profile(((0, -.92), (.27, -1.02),
                           (.55, -1.24), (1, -1.53)), v)


def cabin_x(height: float) -> float:
    return smooth_profile(((1.00, .82), (1.08, .795),
                           (1.28, .715), (1.43, .665),
                           (1.47, .65)), height)
