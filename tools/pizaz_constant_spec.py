"""Runtime derivative of the approved PIZAZ Constant; metres, +X left/+Y up/+Z nose."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/pizaz_constant'
TEXTURE=ROOT/'assets/textures/vehicles/pizaz_constant/body.png'
# Upper half reserved for respray-safe paint. All lenses and trim below it.
REGIONS={'paint':(0,0,256,128),'darkPaint':(0,128,64,192),'trim':(64,128,128,192),
 'upholstery':(128,128,192,192),'glass':(192,128,256,192),'silver':(0,192,64,256),
 'lamp':(64,192,128,256),'amber':(128,192,192,224),'red':(128,224,192,256),'rubber':(192,192,256,256)}
COLORS={'paint':(100,31,50),'darkPaint':(50,30,39),'trim':(21,23,25),
 'upholstery':(48,50,54),'glass':(52,71,80),'silver':(179,182,182),
 'lamp':(174,185,187),'amber':(213,138,56),'red':(169,24,42),'rubber':(17,19,21)}
WHEELS={'x':.78,'y':.32,'front_z':1.38,'rear_z':-1.38,'radius':.32}
DRIVER={'hinge':(.85441452,.57,.835),'seat':(.38,.45,.15),'open_radians':-1.04}
