import * as THREE from 'three';
export type P2 = [number, number]; // longitudinal Z, height Y
export type Map2 = (z:number,y:number)=>THREE.Vector3;
export const BELT = .87;
export const DOOR_BOTTOM = .265;
export const FRONT_SEAM = .84;
export const B_FRONT = -.205;
export const B_REAR = -.245;
export function lerp(a:number,b:number,t:number):number{return a+(b-a)*t;}
export function interp(stations:readonly P2[],q:number):number {
  if(q<=stations[0][0])return stations[0][1];
  for(let i=1;i<stations.length;i++)if(q<=stations[i][0]){const a=stations[i-1],b=stations[i];return lerp(a[1],b[1],(q-a[0])/(b[0]-a[0]));}
  return stations[stations.length-1][1];
}
export function rearDoorEdge(y:number):number{return interp([[.265,-.86],[.34,-.925],[.53,-1.01],[.72,-1.07],[.87,-1.085]],y);}
export function topAt(z:number):number{return smoothProfile([[-2.31,.75],[-2.18,.795],[-1.6,.86],[-1.48,.89],[-1.1,.87],[.88,.87],[1.4,.81],[1.94,.755],[2.2,.70],[2.31,.65]],z);}
export function widthAt(z:number):number{return smoothProfile([[-2.31,.805],[-2.14,.86],[-1.6,.89],[-.8,.89],[.7,.89],[1.45,.885],[1.97,.87],[2.2,.838],[2.31,.805]],z);}
function smoothProfile(stations:readonly P2[],q:number):number {
  if(q<=stations[0][0]){const a=stations[0],b=stations[1];return a[1]+(q-a[0])*(b[1]-a[1])/(b[0]-a[0]);}
  for(let i=1;i<stations.length;i++)if(q<=stations[i][0]){const a=stations[i-1],b=stations[i],p=stations[Math.max(0,i-2)],n=stations[Math.min(stations.length-1,i+1)],d=b[0]-a[0],t=(q-a[0])/d;
   const slope=(j:number)=>{const l=stations[Math.max(0,j-1)],c=stations[j],r=stations[Math.min(stations.length-1,j+1)];if(j===0)return (r[1]-c[1])/(r[0]-c[0]);if(j===stations.length-1)return (c[1]-l[1])/(c[0]-l[0]);const dl=(c[1]-l[1])/(c[0]-l[0]),dr=(r[1]-c[1])/(r[0]-c[0]);return dl*dr<=0?0:2*dl*dr/(dl+dr);};const m0=slope(i-1),m1=slope(i);return (2*t*t*t-3*t*t+1)*a[1]+(t*t*t-2*t*t+t)*d*m0+(-2*t*t*t+3*t*t)*b[1]+(t*t*t-t*t)*d*m1;}
  const a=stations[stations.length-2],b=stations[stations.length-1];return b[1]+(q-b[0])*(b[1]-a[1])/(b[0]-a[0]);
}
export function sideX(z:number,y:number):number {
  const t=(y-.21)/(topAt(z)-.21);
  const inset=smoothProfile([[0,.047],[.12,.028],[.35,.008],[.54,0],[.68,.006],[.83,.013],[.93,.034],[1,.075]],t);
  let flare=0;
  for(const axle of [-1.38,1.38]){const r=Math.hypot(z-axle,y-.32);flare=Math.max(flare,.014*Math.exp(-Math.pow((r-.405)/.075,2)));}
  return widthAt(z)-inset+flare;
}
export function cabinX(y:number):number{return interp([[.87,.815],[.93,.803],[1.12,.733],[1.27,.664],[1.32,.643]],y);}
export function archBottom(z:number):number {
  let y=.21;
  for(const axle of [-1.38,1.38]){const dz=z-axle;if(Math.abs(dz)<.365)y=Math.max(y,.32+Math.sqrt(.365*.365-dz*dz));}
  return y;
}
export function surfaceMesh(parent:THREE.Object3D,name:string,points:THREE.Vector3[],indices:number[],material:THREE.Material,outward:THREE.Vector3):THREE.Mesh {
  const a=new THREE.Vector3(),b=new THREE.Vector3(),normal=new THREE.Vector3();
  const valid:number[]=[];for(let i=0;i<indices.length;i+=3){a.subVectors(points[indices[i+1]],points[indices[i]]);b.subVectors(points[indices[i+2]],points[indices[i]]);if(a.cross(b).lengthSq()>1e-16)valid.push(indices[i],indices[i+1],indices[i+2]);}indices=valid;
  for(let i=0;i<indices.length;i+=3){a.subVectors(points[indices[i+1]],points[indices[i]]);b.subVectors(points[indices[i+2]],points[indices[i]]);normal.add(a.cross(b));}
  if(normal.dot(outward)<0)for(let i=0;i<indices.length;i+=3){[indices[i+1],indices[i+2]]=[indices[i+2],indices[i+1]];}
  const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(points.flatMap(p=>p.toArray()),3));g.setIndex(indices);g.computeVertexNormals();
  const normals=g.getAttribute('normal');for(let i=0;i<normals.count;i++)if(Math.hypot(normals.getX(i),normals.getY(i),normals.getZ(i))<.5)normals.setXYZ(i,outward.x,outward.y,outward.z);
  const mesh=new THREE.Mesh(g,material);mesh.name=name;mesh.castShadow=true;mesh.receiveShadow=true;parent.add(mesh);return mesh;
}
export function grid(parent:THREE.Object3D,name:string,rows:THREE.Vector3[][],material:THREE.Material,outward:THREE.Vector3):THREE.Mesh {
  const n=rows[0].length,ix:number[]=[];for(let r=0;r<rows.length-1;r++)for(let c=0;c<n-1;c++){const a=r*n+c;ix.push(a,a+1,a+n+1,a,a+n+1,a+n);}
  return surfaceMesh(parent,name,rows.flat(),ix,material,outward);
}
export function polygon(parent:THREE.Object3D,name:string,loop:P2[],map:Map2,material:THREE.Material,outward:THREE.Vector3,subdivide=2):THREE.Mesh {
  // Clip a regular surface grid to the silhouette. This keeps narrow ear-cut
  // triangles from carrying unrelated panel normals across a whole fender.
  const minZ=Math.min(...loop.map(p=>p[0])),maxZ=Math.max(...loop.map(p=>p[0]));
  const minY=Math.min(...loop.map(p=>p[1])),maxY=Math.max(...loop.map(p=>p[1]));
  const step=subdivide>=3?.028:.045;
  const nz=Math.max(1,Math.ceil((maxZ-minZ)/step)),ny=Math.max(1,Math.ceil((maxY-minY)/step));
  const triangles:[THREE.Vector2,THREE.Vector2,THREE.Vector2][]=[];
  function clip(poly:P2[],axis:0|1,value:number,sign:number):P2[]{
    const result:P2[]=[];for(let i=0;i<poly.length;i++){const a=poly[i],b=poly[(i+1)%poly.length],da=(a[axis]-value)*sign,db=(b[axis]-value)*sign;
      if(da>=-1e-9)result.push(a);if((da>0&&db<0)||(da<0&&db>0)){const t=da/(da-db);result.push([lerp(a[0],b[0],t),lerp(a[1],b[1],t)]);}}
    return result;
  }
  for(let iz=0;iz<nz;iz++)for(let iy=0;iy<ny;iy++){
    let cell=loop;cell=clip(cell,0,lerp(minZ,maxZ,iz/nz),1);cell=clip(cell,0,lerp(minZ,maxZ,(iz+1)/nz),-1);
    cell=clip(cell,1,lerp(minY,maxY,iy/ny),1);cell=clip(cell,1,lerp(minY,maxY,(iy+1)/ny),-1);
    const contour=cell.map(p=>new THREE.Vector2(...p));
    for(const t of THREE.ShapeUtils.triangulateShape(contour,[]))triangles.push(t.map(i=>contour[i]) as [THREE.Vector2,THREE.Vector2,THREE.Vector2]);
  }
  const lookup=new Map<string,number>(),points:THREE.Vector3[]=[],params:THREE.Vector2[]=[],ix:number[]=[];
  for(const tri of triangles)for(const p of tri){const key=p.x.toFixed(7)+','+p.y.toFixed(7);let i=lookup.get(key);if(i===undefined){i=points.length;lookup.set(key,i);points.push(map(p.x,p.y));params.push(p);}ix.push(i);}
  const mesh=surfaceMesh(parent,name,points,ix,material,outward);
  // Evaluate the parent surface normal, independent of the aperture triangulation.
  const ns=params.flatMap(p=>{const h=.0002,du=map(p.x+h,p.y).sub(map(p.x-h,p.y)),dv=map(p.x,p.y+h).sub(map(p.x,p.y-h));const n=du.cross(dv).normalize();if(n.dot(outward)<0)n.negate();return n.toArray();});
  mesh.geometry.setAttribute('normal',new THREE.Float32BufferAttribute(ns,3));return mesh;
}
export function rounded(loop:P2[],radius=.018,steps=3):P2[]{
  return loop.flatMap((v,i)=>{const p=loop[(i+loop.length-1)%loop.length],n=loop[(i+1)%loop.length];const lp=Math.hypot(p[0]-v[0],p[1]-v[1]),ln=Math.hypot(n[0]-v[0],n[1]-v[1]);const rp=Math.min(.28,radius/lp),rn=Math.min(.28,radius/ln);const a:P2=[lerp(v[0],p[0],rp),lerp(v[1],p[1],rp)],b:P2=[lerp(v[0],n[0],rn),lerp(v[1],n[1],rn)];return Array.from({length:steps+1},(_,j)=>{const t=j/steps;return [(1-t)*(1-t)*a[0]+2*t*(1-t)*v[0]+t*t*b[0],(1-t)*(1-t)*a[1]+2*t*(1-t)*v[1]+t*t*b[1]] as P2;});});
}
export function sampleLoop(loop:P2[],steps=4):P2[]{return loop.flatMap((a,i)=>{const b=loop[(i+1)%loop.length];return Array.from({length:steps},(_,j)=>[lerp(a[0],b[0],j/steps),lerp(a[1],b[1],j/steps)] as P2);});}
export function ring(parent:THREE.Object3D,name:string,outer:P2[],inner:P2[],map:Map2,material:THREE.Material,normal:THREE.Vector3):void {
  const o=sampleLoop(rounded(outer)),i=sampleLoop(rounded(inner,.014));const rows=o.map((p,k)=>[map(...p),map(...i[k])]);rows.push(rows[0]);grid(parent,name,rows,material,normal);
}
export function tube(parent:THREE.Object3D,name:string,points:THREE.Vector3[],radius:number,material:THREE.Material):void {
  const path=new THREE.CurvePath<THREE.Vector3>();for(let i=1;i<points.length;i++)path.add(new THREE.LineCurve3(points[i-1],points[i]));
  const mesh=new THREE.Mesh(new THREE.TubeGeometry(path,Math.max(4,points.length*2),radius,5,false),material);mesh.name=name;parent.add(mesh);
}
