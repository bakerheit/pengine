import * as THREE from 'three';
import type {PizazMaterials} from './moduleTypes.js';
import {BELT,FRONT_SEAM,B_FRONT,B_REAR,rearDoorEdge,topAt,sideX,grid,polygon,lerp,tube,sampleLoop,interp,type P2} from './surface.js';
const UP=new THREE.Vector3(0,1,0);
function lowerEdge(start:number,end:number):P2[]{
  const bottom=(z:number)=>interp([[-2.31,.29],[-1.745,.21],[1.745,.21],[2.31,.235]],z);
  const pts:P2[]=[[start,bottom(start)]];
  for(const axle of [-1.38,1.38])if(axle-.365>start&&axle+.365<end){
    pts.push([axle-.365,.21]);
    for(let i=0;i<=32;i++){const a=Math.PI-i*Math.PI/32;pts.push([axle+.365*Math.cos(a),.32+.365*Math.sin(a)]);}
    pts.push([axle+.365,.21]);
  }
  pts.push([end,bottom(end)]);return pts;
}
function topEdge(start:number,end:number):P2[]{return Array.from({length:26},(_,i)=>{const z=lerp(start,end,i/25);return [z,topAt(z)];});}
function upper(parent:THREE.Group,name:string,start:number,end:number,m:PizazMaterials):void{
  const rows=Array.from({length:31},(_,i)=>{const z=lerp(start,end,i/30),edge=topAt(z),w=sideX(z,edge);return Array.from({length:17},(_,j)=>{const t=-1+j/8;return new THREE.Vector3(t*w,edge+.028*(1-t*t),z);});});
  grid(parent,name,rows,m.paint,UP);
  for(const s of [-1,1]){const points=rows.map(row=>row[s>0?15:1].clone().add(new THREE.Vector3(0,.0015,0)));tube(parent,`${name}_panel_gap_${s}`,points,.0015,m.darkPaint);}
}
export function addPizazBody(parent:THREE.Group,m:PizazMaterials):void{
 const shell=new THREE.Group();shell.name='continuous_pressed_body';parent.add(shell);
 upper(shell,'hood_crowned_stamping',.88,2.31,m);upper(shell,'trunk_crowned_stamping',-2.31,-1.48,m);
 for(const side of [-1,1]){
  const normal=new THREE.Vector3(side,0,0),map=(z:number,y:number)=>new THREE.Vector3(side*sideX(z,y),y,z),label=side>0?'driver':'passenger';
  const front:P2[]=[...lowerEdge(FRONT_SEAM+.004,2.31),...topEdge(2.31,FRONT_SEAM+.004)];
  polygon(shell,`${label}_front_fender`,front,map,m.paint,normal,3);
  const rear:P2[]=[...lowerEdge(-2.31,rearDoorEdge(.265)-.004),[rearDoorEdge(.265)-.004,.265]];
  for(let i=1;i<=18;i++){const y=lerp(.265,BELT,i/18);rear.push([rearDoorEdge(y)-.004,y]);}
  rear.push(...topEdge(rearDoorEdge(BELT)-.004,-2.31));
  polygon(shell,`${label}_rear_quarter`,rear,map,m.paint,normal,3);
  polygon(shell,`${label}_rocker`,[[rearDoorEdge(.265),.21],[FRONT_SEAM,.21],[FRONT_SEAM,.261],[rearDoorEdge(.265),.261]],map,m.paint,normal,2);
  polygon(shell,`${label}_lower_B_post`,[[B_REAR,.263],[B_FRONT,.263],[B_FRONT,BELT],[B_REAR,BELT]],map,m.paint,normal,2);
  for(const axle of [-1.38,1.38]){
   const rows=Array.from({length:33},(_,i)=>{const a=Math.PI*i/32,z=axle+.365*Math.cos(a),y=.32+.365*Math.sin(a);return [map(z,y),new THREE.Vector3(side*.815,y-.002,z)];});
   grid(shell,`${label}_${axle>0?'front':'rear'}_arch_return`,rows,m.paint,normal);
   const well=Array.from({length:25},(_,i)=>{const a=Math.PI*i/24,z=axle+.374*Math.cos(a),y=.32+.374*Math.sin(a);return [new THREE.Vector3(side*.63,y,z),new THREE.Vector3(side*.835,y,z)];});
   grid(shell,`${label}_${axle>0?'front':'rear'}_wheel_well`,well,m.trim,new THREE.Vector3(0,-1,0));
  }
  // Door opening returns stay on the fixed shell; the skins close over them.
  const outlines:P2[][]=[[[FRONT_SEAM,.265],[FRONT_SEAM,BELT],[B_FRONT,BELT],[B_FRONT,.265]],[[B_REAR,.265],[B_REAR,BELT],[rearDoorEdge(BELT),BELT],...Array.from({length:14},(_,i)=>{const y=lerp(BELT,.265,i/13);return [rearDoorEdge(y),y] as P2;})]];
  outlines.forEach((loop,k)=>{const edge=sampleLoop(loop,12);const rows=[...edge,edge[0]].map(([z,y])=>[map(z,y),new THREE.Vector3(side*(sideX(z,y)-.065),y,z)]);grid(shell,`${label}_door_${k}_aperture_return`,rows,m.darkPaint,normal);});
  // A shallow crease remains straight through the door and fender stampings.
  for(const [a,b] of [[-2.27,-1.68],[-1.08,rearDoorEdge(.55)-.012],[FRONT_SEAM+.012,1.08],[1.68,2.24]]){
   const pts=Array.from({length:18},(_,i)=>{const z=lerp(a,b,i/17);return map(z,.55).add(new THREE.Vector3(side*.0015,0,0));});tube(shell,`${label}_body_molding_${a}`,pts,.004,m.darkPaint);
  }
 }
 const floor=new THREE.Mesh(new THREE.BoxGeometry(1.44,.045,2.13),m.trim);floor.name='interior_floor';floor.position.set(0,.24,-.04);shell.add(floor);
}
