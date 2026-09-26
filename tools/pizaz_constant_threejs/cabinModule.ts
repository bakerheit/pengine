import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';
import type {PizazMaterials} from './moduleTypes.js';
import {BELT,FRONT_SEAM,B_FRONT,B_REAR,rearDoorEdge,sideX,cabinX,lerp,grid,polygon,rounded,ring,tube,sampleLoop,type P2,type Map2} from './surface.js';
const UP=new THREE.Vector3(0,1,0);
function box(parent:THREE.Object3D,name:string,size:[number,number,number],at:[number,number,number],material:THREE.Material,r=.008):THREE.Mesh{
 const mesh=new THREE.Mesh(new RoundedBoxGeometry(...size,2,Math.min(r,...size.map(x=>x*.3))),material);mesh.name=name;mesh.position.set(...at);mesh.castShadow=true;parent.add(mesh);return mesh;
}
function scaleLoop(loop:P2[],s:number):P2[]{const c=loop.reduce((a,p)=>[a[0]+p[0]/loop.length,a[1]+p[1]/loop.length] as P2,[0,0] as P2);return loop.map(p=>[lerp(c[0],p[0],s),lerp(c[1],p[1],s)]);}
function windowPanel(parent:THREE.Object3D,name:string,outer:P2[],inner:P2[],map:Map2,side:number,m:PizazMaterials):void{
 const n=new THREE.Vector3(side,0,0);
 ring(parent,`${name}_painted_frame`,outer,inner,map,m.paint,n);
 ring(parent,`${name}_seal`,scaleLoop(inner,1.06),inner,(z,y)=>map(z,y).add(new THREE.Vector3(side*.001,0,0)),m.trim,n);
 polygon(parent,`${name}_glass`,rounded(scaleLoop(inner,1.02),.014),(z,y)=>map(z,y).add(new THREE.Vector3(-side*.009,0,0)),m.glass,n,2);
 const o=sampleLoop(rounded(outer)),i=sampleLoop(rounded(inner,.014));
 for(const [label,loop] of [['outer',o],['inner',i]] as const){const rows=[...loop,loop[0]].map(p=>[map(...p),map(...p).add(new THREE.Vector3(-side*.025,0,0))]);grid(parent,`${name}_${label}_frame_thickness`,rows,m.darkPaint,n);}
}
function solidDoor(parent:THREE.Group,name:string,outline:P2[],map:Map2,side:number,m:PizazMaterials):void{
 const normal=new THREE.Vector3(side,0,0),loop=rounded(outline,.013);
 polygon(parent,`${name}_pressed_outer_skin`,loop,map,m.paint,normal,3);
 polygon(parent,`${name}_inner_shell`,loop,(z,y)=>map(z,y).add(new THREE.Vector3(-side*.047,0,0)),m.darkPaint,normal.clone().negate(),3);
 const edge=sampleLoop(loop,8);
 const rows=[...edge,edge[0]].map(p=>[map(...p),map(...p).add(new THREE.Vector3(-side*.047,0,0))]);grid(parent,`${name}_edge_returns`,rows,m.paint,normal);
 polygon(parent,`${name}_shaped_inner_card`,rounded(scaleLoop(outline,.82),.025),(z,y)=>map(z,y).add(new THREE.Vector3(-side*.055,0,0)),m.upholstery,normal.clone().negate(),2);
}
function addDoor(parent:THREE.Group,side:number,front:boolean,m:PizazMaterials):void{
 const name=`${side>0?'driver':'passenger'}_${front?'front':'rear'}_door`,lead=front?FRONT_SEAM:B_REAR;
 const pivot=new THREE.Vector3(side*(sideX(lead,.65)-.03),.57,lead-.005);
 const door=new THREE.Group();door.name=name;door.position.copy(pivot);door.userData={hingeAxis:'Y',hingeAt:pivot.toArray(),opensOutward:-side};parent.add(door);
 const skin=(z:number,y:number)=>new THREE.Vector3(side*sideX(z,y),y,z).sub(pivot);
 const glass=(z:number,y:number)=>new THREE.Vector3(side*cabinX(y),y,z).sub(pivot);
 const outline:P2[]=front?[[B_FRONT+.003,.267],[FRONT_SEAM-.003,.267],[FRONT_SEAM-.003,BELT],[B_FRONT+.003,BELT]]:[[rearDoorEdge(.267)+.003,.267],[B_REAR-.003,.267],[B_REAR-.003,BELT],[rearDoorEdge(BELT)+.003,BELT],...Array.from({length:12},(_,i)=>{const y=lerp(BELT,.267,(i+1)/12);return [rearDoorEdge(y)+.003,y] as P2;})];
 solidDoor(door,name,outline,skin,side,m);
 if(front)windowPanel(door,'front_window',[[B_FRONT+.003,.865],[.836,.865],[.208,1.27],[-.253,1.27]],[[B_FRONT+.035,.895],[.75,.895],[.197,1.239],[-.22,1.239]],glass,side,m);
 else windowPanel(door,'rear_window',[[-1.081,.865],[B_REAR-.003,.865],[-.295,1.27],[-.788,1.27]],[[-1.035,.898],[B_REAR-.037,.898],[-.327,1.238],[-.771,1.238]],glass,side,m);
 const hz=front?-.105:-.968,hy=.785,hx=side*(sideX(hz,hy)+.003),p=new THREE.Vector3(hx,hy,hz).sub(pivot);
 box(door,'recessed_handle',[.014,.040,.115],p.toArray() as [number,number,number],m.trim,.009);
 box(door,'handle_pull',[.015,.018,.091],[p.x+side*.002,p.y+.004,p.z],m.darkPaint,.004);
 const a=front?B_FRONT+.02:rearDoorEdge(.55)+.02,b=lead-.02;
 tube(door,'straight_side_molding',Array.from({length:18},(_,i)=>skin(lerp(a,b,i/17),.55).add(new THREE.Vector3(side*.0015,0,0))),.004,m.darkPaint);
 const arm=skin(front?.24:-.61,.60).add(new THREE.Vector3(-side*.092,0,0));box(door,'interior_armrest',[.08,.045,.28],arm.toArray() as [number,number,number],m.trim,.014);
 if(front){
  const base=glass(.73,.90),stem=glass(.70,.92).add(new THREE.Vector3(side*.09,0,0));tube(door,'mirror_mount',[base,stem],.023,m.trim);
  const center=new THREE.Vector3(side*.925,.925,.725).sub(pivot),housing=box(door,'mirror_shell',[.15,.092,.15],center.toArray() as [number,number,number],m.paint,.025);housing.rotation.y=side*.16;
  box(door,'mirror_reflector',[.124,.066,.006],[center.x,center.y,center.z-.077],m.silver,.008);
 }
}
function addScreen(parent:THREE.Group,front:boolean,m:PizazMaterials):void{
 const name=front?'windshield':'rear_screen',loZ=front?.88:-1.48,hiZ=front?.25:-.84,loY=front?.87:.89;
 const map=(u:number,v:number)=>{const y=lerp(loY,1.285,v);return new THREE.Vector3(u*lerp(cabinX(loY),cabinX(1.285),v),y+lerp(.028,.012,v)*(1-u*u),lerp(loZ,hiZ,v)+(front?1:-1)*.025*(1-u*u));};
 const normal=new THREE.Vector3(0,.3,front?1:-1);
 const outer:P2[]=[[-1,0],[1,0],[1,1],[-1,1]],inner:P2[]=[[-.955,.06],[.955,.06],[.95,.945],[-.95,.945]];
 ring(parent,`${name}_body_surround`,outer,inner,map,m.paint,normal);
 ring(parent,`${name}_rubber_seal`,scaleLoop(inner,1.02),inner,map,m.trim,normal);
 polygon(parent,name,rounded(inner,.02),(u,v)=>map(u,v).add(new THREE.Vector3(0,-.003,front?-.004:.004)),m.glass,normal,3);
 if(front){for(const side of [-1,1])tube(parent,`wiper_${side}`,[map(side*.06,.09),map(side*.45,.12),map(side*.79,.14)],.004,m.trim);}else{
  for(let i=1;i<7;i++){const v=.10+i*.11;tube(parent,`defroster_${i}`,Array.from({length:12},(_,j)=>map(lerp(-.89,.89,j/11),v)),.0008,m.darkPaint);}
 }
}
function fixedStamp(parent:THREE.Group,name:string,loop:P2[],map:Map2,material:THREE.Material,normal:THREE.Vector3):void {
 polygon(parent,name,loop,map,material,normal,2);
 const inner:Map2=(z,y)=>map(z,y).addScaledVector(normal,-.025);
 polygon(parent,`${name}_inside`,loop,inner,material,normal.clone().negate(),2);
 const edge=sampleLoop(loop,12);grid(parent,`${name}_returns`,[...edge,edge[0]].map(p=>[map(...p),inner(...p)]),material,normal);
}
function addRoof(parent:THREE.Group,m:PizazMaterials):void{
 const rows=Array.from({length:17},(_,i)=>{const t=i/16,z=lerp(-.84,.25,t),endBow=lerp(-.025,.025,t),crown=.012+.023*Math.sin(Math.PI*t);return Array.from({length:17},(_,j)=>{const u=-1+j/8;return new THREE.Vector3(u*cabinX(1.285),1.285+crown*(1-u*u),z+endBow*(1-u*u));});});grid(parent,'continuous_roof_skin',rows,m.paint,UP);grid(parent,'roof_headlining',rows.map(row=>row.map(p=>p.clone().add(new THREE.Vector3(0,-.025,0)))),m.upholstery,UP.clone().negate());
 for(const side of [-1,1]){
  const map=(z:number,y:number)=>new THREE.Vector3(side*cabinX(y),y,z),n=new THREE.Vector3(side,0,0);
  fixedStamp(parent,`roof_side_rail_${side}`,[[-.84,1.285],[.25,1.285],[.208,1.27],[-.788,1.27]],map,m.paint,n);
  fixedStamp(parent,`A_pillar_stamp_${side}`,[[.836,.87],[.88,.87],[.25,1.285],[.208,1.27]],map,m.paint,n);
  fixedStamp(parent,`B_pillar_${side}`,[[B_FRONT,.87],[B_REAR,.87],[-.295,1.27],[-.253,1.27]],map,m.trim,n);
  windowPanel(parent,`fixed_quarter_${side}`,[[-1.085,.87],[-1.48,.89],[-.84,1.285],[-.788,1.27]],[[-1.13,.914],[-1.385,.926],[-.868,1.233],[-.855,1.229]],map,side,m);
 }
}
function interior(parent:THREE.Group,m:PizazMaterials):void{
 box(parent,'dashboard',[1.44,.13,.32],[0,.75,.63],m.trim,.035);
 box(parent,'center_console',[.23,.17,.62],[0,.39,.05],m.trim,.025);
 for(const side of [-1,1])for(const [row,z] of [['front',.15],['rear',-.67]] as const){
  box(parent,`${side}_${row}_seat_cushion`,[.47,.12,.43],[side*.38,.39,z],m.upholstery,.048);
  const back=box(parent,`${side}_${row}_seat_back`,[.45,.47,.13],[side*.38,.66,z-.17],m.upholstery,.05);back.rotation.x=-.14;
  box(parent,`${side}_${row}_headrest`,[.26,.14,.105],[side*.38,.96,z-.205],m.upholstery,.035);
 }
 const wheel=new THREE.Mesh(new THREE.TorusGeometry(.155,.012,6,24),m.trim);wheel.name='driver_steering_wheel';wheel.position.set(.38,.84,.40);wheel.rotation.x=-.30;parent.add(wheel);
 box(parent,'steering_hub',[.085,.07,.032],[.38,.84,.40],m.trim,.016);
 for(let i=0;i<3;i++){const a=i*Math.PI*2/3;tube(parent,`steering_spoke_${i}`,[new THREE.Vector3(.38,.84,.40),new THREE.Vector3(.38+.14*Math.cos(a),.84+.14*Math.sin(a),.40)],.008,m.trim);}
 box(parent,'rear_parcel_shelf',[1.37,.03,.30],[0,.845,-1.27],m.darkPaint,.01);
 box(parent,'rearview_mirror',[.22,.065,.024],[0,1.18,.32],m.trim,.012);
}
export function addPizazCabin(parent:THREE.Group,m:PizazMaterials):void{
 const cabin=new THREE.Group();cabin.name='cabin_fixed';parent.add(cabin);
 addRoof(cabin,m);addScreen(cabin,true,m);addScreen(cabin,false,m);interior(cabin,m);
 for(const side of [-1,1]){addDoor(parent,side,true,m);addDoor(parent,side,false,m);}
}
