import * as THREE from 'three';
import { TessellateModifier } from 'three/addons/modifiers/TessellateModifier.js';
import { polygon, grid as sharedGrid, sideX, topAt, widthAt } from './surface.js';
import { PIZAZ_SHAPE, type PizazMaterials } from './moduleTypes.js';

type FaceDirection = 1 | -1;
type FaceZ = (x: number) => number;

function addMesh<T extends THREE.BufferGeometry>(
  parent: THREE.Object3D,
  name: string,
  geometry: T,
  material: THREE.Material,
  position: [number, number, number] = [0, 0, 0],
): THREE.Mesh<T, THREE.Material> {
  const mesh = new THREE.Mesh(geometry, material);
  mesh.name = name;
  mesh.position.set(...position);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  parent.add(mesh);
  return mesh;
}

function addRoundedContour(
  path: THREE.Path | THREE.Shape,
  width: number,
  height: number,
  radius: number,
  reverse = false,
): void {
  const x = width / 2;
  const y = height / 2;
  const r = Math.min(radius, x, y);
  if (!reverse) {
    path.moveTo(-x + r, -y);
    path.lineTo(x - r, -y);
    path.quadraticCurveTo(x, -y, x, -y + r);
    path.lineTo(x, y - r);
    path.quadraticCurveTo(x, y, x - r, y);
    path.lineTo(-x + r, y);
    path.quadraticCurveTo(-x, y, -x, y - r);
    path.lineTo(-x, -y + r);
    path.quadraticCurveTo(-x, -y, -x + r, -y);
  } else {
    path.moveTo(-x + r, -y);
    path.lineTo(-x, -y + r);
    path.lineTo(-x, y - r);
    path.quadraticCurveTo(-x, y, -x + r, y);
    path.lineTo(x - r, y);
    path.quadraticCurveTo(x, y, x, y - r);
    path.lineTo(x, -y + r);
    path.quadraticCurveTo(x, -y, x - r, -y);
  }
  path.closePath();
}

function roundedShape(width: number, height: number, radius: number): THREE.Shape {
  const shape = new THREE.Shape();
  addRoundedContour(shape, width, height, radius);
  return shape;
}

function roundedFrameShape(
  width: number,
  height: number,
  innerWidth: number,
  innerHeight: number,
  radius: number,
): THREE.Shape {
  const shape = roundedShape(width, height, radius);
  const hole = new THREE.Path();
  addRoundedContour(hole, innerWidth, innerHeight, Math.min(radius * 0.72, innerWidth * 0.2), true);
  shape.holes.push(hole);
  return shape;
}

function curvedShape(
  parent: THREE.Object3D,
  name: string,
  shape: THREE.Shape,
  centerX: number,
  centerY: number,
  surfaceZ: FaceZ,
  direction: FaceDirection,
  outwardOffset: number,
  depth: number,
  material: THREE.Material,
): THREE.Mesh<THREE.BufferGeometry, THREE.Material> {
  const geometry = new TessellateModifier(.055,7).modify(new THREE.ExtrudeGeometry(shape, {
    depth,
    bevelEnabled: false,
    steps: 1,
    curveSegments: 4,
  }));
  const positions = geometry.getAttribute('position');
  for (let i = 0; i < positions.count; i++) {
    const x = centerX + positions.getX(i);
    const y = centerY + positions.getY(i);
    const extrusion = positions.getZ(i);
    positions.setXYZ(i, x, y, surfaceZ(x) + direction * (outwardOffset + extrusion));
  }
  positions.needsUpdate = true;
  geometry.computeVertexNormals();
  return addMesh(parent, name, geometry, material);
}

function curvedPanel(
  parent: THREE.Object3D,
  name: string,
  width: number,
  height: number,
  radius: number,
  centerX: number,
  centerY: number,
  surfaceZ: FaceZ,
  direction: FaceDirection,
  outwardOffset: number,
  depth: number,
  material: THREE.Material,
): void {
  curvedShape(parent, name, roundedShape(width, height, radius), centerX, centerY,
    surfaceZ, direction, outwardOffset, depth, material);
}

function curvedFrame(
  parent: THREE.Object3D,
  name: string,
  width: number,
  height: number,
  innerWidth: number,
  innerHeight: number,
  radius: number,
  centerX: number,
  centerY: number,
  surfaceZ: FaceZ,
  direction: FaceDirection,
  outwardOffset: number,
  depth: number,
  material: THREE.Material,
): void {
  curvedShape(parent, name,
    roundedFrameShape(width, height, innerWidth, innerHeight, radius),
    centerX, centerY, surfaceZ, direction, outwardOffset, depth, material);
}

function addCrownedEndSkin(
  parent: THREE.Object3D,
  name: string,
  endZ: number,
  lowerY: number,
  surfaceZ: FaceZ,
  direction: FaceDirection,
  material: THREE.Material,
): void {
  const edgeY = topAt(endZ);
  const fractions = [0, 0.16, 0.34, 0.52, 0.68, 0.84, 1];
  const columns = 32;
  const rows = fractions.map(fraction => Array.from({ length: columns + 1 }, (_, column) => {
    const u = -1 + (2 * column) / columns;
    if (fraction === 1) {
      const halfWidth = sideX(endZ, edgeY);
      return new THREE.Vector3(
        u * halfWidth,
        edgeY + 0.028 * (1 - u * u),
        endZ,
      );
    }
    const y = lowerY + (edgeY - lowerY) * fraction;
    const x = u * sideX(endZ, y);
    // Recess the detail band while joining the exact crowned hood/deck edge.
    const depth = 0.025 * Math.sin(Math.PI * fraction);
    return new THREE.Vector3(x, y, surfaceZ(x) - direction * depth);
  }));
  sharedGrid(parent, name, rows, material, new THREE.Vector3(0, 0, direction));
}

function makeBumperShell(
  parent: THREE.Object3D,
  name: string,
  front: boolean,
  bottom: number,
  top: number,
  materials: PizazMaterials,
): { surfaceZ: FaceZ; endZ: number } {
  const direction:FaceDirection=front?1:-1,endZ=front?2.31:-2.31;
  const surfaceZ:FaceZ=x=>endZ+direction*.006*(1-Math.pow(x/.805,2));
  const levels=Array.from({length:13},(_,i)=>bottom+(top-bottom)*i/12);
  const rows=levels.map(y=>Array.from({length:41},(_,i)=>{const x=(-1+i/20)*sideX(endZ,y);return new THREE.Vector3(x,y,surfaceZ(x));}));
  sharedGrid(parent,name,rows,materials.paint,new THREE.Vector3(0,0,direction));
  // The corner skins follow the same body profile, with a 2 mm bumper seam.
  for(const side of [-1,1]){
    const back=front?1.79:-1.79,backBottom=.215;
    polygon(parent,`${name}_corner_${side}`,[[endZ,bottom],[back,backBottom],[back,top],[endZ,top]],
      (z,y)=>new THREE.Vector3(side*(sideX(z,y)+.002),y,z),materials.paint,new THREE.Vector3(side,0,0),3);
  }
  return {surfaceZ,endZ};
}

function addBadge(
  parent: THREE.Object3D,
  name: string,
  centerX: number,
  centerY: number,
  surfaceZ: FaceZ,
  direction: FaceDirection,
  materials: PizazMaterials,
  offset: number,
): void {
  const d = 0.004;
  curvedPanel(parent, name + '_stem', 0.009, 0.042, 0.002,
    centerX - 0.011, centerY - 0.001, surfaceZ, direction, offset, d, materials.silver);
  curvedPanel(parent, name + '_top', 0.024, 0.007, 0.002,
    centerX + 0.002, centerY + 0.017, surfaceZ, direction, offset, d, materials.silver);
  curvedPanel(parent, name + '_mid', 0.020, 0.006, 0.002,
    centerX + 0.001, centerY + 0.001, surfaceZ, direction, offset, d, materials.silver);
  curvedPanel(parent, name + '_bowl', 0.006, 0.018, 0.002,
    centerX + 0.011, centerY + 0.009, surfaceZ, direction, offset, d, materials.silver);
}

function addFrontDetails(parent: THREE.Object3D, materials: PizazMaterials, surfaceZ: FaceZ): void {
  const direction: FaceDirection = 1;
  const detailSurfaceZ: FaceZ = x => surfaceZ(x) + .007;
  addCrownedEndSkin(parent, 'front_header_skin', PIZAZ_SHAPE.frontZ, 0.49,
    surfaceZ, direction, materials.paint);

  curvedFrame(parent, 'front_grille_bezel', 0.60, 0.119, 0.568, 0.087, 0.020,
    0, 0.595, detailSurfaceZ, direction, 0.003, 0.005, materials.trim);
  curvedPanel(parent, 'front_grille_dark_recess', 0.56, 0.078, 0.016,
    0, 0.595, detailSurfaceZ, direction, 0.002, 0.002, materials.darkPaint);
  for (let row = 0; row < 3; row++) {
    const y = 0.568 + row * 0.026;
    for (const side of [-1, 1] as const) {
      curvedPanel(parent, 'front_grille_slat_' + row + '_' + side, 0.235, 0.0045, 0.002,
        side * 0.158, y, detailSurfaceZ, direction, 0.006, 0.002, materials.silver);
    }
  }
  addBadge(parent, 'front_P_badge', 0, 0.595, detailSurfaceZ, direction, materials, 0.008);

  for (const side of [-1, 1] as const) {
    const label = side > 0 ? 'driver' : 'passenger';
    const x = side * 0.52;
    curvedFrame(parent, label + '_headlamp_bezel', 0.43, 0.128, 0.394, 0.096, 0.018,
      x, 0.595, detailSurfaceZ, direction, 0.003, 0.005, materials.trim);
    curvedPanel(parent, label + '_headlamp_lens', 0.39, 0.093, 0.013,
      x, 0.595, detailSurfaceZ, direction, 0.008, 0.004, materials.lamp);
    for (const cell of [-1, 1] as const) {
      curvedPanel(parent, label + '_headlamp_reflector_' + cell, 0.006, 0.076, 0.002,
        x + cell * 0.087, 0.595, detailSurfaceZ, direction, 0.013, 0.002, materials.silver);
    }

    const amberX = side * 0.75;
    curvedFrame(parent, label + '_amber_marker_bezel', 0.066, 0.112, 0.050, 0.094, 0.010,
      amberX, 0.595, detailSurfaceZ, direction, 0.003, 0.004, materials.trim);
    curvedPanel(parent, label + '_amber_corner', 0.048, 0.092, 0.009,
      amberX, 0.595, detailSurfaceZ, direction, 0.007, 0.003, materials.amber);

    curvedFrame(parent, label + '_fog_lamp_bezel', 0.176, 0.058, 0.150, 0.034, 0.012,
      side * 0.65, 0.402, surfaceZ, direction, 0.002, 0.004, materials.trim);
    curvedPanel(parent, label + '_fog_lamp', 0.146, 0.030, 0.007,
      side * 0.65, 0.402, surfaceZ, direction, 0.004, 0.002, materials.lamp);
  }
}

function addFrontBumperDetails(parent: THREE.Object3D, materials: PizazMaterials, surfaceZ: FaceZ): void {
  const direction: FaceDirection = 1;
  const openingWidth = 1.08;
  const openingHeight = 0.095;
  const centerY = 0.315;
  curvedFrame(parent, 'front_inlet_lip', openingWidth + 0.035, openingHeight + 0.022,
    openingWidth - 0.002, openingHeight - 0.002, 0.025,
    0, centerY, surfaceZ, direction, 0.001, 0.003, materials.trim);
  curvedPanel(parent, 'front_inlet_recess', openingWidth - 0.035, openingHeight - 0.012, 0.024,
    0, centerY, surfaceZ, direction, 0.0005, 0.001, materials.darkPaint);
  for (let i = 0; i < 3; i++) {
    curvedPanel(parent, 'front_inlet_louver_' + i, openingWidth - 0.13, 0.004, 0.002,
      0, centerY - 0.027 + i * 0.027, surfaceZ, direction, 0.002, 0.001, materials.trim);
  }
}

function addRearDetails(parent: THREE.Object3D, materials: PizazMaterials, surfaceZ: FaceZ): void {
  const direction: FaceDirection = -1;
  const detailSurfaceZ: FaceZ = x => surfaceZ(x) + direction * .007;
  addCrownedEndSkin(parent, 'rear_tail_panel', PIZAZ_SHAPE.rearZ, 0.50,
    surfaceZ, direction, materials.paint);

  curvedFrame(parent, 'rear_lamp_bezel', 1.56, 0.090, 1.51, 0.064, 0.018,
    0, 0.69, detailSurfaceZ, direction, 0.004, 0.005, materials.trim);
  curvedPanel(parent, 'rear_full_width_red_lens', 1.506, 0.060, 0.013,
    0, 0.69, detailSurfaceZ, direction, 0.010, 0.004, materials.red);
  for (const side of [-1, 1] as const) {
    const label = side > 0 ? 'driver' : 'passenger';
    curvedFrame(parent, label + '_reverse_lamp_bezel', 0.16, 0.052, 0.134, 0.034, 0.009,
      side * 0.42, 0.69, detailSurfaceZ, direction, 0.016, 0.004, materials.trim);
    curvedPanel(parent, label + '_reverse_lamp', 0.132, 0.032, 0.007,
      side * 0.42, 0.69, detailSurfaceZ, direction, 0.020, 0.003, materials.lamp);
  }
  addBadge(parent, 'rear_P_badge', 0, 0.69, detailSurfaceZ, direction, materials, 0.020);

  // Shallow plate recess reads as a dark inset with a body-color bumper behind it.
  curvedFrame(parent, 'rear_plate_recess_frame', 0.478, 0.205, 0.428, 0.160, 0.027,
    0, 0.47, surfaceZ, direction, 0.001, 0.003, materials.trim);
  curvedPanel(parent, 'rear_plate_blank', 0.394, 0.132, 0.021,
    0, 0.47, surfaceZ, direction, 0.001, 0.001, materials.darkPaint);
}

function wheelSpokeShape(angle: number): THREE.Shape {
  const radialY = Math.cos(angle);
  const radialZ = Math.sin(angle);
  const tangentY = -radialZ;
  const tangentZ = radialY;
  const point = (radius: number, halfWidth: number, sign: number): [number, number] => [
    radialY * radius + tangentY * halfWidth * sign,
    radialZ * radius + tangentZ * halfWidth * sign,
  ];
  const a = point(0.044, 0.022, -1);
  const b = point(0.044, 0.022, 1);
  const c = point(0.216, 0.029, 1);
  const d = point(0.216, 0.029, -1);
  const shape = new THREE.Shape();
  shape.moveTo(a[0], a[1]);
  shape.lineTo(b[0], b[1]);
  shape.lineTo(c[0], c[1]);
  shape.lineTo(d[0], d[1]);
  shape.closePath();
  return shape;
}

function addWheel(parent: THREE.Object3D, side: -1 | 1, axle: 'front' | 'rear', materials: PizazMaterials): void {
  const z = axle === 'front' ? PIZAZ_SHAPE.frontWheelZ : PIZAZ_SHAPE.rearWheelZ;
  const wheel = new THREE.Group();
  wheel.name = (side > 0 ? 'driver' : 'passenger') + '_' + axle + '_wheel';
  wheel.position.set(side * PIZAZ_SHAPE.wheelX, PIZAZ_SHAPE.wheelY, z);
  wheel.userData = { rotationAxis: 'X', anchor: [side * PIZAZ_SHAPE.wheelX, PIZAZ_SHAPE.wheelY, z] };
  parent.add(wheel);

  // A lathed cross-section gives the tire a rounded shoulder and a real sidewall.
  const tireProfile = [
    new THREE.Vector2(0.264, -0.0925),
    new THREE.Vector2(0.292, -0.0925),
    new THREE.Vector2(0.310, -0.084),
    new THREE.Vector2(0.319, -0.066),
    new THREE.Vector2(PIZAZ_SHAPE.tireRadius, -0.045),
    new THREE.Vector2(PIZAZ_SHAPE.tireRadius, 0.045),
    new THREE.Vector2(0.319, 0.066),
    new THREE.Vector2(0.310, 0.084),
    new THREE.Vector2(0.292, 0.0925),
    new THREE.Vector2(0.264, 0.0925),
    new THREE.Vector2(0.258, 0.078),
    new THREE.Vector2(0.258, -0.078),
  ];
  const tire = addMesh(wheel, 'profiled_tire', new THREE.LatheGeometry(tireProfile, 28), materials.rubber);
  tire.rotation.z = Math.PI / 2;

  for (const sidewall of [-1, 1] as const) {
    const ring = addMesh(wheel, 'tire_sidewall_ridge_' + sidewall,
      new THREE.TorusGeometry(0.287, 0.003, 4, 24), materials.rubber,
      [sidewall * 0.089, 0, 0]);
    ring.rotation.y = sidewall * Math.PI / 2;
  }

  const rim = addMesh(wheel, 'silver_rim_face', new THREE.CylinderGeometry(0.228, 0.228, 0.012, 24),
    materials.silver, [side * 0.087, 0, 0]);
  rim.rotation.z = Math.PI / 2;
  const inset = addMesh(wheel, 'dark_spoke_recess', new THREE.CylinderGeometry(0.195, 0.195, 0.006, 24),
    materials.trim, [side * 0.092, 0, 0]);
  inset.rotation.z = Math.PI / 2;
  const lip = addMesh(wheel, 'polished_rim_lip', new THREE.TorusGeometry(0.219, 0.005, 4, 24),
    materials.silver, [side * 0.096, 0, 0]);
  lip.rotation.y = side * Math.PI / 2;

  for (let index = 0; index < 5; index++) {
    const geometry = new THREE.ExtrudeGeometry(wheelSpokeShape(index * Math.PI * 2 / 5), {
      depth: 0.007,
      bevelEnabled: true,
      bevelSegments: 1,
      bevelSize: 0.001,
      bevelThickness: 0.001,
      steps: 1,
    });
    const spoke = addMesh(wheel, 'five_spoke_alloy_' + (index + 1), geometry, materials.silver,
      [side * 0.094, 0, 0]);
    spoke.rotation.y = side * Math.PI / 2;
  }

  const hub = addMesh(wheel, 'silver_hub_cap', new THREE.CylinderGeometry(0.051, 0.051, 0.012, 16),
    materials.silver, [side * 0.095, 0, 0]);
  hub.rotation.z = Math.PI / 2;
  const cap = addMesh(wheel, 'dark_P_hub', new THREE.CylinderGeometry(0.030, 0.030, 0.004, 12),
    materials.trim, [side * 0.102, 0, 0]);
  cap.rotation.z = Math.PI / 2;

  for (let lug = 0; lug < 5; lug++) {
    const angle = lug * Math.PI * 2 / 5;
    const bolt = addMesh(wheel, 'alloy_lug_' + (lug + 1), new THREE.CylinderGeometry(0.007, 0.007, 0.003, 8),
      materials.silver, [side * 0.098, Math.cos(angle) * 0.148, Math.sin(angle) * 0.148]);
    bolt.rotation.z = Math.PI / 2;
  }

  // Mirror the small cap mark on the far side so it reads upright from both sides.
  const pCenter = side * 0.105;
  addMesh(wheel, 'P_hub_mark_stem', new THREE.BoxGeometry(0.002, 0.024, 0.005),
    materials.silver, [pCenter, -0.001, 0]);
  addMesh(wheel, 'P_hub_mark_top', new THREE.BoxGeometry(0.002, 0.005, 0.017),
    materials.silver, [pCenter, 0.009, side * 0.004]);
  addMesh(wheel, 'P_hub_mark_mid', new THREE.BoxGeometry(0.002, 0.004, 0.014),
    materials.silver, [pCenter, 0.001, side * 0.003]);
  addMesh(wheel, 'P_hub_mark_bowl', new THREE.BoxGeometry(0.002, 0.013, 0.005),
    materials.silver, [pCenter, 0.006, side * 0.008]);
}

export function addPizazFasciaAndWheels(parent: THREE.Group, materials: PizazMaterials): void {
  const module = new THREE.Group();
  module.name = 'fascia_and_wheels';
  module.userData = { units: 'metres', forward: '+Z', up: '+Y' };
  parent.add(module);

  const front = makeBumperShell(module, 'front_bumper_wrap', true, 0.235, 0.50, materials);
  const rear = makeBumperShell(module, 'rear_bumper_wrap', false, 0.29, 0.51, materials);
  addFrontDetails(module, materials, front.surfaceZ);
  addFrontBumperDetails(module, materials, front.surfaceZ);
  addRearDetails(module, materials, rear.surfaceZ);

  const wheels = new THREE.Group();
  wheels.name = 'separate_wheels';
  module.add(wheels);
  addWheel(wheels, -1, 'front', materials);
  addWheel(wheels, 1, 'front', materials);
  addWheel(wheels, -1, 'rear', materials);
  addWheel(wheels, 1, 'rear', materials);
}
