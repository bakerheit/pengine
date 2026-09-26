import * as THREE from 'three';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';
import type {PizazMaterials} from './moduleTypes.js';
import {tube,topAt,sideX,type Map2} from './surface.js';

type V3=[number,number,number];
function box(parent:THREE.Object3D,name:string,size:V3,at:V3,material:THREE.Material,r=.008):THREE.Mesh {
  const geometry=r>=.014?new RoundedBoxGeometry(...size,1,Math.min(r,...size.map(v=>v*.25))):new THREE.BoxGeometry(...size);
  const mesh=new THREE.Mesh(geometry,material);mesh.name=name;mesh.position.set(...at);parent.add(mesh);return mesh;
}
function face(parent:THREE.Object3D,name:string,size:[number,number],at:V3,material:THREE.Material):THREE.Mesh {
  const mesh=new THREE.Mesh(new THREE.PlaneGeometry(...size),material);
  mesh.name=name;mesh.position.set(...at);mesh.rotation.y=Math.PI;parent.add(mesh);return mesh;
}
function rod(parent:THREE.Object3D,name:string,a:V3,b:V3,r:number,material:THREE.Material):void {
  tube(parent,name,[new THREE.Vector3(...a),new THREE.Vector3(...b)],r,material);
}
function fitBelowCowl(mesh:THREE.Mesh):void {
  const positions=mesh.geometry.getAttribute('position');
  for(let i=0;i<positions.count;i++) {
    const x=positions.getX(i)+mesh.position.x,z=Math.max(.88,positions.getZ(i)+mesh.position.z);
    const top=topAt(z),width=sideX(z,top);
    const limit=top+.028*(1-(x/width)**2)-.003;
    positions.setY(i,Math.min(positions.getY(i),limit-mesh.position.y));
  }
  mesh.geometry.computeVertexNormals();
}
function vent(parent:THREE.Object3D,name:string,x:number,y:number,z:number,width:number,m:PizazMaterials):void {
  box(parent,name+'_housing',[width+.022,.065,.025],[x,y,z],m.leather,.007);
  box(parent,name+'_recess',[width,.046,.004],[x,y,z-.015],m.trim,0);
  for(let i=0;i<4;i++)box(parent,name+'_louver_'+i,[width-.012,.004,.009],[x,y-.015+i*.010,z-.020],m.leather,0);
}
function frontSeat(parent:THREE.Group,side:number,m:PizazMaterials):void {
  const x=side*.38,z=.15,label=side>0?'driver':'passenger';
  box(parent,label+'_seat_base',[.47,.12,.43],[x,.39,z],m.leather,.033);
  box(parent,label+'_seat_cloth',[.32,.022,.32],[x,.449,z+.005],m.upholstery,.008);
  for(const s of [-1,1]) {
    box(parent,label+'_cushion_bolster_'+s,[.065,.065,.36],[x+s*.194,.431,z],m.leather,.025);
    box(parent,label+'_seat_runner_'+s,[.030,.055,.39],[x+s*.16,.297,z],m.trim,.005);
  }
  const back=new THREE.Group();back.name=label+'_seat_back';back.position.set(x,.66,z-.17);back.rotation.x=-.14;parent.add(back);
  box(back,'seat_back_shell',[.45,.47,.13],[0,0,0],m.leather,.028);
  box(back,'seat_back_cloth',[.32,.345,.024],[0,.012,.068],m.upholstery,.009);
  for(const s of [-1,1])box(back,'back_bolster_'+s,[.065,.40,.060],[s*.187,0,.055],m.leather,.02);
  for(const s of [-1,1])rod(parent,label+'_headrest_post_'+s,[x+s*.071,.875,z-.195],[x+s*.071,.94,z-.205],.006,m.silver);
  box(parent,label+'_headrest',[.26,.14,.105],[x,.96,z-.205],m.upholstery,.027);
  box(parent,label+'_seatbelt_buckle',[.033,.060,.040],[x-side*.27,.435,z-.105],m.trim,.005);
  box(parent,label+'_belt_release',[.024,.006,.028],[x-side*.27,.466,z-.105],m.leather,.002);
  for(const s of [-1,1])rod(parent,label+'_cushion_seam_'+s,[x+s*.14,.464,z-.115],[x+s*.14,.464,z+.12],.0015,m.trim);
}

export function addPizazInterior(parent:THREE.Group,m:PizazMaterials):void {
  const cabin=new THREE.Group();cabin.name='sealed_interior';parent.add(cabin);
  // Continuous opaque cabin enclosure. Its front ends behind the swept tire
  // envelope; no full-width floor is carried through either axle opening.
  box(cabin,'interior_floor',[1.57,.050,1.89],[0,.255,.005],m.carpet,0);
  fitBelowCowl(box(cabin,'front_firewall',[1.60,.620,.050],[0,.570,.940],m.carpet,0));
  box(cabin,'rear_cabin_bulkhead',[1.57,.625,.045],[0,.5525,-.960],m.carpet,0);
  for(const side of [-1,1]) {
    box(cabin,'inner_sill_'+side,[.090,.095,1.87],[side*.790,.290,.005],m.trim,.007);
    box(cabin,'front_kick_panel_'+side,[.048,.470,.145],[side*.766,.532,.859],m.carpet,.006);
    box(cabin,'rear_quarter_lining_'+side,[.058,.285,.195],[side*.738,.722,-1.045],m.leather,.010);
    box(cabin,'front_carpet_mat_'+side,[.51,.013,.43],[side*.405,.287,.580],m.carpet,.010);
    box(cabin,'rear_carpet_mat_'+side,[.51,.013,.33],[side*.405,.287,-.340],m.carpet,.010);
  }
  box(cabin,'center_floor_tunnel',[.21,.125,1.61],[0,.330,-.015],m.carpet,.035);
  box(cabin,'rear_parcel_shelf',[1.49,.035,.54],[0,.845,-1.205],m.carpet,.008);

  // The padded dash meets the firewall/cowl. Analog instruments sit behind a
  // real hood; only their face uses the dedicated instrument atlas rectangle.
  box(cabin,'dashboard',[1.57,.185,.460],[0,.765,.665],m.leather,.035);
  fitBelowCowl(box(cabin,'dash_cowl_seal',[1.585,.030,.095],[0,.860,.877],m.trim,.007));
  box(cabin,'instrument_hood',[.50,.170,.220],[.38,.865,.548],m.trim,.021);
  face(cabin,'analog_instruments',[.439,.120],[.38,.863,.434],m.instruments);
  box(cabin,'driver_knee_panel',[.53,.130,.100],[.405,.609,.646],m.leather,.014);
  box(cabin,'passenger_lower_dash',[.64,.185,.170],[-.425,.635,.619],m.leather,.020);
  box(cabin,'glovebox_seam',[.51,.099,.010],[-.425,.610,.529],m.trim,.008);
  box(cabin,'glovebox_lid',[.497,.086,.015],[-.425,.610,.520],m.leather,.009);
  box(cabin,'glovebox_latch',[.075,.018,.020],[-.425,.632,.507],m.trim,.004);
  vent(cabin,'driver_vent',.705,.798,.427,.094,m);
  vent(cabin,'passenger_vent',-.695,.798,.427,.095,m);
  vent(cabin,'center_vent',-.070,.798,.427,.175,m);
  for(const side of [-1,1]) {
    const grille=box(cabin,'demister_grille_'+side,[.37,.007,.052],[side*.385,.862,.855],m.trim,.003);
    for(let i=0;i<6;i++)box(grille,'demister_slot_'+i,[.043,.006,.033],[-.15+i*.06,.004,0],m.leather,0);
  }
  box(cabin,'center_stack',[.232,.258,.122],[-.015,.564,.520],m.trim,.012);
  face(cabin,'cassette_radio',[.194,.072],[-.015,.627,.455],m.radio);
  face(cabin,'manual_heater_controls',[.194,.063],[-.015,.554,.455],m.hvac);
  box(cabin,'ashtray',[.166,.038,.025],[-.015,.496,.448],m.leather,.004);
  box(cabin,'ashtray_pull',[.06,.006,.012],[-.015,.503,.431],m.silver,.002);

  box(cabin,'center_console',[.225,.172,.660],[0,.390,.055],m.leather,.017);
  box(cabin,'shifter_boot',[.12,.027,.16],[0,.487,.243],m.trim,.012);
  rod(cabin,'automatic_shift_lever',[0,.494,.255],[0,.604,.223],.011,m.trim);
  box(cabin,'shift_grip',[.087,.038,.044],[0,.610,.220],m.leather,.010);
  const selector=face(cabin,'PRND_selector',[.046,.116],[.077,.480,.238],m.selector);selector.rotation.set(-Math.PI/2,0,0);
  box(cabin,'console_storage',[.148,.010,.130],[0,.481,-.070],m.trim,.005);
  rod(cabin,'handbrake_handle',[.101,.438,-.070],[.101,.493,.075],.013,m.trim);
  rod(cabin,'steering_column',[.38,.84,.40],[.38,.637,.78],.031,m.trim);
  const steering=new THREE.Group();steering.name='driver_steering_assembly';steering.position.set(.38,.84,.40);steering.rotation.x=-.30;cabin.add(steering);
  const rim=new THREE.Mesh(new THREE.TorusGeometry(.155,.013,6,28),m.trim);rim.name='driver_steering_wheel';steering.add(rim);
  box(steering,'steering_hub',[.087,.072,.036],[0,0,0],m.leather,.012);
  for(let i=0;i<3;i++){const a=i*Math.PI*2/3;rod(steering,'steering_spoke_'+i,[0,0,0],[.142*Math.cos(a),.142*Math.sin(a),0],.009,m.trim);}
  rod(cabin,'indicator_stalk',[.32,.77,.51],[.19,.79,.47],.005,m.trim);
  box(cabin,'brake_pedal',[.105,.063,.022],[.315,.365,.754],m.rubber,.003);
  box(cabin,'accelerator',[.048,.094,.020],[.492,.367,.779],m.rubber,.003);
  for(const side of [-1,1])frontSeat(cabin,side,m);

  box(cabin,'rear_bench_base',[1.42,.135,.43],[0,.403,-.663],m.leather,.030);
  box(cabin,'rear_bench_cloth',[1.25,.021,.325],[0,.477,-.643],m.upholstery,.012);
  const back=box(cabin,'rear_bench_back',[1.42,.43,.12],[0,.684,-.864],m.leather,.025);back.rotation.x=-.12;
  const insert=box(cabin,'rear_back_cloth',[1.25,.325,.022],[0,.700,-.793],m.upholstery,.009);insert.rotation.x=-.12;
  for(const side of [-1,1]) {
    box(cabin,'rear_headrest_'+side,[.28,.14,.108],[side*.41,.964,-.909],m.upholstery,.026);
    box(cabin,'rear_belt_buckle_'+side,[.035,.032,.050],[side*.205,.493,-.745],m.trim,.004);
    box(cabin,'rear_speaker_'+side,[.24,.011,.09],[side*.43,.869,-1.203],m.speaker,.005);
    rod(cabin,'front_belt_webbing_'+side,[side*.765,1.115,-.229],[side*.745,.405,-.183],.010,m.trim);
    box(cabin,'sun_visor_'+side,[.42,.065,.10],[side*.370,1.225,.254],m.leather,.010);
  }
  box(cabin,'rearview_mirror',[.22,.065,.024],[0,1.18,.32],m.trim,.009);
  face(cabin,'rearview_reflector',[.198,.047],[0,1.18,.306],m.silver);
  rod(cabin,'rearview_stem',[0,1.205,.32],[0,1.266,.258],.006,m.trim);
  box(cabin,'ceiling_lamp',[.14,.012,.085],[0,1.274,-.32],m.leather,.005);
}

export function addPizazDoorInterior(door:THREE.Group,skin:Map2,side:number,front:boolean,m:PizazMaterials):void {
  const z=front?.24:-.61;
  const at=(zz:number,y:number,inset:number):V3=>skin(zz,y).add(new THREE.Vector3(-side*inset,0,0)).toArray() as V3;
  box(door,'inner_door_pocket',[.075,.115,front?.43:.30],at(z,.405,.085),m.leather,.014);
  box(door,'door_pocket_mouth',[.062,.012,front?.36:.24],at(z,.467,.094),m.trim,.004);
  box(door,'interior_armrest',[.080,.052,front?.31:.24],at(z,.605,.102),m.leather,.012);
  box(door,'window_switch_base',[.052,.008,.105],at(z+.07,.635,.111),m.trim,.002);
  for(let i=0;i<(front?2:1);i++)box(door,'window_rocker_'+i,[.016,.008,.034],at(z+.07+i*.039,.642,.111),m.silver,.001);
  box(door,'release_recess',[.017,.040,.113],at(z+.15,.722,.071),m.trim,.005);
  box(door,'interior_release',[.022,.012,.075],at(z+.153,.724,.083),m.silver,.004);
  box(door,'door_grab_handle',[.020,.060,.11],at(z-.083,.633,.120),m.trim,.007);
  if(front){
    const panel=new THREE.Mesh(new THREE.PlaneGeometry(.14,.12),m.speaker);panel.name='door_speaker_grille';
    panel.position.set(...at(.628,.450,.068));panel.rotation.y=-side*Math.PI/2;door.add(panel);
  }
}
