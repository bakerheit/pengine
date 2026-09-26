import { writeFile, mkdir } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import * as THREE from 'three';
import { GLTFExporter } from 'three/addons/exporters/GLTFExporter.js';
import { createPizazConstant } from './createPizazConstant.js';

// GLTFExporter uses the browser FileReader API even for texture-free GLBs.
class NodeFileReader {
  result: ArrayBuffer | string | null = null;
  onloadend: (() => void) | null = null;

  readAsArrayBuffer(blob: Blob): void {
    void blob.arrayBuffer().then(value => { this.result = value; this.onloadend?.(); });
  }

  readAsDataURL(blob: Blob): void {
    void blob.arrayBuffer().then(value => {
      this.result = `data:${blob.type};base64,${Buffer.from(value).toString('base64')}`;
      this.onloadend?.();
    });
  }
}
(globalThis as unknown as { FileReader: typeof NodeFileReader }).FileReader = NodeFileReader;

const output = resolve(import.meta.dirname, '../../assets/models/vehicles/pizaz_constant/pizaz_constant.glb');
const car = createPizazConstant();
car.updateMatrixWorld(true);
// Reject broken geometry and missing articulated parts before writing a handoff.
car.traverse(object => {
  if (!(object instanceof THREE.Mesh)) return;
  const position = object.geometry.getAttribute('position');
  if (!position || !Array.from(position.array).every(Number.isFinite)) throw new Error(`Invalid positions: ${object.name}`);
});
for (const side of ['driver', 'passenger']) for (const axle of ['front', 'rear']) {
  for (const part of ['door', 'wheel']) if (!car.getObjectByName(`${side}_${axle}_${part}`)) throw new Error(`Missing ${side} ${axle} ${part}`);
  const door=car.getObjectByName(`${side}_${axle}_door`)!;
  const skin=door.getObjectByName(`${door.name}_pressed_outer_skin`)!;
  const bounds=new THREE.Box3().setFromObject(skin);
  if (axle==='rear' && bounds.min.z < -1.10) throw new Error('Rear door intrudes into the fixed wheel quarter');
  const shell=door.getObjectByName(`${door.name}_inner_shell`)!;
  const inner=new THREE.Box3().setFromObject(shell);
  if (Math.abs(Math.abs(bounds.max.x-inner.max.x)-.047)>.001) throw new Error('Door shell lost its 47 mm depth');
}
console.log('Geometry checks passed: finite positions, four doors, four wheels, rear-door boundary and door depth.');
const tracks: THREE.QuaternionKeyframeTrack[] = [];
for (const side of ['driver', 'passenger']) {
  for (const position of ['front', 'rear']) {
    const name = `${side}_${position}_door`;
    const direction = side === 'driver' ? -1 : 1;
    const open = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), direction * 1.04);
    tracks.push(new THREE.QuaternionKeyframeTrack(`${name}.quaternion`, [0, 0.4, 1.6, 2.0], [
      0, 0, 0, 1, open.x, open.y, open.z, open.w,
      open.x, open.y, open.z, open.w, 0, 0, 0, 1,
    ]));
  }
}
const doorDemo = new THREE.AnimationClip('open_four_doors', 2.0, tracks);
const data = await new GLTFExporter().parseAsync(car, { binary: true, onlyVisible: true, animations: [doorDemo] });
if (!(data instanceof ArrayBuffer)) throw new Error('GLB exporter returned text');
await mkdir(dirname(output), { recursive: true });
await writeFile(output, Buffer.from(data));
console.log(`Wrote ${output} (${data.byteLength} bytes)`);
