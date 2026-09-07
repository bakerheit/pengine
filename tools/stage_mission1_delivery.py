#!/usr/bin/env python3
"""Rebuild the Mission 1 arrival scene; never modifies Johnny's opening.

Optional takes live under assets/audio/dialogue/mission1_delivery/take_01.
When takes exist, their measured lengths drive the edit and handoff timing.
voice_overrides.json selects preserved, versioned character retakes.
"""
import json
import array
from pathlib import Path
import wave

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets'
TAKES = ASSETS / 'audio/dialogue/mission1_delivery/take_01'
CAST = 'models/characters/psx_pack/'
SCRIPT = [
    ('johnny_01', 'Johnny', 'cedar', "Devon? Lou sent me. Got your package."),
    ('devon_01', 'Devon', 'verse', "did anyone follow you?"),
    ('johnny_02', 'Johnny', 'cedar', "No. Why?"),
    ('devon_02', 'Devon', 'verse', "Just checking. Tell Lou we're square."),
]


def site(x, y, z):
    # kMarlinDockSite: origin (-2041,-600), cos=1, sin=0, ground=0.
    return [-2041+x, y, -600+z]


def main():
    cue_rows, manifest = [], []
    overrides_path=TAKES.parent/'voice_overrides.json'
    overrides=json.loads(overrides_path.read_text()) if overrides_path.exists() else {}
    cursor = 1.1
    for ident, speaker, voice, line in SCRIPT:
        take = ASSETS / overrides[ident] if ident in overrides else TAKES / (ident + '.wav')
        span = max(1.7, len(line.split()) / 2.8)
        audio = ''
        if take.exists():
            with wave.open(str(take)) as wav:
                span = wav.getnframes()/wav.getframerate()+.12
            audio = take.relative_to(ASSETS).as_posix()
        cue_rows.append([speaker, line, audio, round(cursor,3), round(span,3)])
        manifest.append(dict(id=ident,kind='Cutscene',title='Mission 1 package arrival',
            character=speaker,voice=voice,text=line,
            direction=('Young adult American man, light East Coast edge, casual and dry. Speaking to Devon at arm length in a bait shop. Natural pace, no theatrical pauses.' if speaker=='Johnny' else
                       'Adult American man who runs a bait shop. Guarded and watchful, lightly rough everyday voice. The first question is quiet and serious; relax slightly once the package is in hand. Speaking to a young courier at arm length. Natural pace, no growl or drawl.')))
        if ident in overrides:
            metadata=json.loads(take.with_suffix('.json').read_text())
            assert metadata['transcript_matches'] and metadata['text']==line
            manifest[-1]['voice']=metadata['voice']
            manifest[-1]['direction']=metadata['direction']
        cursor += span+.5
    transfer = cue_rows[2][3]+cue_rows[2][4]+.25
    # A quiet beat for the full reach/grip/release before Devon's last line.
    cue_rows[3][3] = round(max(cue_rows[3][3],transfer+2.1),3)
    total = round(cue_rows[3][3]+cue_rows[3][4]+1.6,3)
    q=lambda v:json.dumps(str(v))
    enc=lambda row,n:' '.join(q(v) if i<n else str(round(v,5) if isinstance(v,float) else v) for i,v in enumerate(row))
    shots=[]
    def shot(name, end, eye, target, lens):
        start=sum(s[1] for s in shots)
        view=[*site(*eye),*site(*target),lens]
        shots.append([name,round(end-start,3),1,*view,*view])
    shot('01 - Lou sent me',cue_rows[1][3]-.15,(3.9,2.30,6.0),(1.5,1.72,7.3),57)
    shot('02 - Did anyone follow you',cue_rows[2][3]-.15,(2.8,2.20,6.65),(1.5,1.91,8),51)
    shot('03 - No. Why.',transfer,(2.9,2.2,8.7),(1.5,1.93,6.95),49)
    shot('04 - Package received',transfer+2.1,(3.0,2.30,7.3),(1.5,1.68,7.5),54)
    shot('05 - We are square',total,(3.9,2.35,6.2),(1.5,1.72,7.5),57)
    path=[(0,(1.5,1.80,6.48)),(1.3,(1.5,1.80,7.44)),
          (transfer,(1.5,1.80,7.44)),(transfer+.7,(1.5,1.80,7.53)),
          (transfer+1.3,(1.5,1.80,7.53)),(transfer+2.1,(1.5,1.80,7.65)),
          (total,(1.5,1.80,7.65))]
    def box_at(t):
        for (ta,a),(tb,b) in zip(path,path[1:]):
            if t<=tb:
                u=max(0,min(1,(t-ta)/(tb-ta)))
                return [x+(y-x)*u for x,y in zip(a,b)]
        return path[-1][1]
    def key(t, p, yaw, reach=0, receiver=False):
        x,y,z=box_at(t)
        z += .055 if receiver else -.055
        side=-.23 if receiver else .23
        return [t,*site(*p),yaw,reach,*site(x+side,y-.015,z),*site(x-side,y-.015,z)]
    johnny=[key(0,(1.5,.66,6.1),0,1),key(1.3,(1.5,.66,7.10),0,1),
            key(transfer+.7,(1.5,.66,7.10),0,1),key(transfer+1.3,(1.5,.66,7.10),0,1),
            key(transfer+1.9,(1.5,.66,7.10),0,0),key(total,(1.5,.66,7.10),0,0)]
    devon=[key(0,(1.5,.66,8),180,0,True),key(transfer,(1.5,.66,8),180,0,True),
           key(transfer+.7,(1.5,.66,8),180,1,True),key(total,(1.5,.66,8),180,1,True)]
    prop=[key(t,p,0) for t,p in path]
    actors=[]
    def actor(name,model,height,track,gestures,walk=''):
        isprop=name=='Delivery box'
        mesh='models/props/delivery_box/flat_box.emesh' if isprop else CAST+model+'/skin.emesh'
        texture='models/props/delivery_box/cardboard_generated.png' if isprop else CAST+model+'/body.png'
        idle='' if isprop else CAST+'animations/idle.eanim'
        row=[name,mesh,texture,idle,*track[0][1:4],*track[-1][1:4],track[0][4],track[-1][4],height,0,total]
        actors.append(enc(row,4)+' '+q(walk)+' '+str(len(track))+' '+str(len(gestures)))
        actors.extend(enc(k,0) for k in track)
        actors.extend(enc(g,0) for g in gestures)
    actor('Johnny Mercer','player_male_01',1.76,johnny,[[total-1.3,1.1,.45,4]],CAST+'animations/walk.eanim')
    actor('Devon','civilian_male_07',1.72,devon,[[cue_rows[1][3]+.2,1.4,.45,5]])
    actor('Delivery box','',.10,prop,[])
    rows=['APRICOT_CUTSCENE 3',q('Mission 1 - We are square')+' 0.46',f'{len(shots)} 3 {len(cue_rows)}']
    rows.extend(enc(s,1) for s in shots);rows.extend(actors);rows.extend(enc(c,3) for c in cue_rows)
    (ASSETS/'cutscenes/mission1-delivery.cutscene').write_text('\n'.join(rows)+'\n')
    folder=TAKES.parent;folder.mkdir(parents=True,exist_ok=True)
    (folder/'script.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (folder/'timing.json').write_text(json.dumps(dict(duration=total,transfer=transfer,cues=cue_rows),indent=2)+'\n')
    # Keep the listening reel identical to the authored cue references.
    reel=array.array('h',[0])*round(total*24000)
    for _,_,audio,start,span in cue_rows:
        if not audio:continue
        with wave.open(str(ASSETS/audio)) as wav:
            assert (wav.getframerate(),wav.getnchannels(),wav.getsampwidth())==(24000,1,2)
            samples=array.array('h',wav.readframes(wav.getnframes()))
            assert len(samples)/24000<=span
        offset=round(start*24000);reel[offset:offset+len(samples)]=samples
    with wave.open(str(folder/'mission1-delivery-reel.wav'),'wb') as wav:
        wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(24000);wav.writeframes(reel.tobytes())
    print(f'Staged {total}s, 5 shots, 4 lines. Transfer: {transfer:.3f}s.')


if __name__=='__main__':
    main()
