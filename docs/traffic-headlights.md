# Traffic headlights

AI traffic now emits real twin spotlights. The existing glowing lenses remain;
the new beams illuminate roads, vehicle bodies, buildings and skinned people.
The player's separate two-light rig and canopy lights are unchanged except for
early rejection of disabled lights and fragments outside their cones.

## Flush lens glow

Player and traffic lens glow reuse the body's exact triangles, transform and
damage data. They render after opaque geometry with `GL_EQUAL`, depth writes
disabled, and no vertex or clip-depth offset. This prevents glow from being
pulled in front of the dented fascia or through a nearby panel. The renderer
splits body/glow instances before batch planning, since they can share the
same mesh/material key. Both instanced and single-instance paths use this order;
ordinary alpha decals still draw afterward with `GL_LESS`.

`tools/check_vehicle_headlights.py` checks the fleet's left/right masks plus
damaged lenses at two angles. Frame 3, frame 90, and single-instance captures
must match. Add `--brakes` to check the shared rear-lamp path.

## Bounded work, not a car-count budget

- `TrafficVisual` derives two world-space emitters from each active car's
  fitted lamp geometry and chassis pose, with the existing damage/health rules.
- `TiledLightGrid` bins conservative spotlight bounds into 64-pixel screen
  tiles and 24 logarithmic depth bands. Each cone is clipped separately to
  each depth band before projection, including near-plane crossings.
- Cell lists have variable length. There is no nearest-four-car selection,
  per-cell cap, or silent overflow. Exceeding the device's buffer-texture
  capacity is reported as an error instead of dropping lights.
- Three buffer textures carry lamp data, cell offset/count pairs and indices.
  Orphaned, four-frame rotating storage avoids overwriting data the GPU is
  still reading. The GL binding cache tracks 2D and buffer texture targets
  separately, including deletion and invalidation.
- The existing shared forward shader reads only its cell's list. Squared
  distance and cone tests reject work before specular evaluation. No extra
  scene draw calls or per-car render passes are introduced.
- Each traffic beam reaches 32 metres. View-depth fade starts at 200 metres
  and ends at 256 metres. Off-screen emitters remain eligible when their
  cones reach the view. Daylight disables beam generation and skips uploads.

This is shadowless lighting. Beams can pass through walls or other cars; the
tile grid is not an occlusion/shadow solution. Heavy overlap still costs more:
tiling removes irrelevant work, not actual contributions to the same pixels.

## Repeatable checks

```sh
./build/bin/apricot --lighting-benchmark --frames 1200 --start-at 4 9 --start-heading 90 --screenshot /tmp/traffic.bmp
./build/bin/apricot --lighting-benchmark --traffic-light-stress --frames 1200
./build/bin/apricot --night --frames 300
./build/bin/apricot --night --no-traffic-headlights --frames 300
```

The benchmark uses midnight, ignores gameplay input, advances 600 fixed sim
steps, then freezes the scene. It alternates beams off/on every 120 frames,
discarding the first 16 frames of each phase. Optional screenshots capture
the same frozen scene at frames 419/539 as `.off.bmp` and `.on.bmp` suffixes.
The stress option replaces actual emitters with **200 synthetic beams** in a
dense camera-facing 100-car arrangement; it does not spawn 100 extra models.

GPU timer queries measure the world-and-people pass. Diagnostic runs drain
preceding GPU work before starting a timer to avoid Apple's multi-frame queue
latency contaminating the interval. Normal gameplay never performs that wait.
CPU binning and upload times are reported separately. Benchmark FPS is not a
normal-play throughput claim because the timing mode deliberately serializes
GPU work. Other running applications can affect these local measurements.

Measured on Apple M5 / OpenGL 4.1 at 2560x1440, RelWithDebInfo, 2026-09-03:

| Scene | Source / view-relevant beams | GPU off / on | Added GPU | CPU grid + upload |
|---|---:|---:|---:|---:|
| Actual traffic, west-facing intersection | 444 / 132 | 2.198 / 2.524 ms | 0.326 ms | 0.116 ms |
| Dense synthetic 100-car fixture | 200 / 200 | 2.244 / 5.625 ms | 3.381 ms | 0.789 ms |
| Same synthetic fixture, repeat run | 200 / 200 | 2.602 / 6.683 ms | 4.081 ms | 0.810 ms |

These are median samples from individual A/B runs, not hardware-independent
budgets. Matched real-traffic screenshots confirm lighting on pavement,
vehicles and pedestrians. The repeat stress run kept exactly 106 world draws,
29 instanced batches and 69 character draws in both phases. Headless tests pin all 200 overlapping lights,
depth separation, daylight, deterministic list construction, invalid inputs,
off-screen cones, near-plane clipping, approaching beams and non-tile-aligned
resolutions.
