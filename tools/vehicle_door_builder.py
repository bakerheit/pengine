"""Shared new-vehicle doorway construction; all positions remain model-local."""
from vesper_mistral_blender import clipped_ring


def split_cabin_side(b,sections,side,door,cabin_rear,material='SIDE'):
    """Keep original outer skin, replace the buried cabin wedge with a thin shell.

    Sections use positive X radii. Mirror only after clipping so both sides get
    identical winding/clearance. Only +X yields the independently moving door.
    """
    rear,front,sill=door['rear_z'],door['front_z'],door['sill_y']
    def at(z):
        for (az,a),(cz,c) in zip(sections,sections[1:]):
            if az<=z<=cz and cz>az:
                t=(z-az)/(cz-az)
                return z,[(ax+t*(cx-ax),ay+t*(cy-ay)) for (ax,ay),(cx,cy) in zip(a,c)]
        raise ValueError(z)
    def span(a,c):return [at(a)]+[s for s in sections if a<s[0]<c]+[at(c)]
    def loft(name,ss):return b.loft(name+str(side),[(z,[(side*x,y) for x,y in r]) for z,r in ss],material)
    fixed=[loft('RearQuarter',[s for s in sections if s[0]<cabin_rear]+[at(cabin_rear)]),
           loft('FrontQuarter',[at(front)]+[s for s in sections if s[0]>front]),
           loft('CabinSill',[(z,clipped_ring(r,sill,False)) for z,r in span(cabin_rear,front)])]
    def outer(ss):return [(z,clipped_ring(clipped_ring(r,sill,True),door['inner_x'],True,axis=0)) for z,r in ss]
    if side==1:
        fixed.append(loft('FixedRearCabinWall',outer(span(cabin_rear,rear))))
        moving=loft('DriverDoorOuter',outer(span(rear,front)))
    else:
        fixed.append(loft('PassengerCabinWall',outer(span(cabin_rear,front))))
        moving=None
    return fixed,moving
