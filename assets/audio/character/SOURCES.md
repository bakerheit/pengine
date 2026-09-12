# Character audio sources

## Footsteps

`runtime/footstep_<family>_<n>.wav` are cooked from a supplied pack named
**40 Free PSX Footsteps**.

- Supplied as: `40 Free PSX Footsteps.zip`, given to the build on 2026-09-11
- Archive SHA-256: `8ec6f17e6e72cdb11f2817f243c53523ea6adefd3ddaebda31a64332449774ee`
- 49 Ogg Vorbis files, 44.1 kHz stereo, in eight folders: Concrete, Dirt,
  Grass, Gravel, Metal, Stairs, Stone, Wood
- Per-file source and runtime hashes: `runtime/manifest.json`
- Runtime hashes alone: `RUNTIME_SHA256SUMS`

> **LICENCE AND ATTRIBUTION ARE NOT YET RECORDED, AND THAT IS A GAP, NOT AN
> OMISSION.** The archive contains no licence file, no readme and no metadata
> tags — every Vorbis comment block is empty, which is why nothing is quoted
> here. The pack name says "free"; free is not a licence, and a licence is what
> this file is for. Before these files ship in a build that leaves this machine,
> fill in the creator, the distribution page and the licence terms below.
>
> - Creator: **unknown — to be supplied**
> - Source page: **unknown — to be supplied**
> - Licence: **unknown — to be supplied**

### Which families are cooked, and why the other three are not

Five of the eight folders are cooked, one per `FootstepSurface`:

| Family | Source folder | Takes | Chosen when |
|---|---|---|---|
| concrete | Concrete | 5 | `GroundHit::road` where the bake's own material is not gravel — paved carriageway, kerbs and sidewalk slabs |
| stone | Stone | 4 | terrain `Surface::Rock`, and any authored prop top the player stands on |
| gravel | Gravel | 5 | terrain `Surface::Gravel`, and unpaved roads, which the bake also marks Gravel |
| dirt | Dirt | 5 | terrain `Surface::Sand`, which is the classifier's dry ground |
| grass | Grass | 6 | terrain `Surface::Grass`, the default drivable ground |

**Metal, Stairs and Wood are deliberately left in the pack and not cooked.**
Nothing the runtime can ask about the ground distinguishes them. `GroundHit`
reports a `Surface` (rock, gravel, grass, sand), a road flag and a prop flag,
and every `StaticBox` in the tree today is registered with the default material
— so there is no signal that says "this floor is timber" or "this walkway is
steel". Cooking them would ship three families the runtime has no way to
select, which is worse than not having them: it looks finished.

Giving them a home is real work in the world module, not here — authored
interiors and walkways would need to carry a footstep material of their own, and
`Surface` is the wrong enum to append to, because its order is the component
order of the terrain splat weights and the row order of the tyre grip table.

### How they were cooked

`tools/prepare_footsteps.py` decodes each take to 48 kHz mono, trims the
lead-in, and applies one gain per family:

```sh
python3 tools/prepare_footsteps.py \
  '/path/to/40 Free PSX Footsteps' assets/audio/character/runtime
```

Mono, because the sources are stereo recordings of a single foot and the runtime
plays footsteps non-spatially at the listener — the player's own feet are not
somewhere else in the room.

The lead-in is trimmed because a footstep is a transient triggered ON the
footfall. The takes carry up to 17 ms of silence in front, and pre-roll does not
read as an audio offset, it reads as the animation being out of sync.

**Normalisation is per FAMILY, not per file, and that is the decision worth
knowing.** Raw, the families differ by 2.5x in level — a walk across a kerb
would sound like a volume bug rather than a change of surface. Normalised per
file, the five takes in a family collapse to one loudness and the rotation stops
reading as a rotation. One gain per family, set by whichever take in it is
loudest, keeps the variation inside the family and makes the families
comparable. `audio_footstep_tests` measures both halves of that: no family is
twice as loud as another, and no take is silent.

Every take ends up at 0.62 peak or below and 0.032–0.082 RMS, which leaves
headroom under the engine, the city bed and dialogue. No compression, no pitch
change, no added effects.
