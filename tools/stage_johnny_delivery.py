#!/usr/bin/env python3
"""Stage the recorded Ostend delivery beat after the archived lotto dialogue.

Reproducible scene/parcel authoring. No API key or network access is needed.
The original recordings and archived scene are never changed.
"""
import json
from pathlib import Path
import shlex
import struct
import wave

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets'
OUT = ASSETS / 'audio/dialogue/johnny_lotto/delivery_01'


def site(x, y, z):
    return [18 + .9945218954*x - .1045284633*z, 12+y,
            -12 + .1045284633*x + .9945218954*z]


def write_wav(path, samples):
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(24000)
        out.writeframes(np.rint(samples).astype('<i2').tobytes())


def parcel():
    folder = ASSETS / 'models/props/delivery_box'
    folder.mkdir(parents=True, exist_ok=True)
    # Generated diffuse atlas is a separate source asset; never repaint it here.
    vertices, indices = [], []
    faces = [((1,0,0),(0,0,-1),(0,1,0)),((-1,0,0),(0,0,1),(0,1,0)),
             ((0,1,0),(1,0,0),(0,0,-1)),((0,-1,0),(1,0,0),(0,0,1)),
             ((0,0,1),(1,0,0),(0,1,0)),((0,0,-1),(-1,0,0),(0,1,0))]

    def box(center, half, tile):
        for face_index, (normal, u, v) in enumerate(faces):
            tile = (0,0) if face_index==2 else (1,0) if face_index==3 else (0,1) if face_index>=4 else (1,1)
            n, u, v = map(np.array, (normal, u, v))
            c = np.array(center) + n*np.array(half)
            du, dv = u*abs(np.dot(u, half)), v*abs(np.dot(v, half))
            offset = len(vertices)
            for p, uv in zip((c-du-dv,c+du-dv,c+du+dv,c-du+dv), ((0,0),(1,0),(1,1),(0,1))):
                tex = [.02 + tile[0]*.5 + uv[0]*.45, .02 + tile[1]*.5 + uv[1]*.45]
                tex[1]=1-tex[1]
                vertices.append(struct.pack('<12f', *p, *n, *tex, *u, 1))
            indices.extend([offset+i for i in (0,1,2,0,2,3)])
    box((0,.05,0),(.21,.05,.15),(0,0))
    strings=b'cardboard\0'
    blob=struct.pack('<8I',0x48534D45,2,0,len(vertices),len(indices),1,len(strings),0)
    blob+=b''.join(vertices)+struct.pack('<'+'I'*len(indices),*indices)
    blob+=struct.pack('<4I',0,len(indices),0,0)+strings
    (folder/'flat_box.emesh').write_bytes(blob)


def main():
    parcel()
    rows=(ASSETS/'cutscenes/archive/johnny-opening-lotto-voiced-v3.cutscene').read_text().splitlines()
    assert rows[0]=='APRICOT_CUTSCENE 1'
    shots=[shlex.split(x) for x in rows[3:10]]
    actors=[shlex.split(x) for x in rows[10:12]]
    cues=[shlex.split(x) for x in rows[12:]]
    recorded=json.loads((OUT/'scripts.json').read_text())
    overrides_path=OUT/'voice_overrides.json'
    overrides=json.loads(overrides_path.read_text()) if overrides_path.exists() else {}
    for cue in cues:
        take_id=Path(cue[2]).stem
        if take_id in overrides:
            cue[2]=overrides[take_id]
            with wave.open(str(ASSETS/cue[2])) as w:
                cue[4]=str(w.getnframes()/w.getframerate()+.08)
    old_timing=json.loads((OUT.parent/'take_01/timing.json').read_text())
    gain={entry['speaker']:10**(entry['gain_db']/20) for entry in old_timing['cues']}
    balanced=OUT/'balanced';balanced.mkdir(exist_ok=True)
    # Walking starts during Lou's direction. Pickup happens when he arrives;
    # Johnny replies only after he has taken the box.
    starts=[53.7,65.1,68.0]
    for take,start in zip(recorded,starts):
        if take['id'] in overrides:
            replacement=overrides[take['id']]
            with wave.open(str(ASSETS/replacement)) as w:
                assert (w.getframerate(),w.getsampwidth(),w.getnchannels())==(24000,2,1)
                duration=w.getnframes()/24000
            cues.append(['Johnny' if 'johnny' in take['id'] else 'Lou',take['text'],
                         replacement,str(start),str(duration+.08)])
            continue
        meta=json.loads((OUT/(take['id']+'.json')).read_text())
        assert meta['transcript_matches'] and meta['actual_model']=='gpt-realtime-2.1'
        with wave.open(str(OUT/meta['file'])) as w:
            assert (w.getframerate(),w.getsampwidth(),w.getnchannels())==(24000,2,1)
            audio=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)
        speaker='Johnny' if 'johnny' in take['id'] else 'Lou'
        samples=audio*gain[speaker]
        assert np.max(np.abs(samples))<32767
        write_wav(balanced/meta['file'],samples)
        cues.append([speaker,take['text'],(balanced/meta['file']).relative_to(ASSETS).as_posix(),
                     str(start),str(len(samples)/24000+.08)])
    total=75.6
    # Make room for the delivery beat; preserve all previous dialogue/cuts.
    shots[-1][1]=str(53.4-sum(float(s[1]) for s in shots[:-1]))

    def shot(name,duration,eye,target,lens=56):
        view=[*site(*eye),*site(*target),lens]
        shots.append([name,str(duration),'1',*map(str,view),*map(str,view)])
    shot('08 - Around the counter',7.0,(4.4,2.15,-12.3),(.45,1.15,-9.75),62)
    shot('09 - The delivery box',4.7,(2.5,1.95,-10.9),(.2,1.18,-9.13),57)
    shot('10 - I know the docks',2.8,(1.8,1.8,-10.55),(-.25,1.35,-8.85),54)
    shot('11 - Ask for Devon',7.7,(-1.0,1.8,-10.8),(.35,1.35,-9.0),54)

    # Each key owns a world position, facing, and optional world-space wrists.
    # The parcel trajectory and contacts are shared by both performers.
    box_path=[(0,(.8,1.25,-9.50)),(60.4,(.8,1.25,-9.50)),
              (61.15,(.8,1.40,-9.50)),(61.6,(.50,1.35,-9.38)),
              (61.8,(.33,1.32,-9.25)),(62.05,(.20,1.30,-9.10)),
              (63.15,(.16,1.30,-9.10)),(64.1,(.10,1.25,-9.10)),
              (64.75,(.10,1.25,-8.95)),(total,(.10,1.25,-8.95))]

    def box_at(t):
        for (ta,a),(tb,b) in zip(box_path,box_path[1:]):
            if t<=tb:
                u=max(0,min(1,(t-ta)/(tb-ta)))
                return np.array(a)*(1-u)+np.array(b)*u
        return np.array(box_path[-1][1])

    def key(t,position,yaw,reach=0,grip='lou'):
        b=box_at(t)
        # Rotate the parcel and its edge contacts with Lou's turn.
        degrees=np.interp(t,[0,61.15,61.6,61.8,62.05,total],[0,0,45,70,90,90])
        angle=np.radians(degrees)
        side=np.array([.20*np.cos(angle),0,-.20*np.sin(angle)])
        left=b-side+(0,.04,0);right=b+side+(0,.04,0)
        if grip=='johnny':left,right=right,left
        return [t,*site(*position),yaw,reach,*site(*left),*site(*right)]

    lou=[key(0,(-.4,.15,-11.2),-6),key(53.4,(-.4,.15,-11.2),-6),
         key(53.7,(-.4,.15,-11.2),84),key(55.9,(2.2,.15,-11.2),84),
         key(56.2,(2.2,.15,-11.2),-6),key(58.35,(2.2,.15,-8.95),-6),
         key(58.65,(2.2,.15,-8.95),-96),key(59.85,(.8,.15,-9.10),-96),
         key(60.05,(.8,.15,-9.10),174,0,'pickup'),key(60.4,(.8,.15,-9.10),174,1,'pickup'),
         key(61.15,(.8,.15,-9.10),174,1,'pickup'),
         key(61.6,(.8,.15,-9.10),219,1,'pickup'),key(61.8,(.72,.15,-9.10),244,1),
         key(62.05,(.575,.15,-9.10),264,1),
         key(63.15,(.575,.15,-9.10),264,1),key(64.1,(.575,.15,-9.10),264,0),
         key(64.75,(.575,.15,-9.10),264,0),key(total,(.575,.15,-9.10),264,0)]
    johnny=[key(0,(-.4,.15,-8.75),174,0,'johnny'),key(59.85,(-.4,.15,-8.75),174,0,'johnny'),
            key(61.15,(-.23,.15,-8.95),84,0,'johnny'),key(62.05,(-.23,.15,-8.95),84,0,'johnny'),
            key(63.15,(-.23,.15,-8.95),84,1,'johnny'),key(64.1,(-.23,.15,-8.95),84,1,'johnny'),
            key(64.75,(-.23,.15,-8.95),84,1,'johnny'),key(total,(-.23,.15,-8.95),84,1,'johnny')]
    prop=['Delivery box','models/props/delivery_box/flat_box.emesh','models/props/delivery_box/cardboard_generated.png','',
          *map(str,site(*box_path[0][1])),*map(str,site(*box_path[-1][1])), '-6','-6','.10','0',str(total)]
    actors.append(prop)
    tracks=[johnny,lou,[key(t,p,float(np.interp(t,[0,61.15,61.6,61.8,62.05,total],[-6,-6,39,64,84,84]))) for t,p in box_path]]
    # Retakes need conversational spacing, not the old actor's long holds.
    cursor=1.0
    for index,cue in enumerate(cues[:16]):
        cue[3]=str(round(cursor,3))
        gap=.65 if index==9 else .75 if index==10 else .45 if index==14 else .28
        cursor+=float(cue[4])-.08+gap
    action_start=round(float(cues[15][3])+float(cues[15][4])-.08+.65,3)
    shift=action_start-53.4
    boundaries=[0]+[float(cues[i][3])-.12 for i in (3,6,7,10,12,15)]+[action_start]
    for index,shot_row in enumerate(shots[:7]):
        shot_row[1]=str(round(boundaries[index+1]-boundaries[index],3))
    for cue in cues[16:]:cue[3]=str(round(float(cue[3])+shift,3))
    for track in tracks:
        for actor_key in track:
            if actor_key[0]>0:actor_key[0]=round(actor_key[0]+shift,3)
    total=round(total+shift,3)
    gestures=[[],[],[]]
    def gesture(actor,line,kind,strength=.7,delay=.12,duration=1.8):
        cue=cues[line-1]
        span=max(.3,min(duration,float(cue[4])-delay))
        gestures[actor].append([round(float(cue[3])+delay,3),round(span,3),strength,kind])
    # Explain=0, dismiss=1, shrug=2, self=3, nod=4, glance=5.
    gesture(0,1,5,.65,duration=1.1)
    gesture(0,1,0,.5,delay=.85,duration=1.6)
    gesture(1,2,1,.65,duration=1.35)
    gesture(0,3,2,.7,duration=1.4)
    gesture(1,4,0,.45,duration=2.1)
    gesture(0,5,0,.8,duration=1.8)
    gesture(1,6,2,.4,duration=1.7)
    gesture(0,7,0,.75,duration=2.5)
    gesture(1,8,1,.5,duration=2.2)
    gesture(0,9,5,.65,duration=.75)
    gesture(1,10,4,.5,duration=1.3)
    gesture(0,11,4,.8,delay=.02,duration=.6)
    gesture(1,12,5,.7,duration=1.0)
    gesture(1,12,0,.5,delay=.9,duration=1.7)
    gesture(0,13,3,.85,delay=.05,duration=1.1)
    gesture(1,14,5,.5,delay=.02,duration=.65)
    gesture(0,15,0,.85,duration=3.2)
    gesture(1,16,5,.45,duration=1.0)
    gesture(1,16,4,.5,delay=2,duration=1.4)
    gesture(1,19,4,.55,delay=.7,duration=2.0)
    quote=lambda s:json.dumps(str(s),ensure_ascii=False)
    def encode(tokens,strings):return ' '.join(quote(v) if i<strings else str(v) for i,v in enumerate(tokens))
    output=['APRICOT_CUTSCENE 3',rows[1],f'{len(shots)} 3 {len(cues)}']
    output.extend(encode(s,1) for s in shots)
    for actor,track,acting in zip(actors,tracks,gestures):
        actor[-1]=str(total)
        walk='models/characters/psx_pack/animations/walk.eanim' if actor is actors[1] else ''
        output.append(encode(actor,4)+' '+quote(walk)+' '+str(len(track))+' '+str(len(acting)))
        output.extend(' '.join(str(round(v,6)) for v in k) for k in track)
        output.extend(' '.join(map(str,g)) for g in acting)
    output.extend(encode(c,3) for c in cues)
    (ASSETS/'cutscenes/johnny-opening.cutscene').write_text('\n'.join(output)+'\n')
    reel=np.zeros(round(total*24000))
    for cue in cues:
        with wave.open(str(ASSETS/cue[2])) as w:
            a=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2')
        start=round(float(cue[3])*24000)
        assert len(a)/24000<=float(cue[4])
        reel[start:start+len(a)]+=a
    assert np.max(np.abs(reel))<32767
    write_wav(OUT/'johnny-lou-lotto-and-delivery.wav',reel)
    (OUT/'timing.json').write_text(json.dumps(dict(duration_seconds=total,
        action_time_shift=shift,
        walk=[53.7+shift,59.85+shift],pickup=[60.05+shift,61.15+shift],handoff=[62.05+shift,64.75+shift],
        cues=[dict(speaker=c[0],text=c[1],audio=c[2],start=float(c[3]),duration=float(c[4])) for c in cues]),indent=2)+'\n')
    print(f'Staged {len(shots)} shots, 3 actors/props, {len(cues)} voices, {total} seconds.')


if __name__=='__main__':
    main()
