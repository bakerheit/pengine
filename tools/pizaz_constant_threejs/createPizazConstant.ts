import * as THREE from 'three';
import { addPizazBody } from './bodyModule.js';
import { addPizazCabin } from './cabinModule.js';
import { addPizazFasciaAndWheels } from './fasciaWheelModule.js';
import type { PizazMaterials } from './moduleTypes.js';
import { PIZAZ_SHAPE } from './moduleTypes.js';

function makeMaterials(): PizazMaterials {
  const materials: PizazMaterials = {
    paint: new THREE.MeshPhysicalMaterial({
      color: 0x641f32,
      metalness: 0.30,
      roughness: 0.34,
      clearcoat: 0.55,
      clearcoatRoughness: 0.35,
      side: THREE.DoubleSide,
    }),
    darkPaint: new THREE.MeshStandardMaterial({ color: 0x321e27, metalness: 0.18, roughness: 0.49, side: THREE.DoubleSide }),
    trim: new THREE.MeshStandardMaterial({ color: 0x151719, metalness: 0.02, roughness: 0.72, side: THREE.DoubleSide }),
    rubber: new THREE.MeshStandardMaterial({ color: 0x111315, metalness: 0, roughness: 0.94 }),
    silver: new THREE.MeshStandardMaterial({ color: 0xb3b6b6, metalness: 0.7, roughness: 0.35, side: THREE.DoubleSide }),
    glass: new THREE.MeshPhysicalMaterial({
      color: 0x344750,
      metalness: 0,
      roughness: 0.18,
      transparent: true,
      opacity: 0.72,
      depthWrite: false,
      side: THREE.DoubleSide,
    }),
    lamp: new THREE.MeshPhysicalMaterial({ color: 0xaeb9bb, metalness: 0.05, roughness: 0.35, side: THREE.DoubleSide }),
    amber: new THREE.MeshPhysicalMaterial({ color: 0xd58a38, metalness: 0, roughness: 0.28, side: THREE.DoubleSide }),
    red: new THREE.MeshPhysicalMaterial({ color: 0xa9182a, metalness: 0, roughness: 0.22, side: THREE.DoubleSide }),
    upholstery: new THREE.MeshStandardMaterial({ color: 0x303236, metalness: 0, roughness: 0.87 }),
  };
  for (const [name, material] of Object.entries(materials)) material.name = `pizaz_${name}`;
  return materials;
}

export function createPizazConstant(): THREE.Group {
  const car = new THREE.Group();
  car.name = 'PIZAZ_Constant_1991';
  car.userData = {
    units: 'metres',
    forward: '+Z',
    up: '+Y',
    source: 'PIZAZ Constant four-view reference set',
    year: 1991,
    shape: PIZAZ_SHAPE,
  };

  const materials = makeMaterials();
  const body = new THREE.Group();
  body.name = 'fixed_body';
  car.add(body);
  addPizazBody(body, materials);

  // Keep doors as root children so the demo animation can rotate their pivots.
  addPizazCabin(car, materials);

  // The wheel module adds one fixed fascia group and a separate wheel group.
  addPizazFasciaAndWheels(car, materials);

  return car;
}
