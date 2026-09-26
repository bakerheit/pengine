import type * as THREE from 'three';

export interface PizazMaterials {
  paint: THREE.Material;
  darkPaint: THREE.Material;
  trim: THREE.Material;
  rubber: THREE.Material;
  silver: THREE.Material;
  glass: THREE.Material;
  lamp: THREE.Material;
  amber: THREE.Material;
  red: THREE.Material;
  upholstery: THREE.Material;
}

export const PIZAZ_SHAPE = {
  length: 4.62,
  bodyWidth: 1.79,
  roofHeight: 1.32,
  wheelbase: 2.76,
  wheelTrack: 1.56,
  wheelX: 0.78,
  wheelY: 0.32,
  frontWheelZ: 1.38,
  rearWheelZ: -1.38,
  tireRadius: 0.32,
  archRadius: 0.365,
  bodyHalfWidth: 0.875,
  frontZ: 2.31,
  rearZ: -2.31,
} as const;
