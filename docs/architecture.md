# Architecture notes — the design rules this engine lives by

Every rule below is stated with the reason it exists and what it cost to
learn. Most were paid for on `pengine`, the engine apricot succeeds; a few
have already been paid for here. They are not style preferences. Break one
and you will re-learn it, usually on someone else's machine.

**A note on what this document is.** apricot is being built by several agents
working different modules at once, so the feature list moves daily. The rules
below do not. Where a rule is enforced by code that is currently a stub, that
is called out explicitly rather than glossed — a doc that describes the engine
you meant to write is worse than no doc.

**The game on top is Pinatty**, a GTA-style open-world crime game rebuilding
`probablecause`. Its design is [`docs/design/pinatty.md`](design/pinatty.md).
**Its MAP is implemented and nothing else is**: `src/city/` holds the ten
district polygons, their character parameters, the landmark table and the five
terrain operators that `height_at()` evaluates (PENG-41). Roads, traffic,
police, missions and buildings are still design only.
Apricot Rally, the time trial that used to be the sample, was deleted in
PENG-23; every rule below that used to be stated in terms of laps, gates or
ghosts is now stated in terms of the engine, which is where it always belonged.

---

## The link graph is the architecture

**`apricot_sim` links glm and Threads. Nothing else, forever.** No windowing
library, no GL loader, no audio device, no UI toolkit. Everything that touches
hardware lives in `apricot_host`, which links `apricot_sim`. Dependencies flow
one way only: sim → host → exe.

That constraint buys three things at once. Every sim module is headless
testable, because there is nothing in it that needs a screen. Every sim module
is deterministic and replayable, because there is nothing in it that can read
a device. And "test the real producer, not a hand-built stand-in" stops being
advice and becomes the only thing the build will let you do.

The cost of *not* having this is specific and measurable. pengine had one
library, so a test could link anything, so keeping GL out of the logic layer
was a convention. Conventions erode one innocent `#include` at a time. By the
end, `tools/ci.sh` carried a hand-maintained `EXCLUDE` list of suites that
could not run headless, and every new test author had to know which side of an
invisible line their code was on. Here the line is a link error.

**`tools/guard_sim_purity.sh` runs first in CI, before anything compiles.** A
compile error you hit in thirty seconds is cheap. An architecture you notice
you lost six months later is not.

**The guard is a plain text search, and that is deliberate.** It greps sim
sources for `glad|SDL|<GL/|miniaudio|imgui`, so it also trips on those words
in *comments*. That is not a bug to work around: sim code should not need to
talk about the windowing library either. If a sim-side comment needs to refer
to the other side, say "the host layer" or "the platform layer". This document
and the other markdown files are not scanned, which is why they can name the
libraries freely.

The guard checks two things, because the grep alone is not enough. First, no
banned identifier in any sim source. Second, no sim module's `CMakeLists.txt`
mentions `apricot_host` — a module can quietly attach its sources to the host
target and slip past a text search entirely. It also **fails when an expected
sim-side file is missing**, so renaming a file cannot silently drop it out of
coverage. A guard that stops checking things is decoration.

**When the guard fires, the fix is essentially never to loosen the guard.**
Move the code to `src/platform/` or `src/gfx/` and have the sim side talk to
it through plain data. `core/input_frame.h` is the shape to copy.

**CMake fragments are module-owned.** The root `CMakeLists.txt` declares three
source-less targets and `add_subdirectory`s; each `src/<mod>/CMakeLists.txt`
calls `target_sources()` to attach its own files.

The reason is contention, not size. pengine's root file is 486 lines with 27
`add_test` blocks and probablecause's is 781 with 32 — perfectly manageable to
read, and still a single file that every agent adding a source must edit. Here,
two agents working different modules never open the same build file.
Registering a test is one line (`apricot_add_suite(name)`) in
`tests/CMakeLists.txt`.

Measured on this repo: seven agents built seven modules simultaneously and
collided only on `tests/CMakeLists.txt`, which is shared by design. No module
fragment ever conflicted.

(An earlier draft of this file claimed pengine's root reached "roughly 19k
lines with 500+ `add_test` blocks". That was wrong — 18,865 is its size in
BYTES, misread as lines from a directory listing. The decision stands on
contention; the number that justified it did not exist.)

`src/audio/` is the one **split** module: `synth.cpp` and the header-only
`mixer.h` build into `apricot_sim`, while `device.cpp` and `miniaudio_impl.c`
build into `apricot_host`. The guard knows this by an explicit file list, not
by a directory rule. See `src/audio/README.md`.

---

## Determinism is the testing strategy

**The world is a pure function of one 64-bit run seed, and the drive is a pure
function of its inputs.** Those two facts together are why one mechanism —
recording an input tape — produces in-game replay *and* bit-exact headless
regression tests. Every rule in this section protects that.

**No `std::rand()`, no `std::random_device`, no global generator state, and no
time-seeding anywhere in the engine.** A world that differs between two runs of
the same seed cannot be replayed, cannot be reported as a bug, and cannot be
regression-tested. `src/app/app.h` holds the run seed as a literal for this
reason, with a TODO to take it from the command line — never from the clock.

**Procedural content derives from `hash_coord()`, never from a sequential
stream.** A stream's value depends on how many times it has been pulled, and
pull order is exactly what you cannot control once chunks stream in around a
moving camera. A chunk approached from the north must generate identically to
the same chunk approached from the south. `Rng` exists for genuinely
order-local work — shuffling a list you already hold, jittering inside a chunk
you have already keyed — and `rng_at(seed, x, z)` is the sanctioned way to get
one.

**The sim steps at exactly 120 Hz and never reads a wall clock.** `kSimHz` is a
constant, not a setting; changing it invalidates every recorded tape. Exactly
one wall clock exists in the whole program, in `App::run()`, and it feeds a
measured delta to `FixedStep`. Nothing below `App` can observe real time.
`step_vehicle()` takes `dt` as a parameter rather than reading one, so it stays
testable at other rates and nobody is tempted to reach for a timer inside. Any
game step function added later inherits the same rule. The first `std::chrono`
call added below that line ends replay, and the symptom is "replays desync
after a minute".

**Every clock a game keeps counts sim steps, never wall time.** That is what
makes a recorded duration a property of the drive rather than of the machine it
ran on, and it is what lets a tape reproduce a run exactly instead of
approximately.

**`InputFrame` is the replay format, so it is POD forever.** Trivially
copyable, standard layout, no padding — all three asserted at compile time.
Button bits carry explicit values because they are serialised; renumbering them
rewrites history. Append new fields at the *end* and bump
`kReplayTapeVersion`. Reordering or resizing an existing field silently
invalidates every tape ever recorded, and the failure presents as "the physics
changed".

**The tape and its version live in `core/`, not in a game.** `core/replay_tape.h`
holds `kReplayTapeVersion` and `ReplayTape`. They used to live in `game/`, next
to the sample that happened to record tapes first, and the cost of that was
exact and measurable: `tests/sim_determinism_tests.cpp` — the suite that pins
the format — could not pin the *version*, because reaching into `game/` for it
is the coupling that suite exists to remove. It carried a written note saying
so. A format owned by whichever game is currently on top is a format that
leaves with it. Both are now pinned in one place.

**A tape stores inputs, never positions.** `ReplayTape` is a version, a seed, the
absolute sim step it starts from, and a `std::vector<InputFrame>`, one per sim
step. Storing positions as well would create a second source of truth for the
same run, and the only thing two sources of truth ever do is disagree. Playback
refuses to extrapolate past the end of the tape for the same reason: a silently
repeated last frame would let a replay drive on past the end of the run it
recorded, looking entirely plausible while it did.

**The start step is on the tape because purity demands it.** Anything keyed on
absolute step — `game/conditions.h`'s weather is the live example — would hand a
tape recorded an hour into a session the conditions of step zero. The symptom
reads as "replays drift", a long way from the cause.

**Record before stepping.** A recorder appends the input that produced the
transition *out of* the current state. Recording afterwards offsets the whole
tape by one step, and the replay drifts from the moment it starts.

**The replay claim is pinned against the ENGINE, in
`tests/sim_determinism_tests.cpp`, and that file includes nothing from
`game/`.** It records a tape of varied input — throttle, brake, steer,
handbrake, latched gear-change edges — drives it through `step_vehicle` against
a real `TerrainCollider`, and compares every `VehicleState` field with `==`.

**That argument has since been collected, in full.** The proof used to live only
inside the sample game, so replacing the sample would have taken the guarantee
with it and unpinned the sim at the exact moment a new game was being built on
top of it. The sample was replaced (PENG-23) and this suite did not move a line.
Keep it that way: nothing in it may ever include from `game/`.

Three things in that suite are load-bearing and easy to delete by accident.
Both **negative controls** — the same tape on a different seed must diverge, and
altering one `InputFrame` must change the run — without them every assertion
still passes if the step ignored the terrain, or ignored the tape. An
**anti-vacuity floor** on distance travelled, because "bit-identical" is
trivially true of two cars that never moved; measured with the throttle cut,
the run drops from 206.6 m to 0.9 m and still replays perfectly. And the
comparison helper **proves its own coverage** by flipping a bit at every byte
offset of a live mid-run state and requiring every byte of every declared
member to be noticed. That last one is not theoretical: the sample game's
best-lap serialiser once dropped 14 `VehicleState` fields, every wheel's
`angular_velocity` among them, and it survived a ticket because the test's own
comparison skipped those exact fields. A helper that quietly skips a field is
worse than no test, because it reads as coverage. (That serialiser was deleted
with the rally. The lesson was not.)

**The version constant is a golden value too.** `kReplayTapeVersion` is asserted
by literal in the same suite, for the same reason `hash_coord`'s outputs are:
bumping it invalidates every tape ever recorded, and the assertion is what makes
somebody type the new number on purpose instead of discovering the cost three
tickets later.

**A test must never let the thing it is testing decide when the test stops.**
The tape-replay loop is bounded by a frame count rather than written as
`while (replay_input(...))`. With the bare form, an implementation that
extrapolates past the end of the tape makes the suite *hang* instead of fail —
and a hung suite reads as a broken CI runner, not a broken tape format. Measured
while writing it: the sabotaged build ran until it was killed. Bounded, it fails
in under a second and says what happened.

### A worked example: why determinism gets its own tests

`hash_coord(0, 0, 0)` returned **0**.

splitmix64's finaliser is a bijection with excellent avalanche, and it has zero
as a fixed point: `mix(0) == 0`. Feeding it seed 0 at coordinate (0, 0) fed it
zero, so it returned zero, so the terrain lattice was degenerate at the world
origin — which is the single most likely seed-and-coordinate combination in
existence. It is where every default-constructed state sits, where every test
starts, and where every fresh run spawns.

The fix is one line: offset the input by the golden-ratio gamma constant before
mixing, moving it off the fixed point. The important part is not the fix. It is
that a hash with textbook-quality avalanche was *provably wrong at the one
input everybody uses*, and nothing about the output looked wrong anywhere else.
No amount of driving around would have found it.

It is now pinned by `tests/rng_determinism_tests.cpp` in two ways: a direct
regression check that the origin and the nearby axes are non-degenerate, and a
set of **golden values** — the literal outputs of the current algorithm,
asserted. Any change to the mixing trips them.

**Golden values are a feature, not friction.** If you change the algorithm
deliberately, you must regenerate them, and in doing so you invalidate every
existing replay tape and every save seed. That is the real cost of the change.
The test exists to make sure you pay it on purpose rather than discovering it
three tickets later as "the world generates differently on my machine".

**Test assertions must not compile out.** The default build type is
`RelWithDebInfo`, which defines `NDEBUG`, under which `<cassert>`'s `assert()`
is a no-op. A suite using plain `assert()` does not fail — it *lies*, passing
green while checking nothing. `tests/test_assert.h` provides `REQUIRE`,
`REQUIRE_MSG` and `REQUIRE_NEAR`, which always fire. There is no `<cassert>` in
the test tree and there should never be one.

---

## Fixed-step accounting

**The accumulator is held in step units, not seconds.** One unit is one sim
step. This is what makes `pending_ -= floor(pending_)` *exact* and `alpha()`
provably in `[0, 1)` with no clamping. Holding seconds means repeatedly
subtracting `kSimDt`, which is 1/120 and therefore not representable in
IEEE-754, and the rounding slop lands precisely on the step boundary — where it
either drops a step or double-counts one. That is a drift you only notice as a
replay desync after several minutes.

**One `floor`, not a subtract-until-empty loop.** The `while (acc >= dt) { acc
-= dt; ++n; }` form derives an integer by repeated float subtraction: it costs
an iteration per owed step after a hitch, and it accumulates error into the one
quantity the engine's determinism actually rests on.

**A stall clamps, and the surplus is discarded, not carried.** `kMaxStepsPerFrame`
is 12 — 100 ms of sim, which comfortably covers a genuine hitch and refuses to
cover a debugger. Carrying the surplus instead means every following frame runs
a full budget repaying a debt it can never clear, so one twenty-second
breakpoint leaves the session permanently stuttering. Sim time may fall behind
wall time. It may never spiral. The clamp is surfaced in the overlay in red,
because a build that reports it outside a debugger session is genuinely too
slow and swallowing that silently hides it.

**Garbage deltas are inert.** The guard is written `if (!(frame_dt > 0.0))`
rather than `<= 0`, because that form is also true for NaN. A misbehaving timer
then produces a zero-step frame instead of a NaN accumulator that poisons the
clock for the rest of the run, and a backwards clock cannot rewind the sim.

### Input edges clear on step count, never per frame

**This is the rule the whole latched-edge design exists to protect.**

A render frame can legitimately owe **zero** sim steps. A 240 Hz display
against a 120 Hz sim owes a step every other frame; roughly half of all frames
owe nothing at all. If a button press sampled during such a frame is cleared at
the end of that frame, the press is simply gone. The sim never saw it.

The bug that produces is intermittent, frame-rate dependent, and invisible on
any machine whose display runs at or below the sim rate — which is the machine
it gets written on. To a player it reads as "the handbrake doesn't always
work", and that is agony to chase from a bug report.

So the ordering is explicit at every layer:

```
mapper.begin_frame();                        // clears look deltas ONLY
while (poll(&e)) mapper.handle_event(e);     // ORs edges in as they arrive
mapper.end_frame();                          // bakes held keys into axes

const FixedStep::Tick tick = clock.advance(frame_dt);
for (int i = 0; i < tick.steps; ++i) step(...);
if (tick.steps > 0) mapper.consume_edges();  // <-- the guard
render(...);
```

`InputFrame::pressed` is a **latched** mask: the producer ORs bits in as events
arrive, and the consumer clears the whole mask once, only after a step has
actually run. `begin_frame()` deliberately does not touch it. `consume_edges()`
is a separate explicit call precisely so that moving it out from behind the
`tick.steps > 0` guard is a visible edit rather than something that happens
quietly inside a helper.

The edge is also latched only on a genuine transition, because the platform
layer repeats key-down events while a key is held; without that check a tap and
a hold are indistinguishable.

`tests/fixed_step_tests.cpp` pins both halves: that a zero-step frame keeps a
latched edge, and that over 4000 frames at double refresh — with about half of
them owing no step — not one press is lost. The test asserts that zero-step
frames actually occurred, so it cannot pass vacuously.

---

## World streaming

**Pace by work, not by cell count.** A "load N cells per frame" budget still
dumps tens of thousands of node creations into the one frame that crosses a
boundary, and the resulting hitch is far more noticeable than a chunk arriving
two frames later. pengine's streamer activates *instances* until a per-frame
budget runs out and carries a half-activated cell into the next frame, so
loading stays ahead of the player and the latency is invisible.

*Status in apricot:* done, and this note used to say otherwise. There is no
`StreamerConfig::max_loads_per_update` in this tree — grep returns nothing. The
budget that exists is `max_instances_per_step` (384), denominated in scene nodes
rather than chunks precisely because a wooded chunk and a bare rock chunk do not
cost the same, and a chunk that exhausts the budget half way is **carried** to
the next step and resumes at the prop it stopped on.

**The second denomination landed with LOD (PENG-27), and it had to.** A chunk's
*mesh* cost stopped being constant too: level 0 is 4096 quads and level 3 is 64,
so "two chunks a step" is either 8192 quads or 128 depending on where the player
is looking. `max_build_quads_per_step` (4096) and `max_chunk_builds_per_step`
(16) both apply and the tighter wins. The count is generous because the quad
budget is the one that bounds work — at a count of 2 the shipping config's 2796
level 3 chunks need 1400 steps and a teleport leaves the horizon empty for
twenty seconds.

**4096 is one level 0 chunk, and that number came from a measurement rather than
from arithmetic.** It was 8192 first, on the reasoning that two chunks a step is
a modest ask. In the running app that was 6.0–6.5 ms of meshing per frame for
two hundred frames of far-ring fill — a third of a 60 Hz frame spent on terrain
a kilometre away. Halving it made a full step ≈2.5 ms *at every level*, and took
frames over a 4 ms streaming budget from hundreds to one in fifteen hundred.
That uniformity across levels is the property a count-only budget can never
give.

**Evict in bulk.** Tearing a cell down one removal at a time is O(cell × world)
and freezes the frame on every crossing; sweep each container once instead.

*Status in apricot:* done, inside the streamer. `evict()` ends in
`scene.remove_many()`, which is the single sweep this rule asks for.

**Eviction has a GPU half, and it did not exist until PENG-27/PENG-28.** Nodes
came out of the scene and the chunk meshes stayed in the renderer's table
forever, because that table was append-only by design — a recycled `MeshId`
aliasing a live node is a bug that points at nothing. Streaming removed the
option, so the aliasing was made *unrepresentable* instead: a `MeshId` is now
`slot | generation` and a handle held across a free resolves to `nullptr`,
draws nothing, and logs. `Streamer::take_released_meshes()` drains the ids the
host must free — from eviction, from a level change, **and from a dropped
delivery**, which is the subtle one: the host has already uploaded that mesh and
nothing else knows it exists.

**A level change is a refit, not a rebuild.** The terrain node is re-pointed at
the new mesh in place, so the chunk is on screen at its old level right up to
the frame it is on screen at its new one. Destroying and recreating it would put
a chunk-sized hole under the player for the length of the activation.

An earlier version of this paragraph described a `pending_evictions()` /
`mark_evicted()` handshake that the streamer would expose for someone else to
consume. **That API does not exist and never did in this tree** — a grep for it
returns nothing. It was written from a plan rather than from the code, and it
then travelled: into a survey, into a ticket, and into a status report, each
one repeating it with more confidence than the last. If a status note here
names a function, grep for it before believing it.

**Load and evict radii are deliberately different.** A single radius means a
player idling exactly on a boundary thrashes the same chunk in and out forever.
The gap is the hysteresis that stops it, and `update()` enforces
`evict_radius > load_radius` rather than trusting the config.

**A chunk is resident when the work finished, not when it was requested.**
A build may complete several frames later, so a chunk must be marked resident
when the work finished and not when it was requested — marking on request is how
a chunk ends up permanently missing, never built and never re-requested.

(`mark_resident()` is likewise not a function in this tree. The *rule* stands;
the API named here did not exist.)

**Everything the streamer decides is deterministic.** Load candidates are
sorted by squared distance so the budget spends itself on the chunks the player
is about to reach, with a coordinate tie-break so two machines given the same
camera position produce the same load order. The eviction list is sorted too,
because `unordered_set` iteration order is not guaranteed and a nondeterministic
eviction order is a nondeterministic test.

**Fill before you resume.** A cold start and a mission teleport both drop a
camera into a world with nothing around it. Without an explicit fill path the
player gets a frame of void and then the hitch of the world arriving, which
`docs/design/pinatty.md` §7.1 calls a blocker in a way steady-state streaming is
not. `StepMode::Fill` spends no budget and plans only `prime_radius`, so the
loop is *bounded* — an unbudgeted full-radius plan is not a fix for a hitch, it
is a longer one. Measured: 68.5 ms in 2 steps on a cold start, 73–78 ms on a
teleport, spent before the frame clock starts so `FixedStep` is never handed the
bill.

**`ready()` measures levels about the position it is handed, not about the last
planned centre**, and that distinction cost a debugging session. `centre_` only
moves when `plan()` runs, so between a camera jump and the next step it still
describes the world the player *left*; asked about the destination it reports
every chunk already at the level it wants, because relative to the old centre it
is. The teleport then filled zero chunks and resumed on whatever coarse ground
was lying around. **The symptom was shaped like success** — "filled in 0 steps,
0.0 ms" — because a fill that never runs and a fill that is very fast print the
same number. The test that catches it warps *inside* the already-loaded radius,
where nothing needs loading and everything needs refitting; a test aimed at the
far side of the island passes either way.

**Generated chunks never touch disk.** A chunk is a pure function of
`(seed, coord, lod)`, so regenerating is cheaper and safer than caching — and a
cached file on disk silently outranks a code change. Caching generated cells to
disk once flooded pengine's working tree with hundreds of phantom files.

**The seed is the world identity — and from Pinatty onward, so is the map.**
This rule has been amended, deliberately, the amendment is narrow, and as of
PENG-41 it is implemented: `city::kMapSeed` is pinned in the map tables and
`App` builds its `TerrainCollider` from it, while `App::seed_` carries the
session.

Pinatty is an *authored* city: a specific place with named districts that a
player learns, not a fresh draw per seed. So the world is now a pure function
of `(map, seed, coord)` rather than `(seed, coord)`, where `map` is a compact,
human-editable, diffable definition in the repo — district polygons, road
spines, character parameters, landmarks. A save file is a seed plus a map
version. Everything visual is still *generated*: no baked geometry ships.

**Generation still never writes. Only the editor writes, only on an explicit
save, and only in a dev build.** That distinction is the whole rule, because it
is precisely where pengine went wrong: its *streamer* saved procedural output
as authored data on first encounter of a cell, so generation silently became
content and the working tree filled with hundreds of files nobody chose to
create. Those files then outranked the code that would have regenerated them.

So:

- The shipped runtime opens the map read-only. It has no write path at all.
- The map editor is dev-only, behind a build flag, and is the sole writer.
- It writes the **definition**, never baked chunks. If saving ever produces a
  file per cell, the rule has been broken again.
- The map version is hashed into the determinism identity, so a tape recorded
  against one map cannot silently replay against another.

**Editor tests must write to a temp directory by construction, not by
convention.** probablecause carries a CI `EXCLUDE` list whose entries are
excluded *because they write into live world data*, and its own working
agreement warns never to re-add them until they use a temp dir. `tools/ci.sh`
has no `EXCLUDE` list and must never grow one — so this is a design requirement
of the editor, not a follow-up ticket.

**Never "fix" a seam by averaging across chunks.** Chunk meshes are built at
absolute world coordinates and carry a shared closing row (65 vertices per
64-quad edge), so neighbouring chunks evaluate `height_at()` at the identical
coordinate and the seam closes exactly rather than approximately. If a seam
appears, something stopped being pure; averaging hides that and keeps the
cause.

**LOD reopened that seam, and the rule survived it intact.** A chunk at level
`L` samples every `1 << L`-th point of the *same global lattice*, at absolute
world coordinates, so a coarse chunk's vertices are a strict subset of the fine
chunk's and agree with them **bit for bit**. Nothing is filtered or decimated —
a coarse chunk asks the same pure function fewer questions. Two chunks at the
same level therefore still close exactly, exactly as before.

Two chunks at *different* levels do not share every coordinate along their
common edge, and there the crack is real. It is covered by a **skirt**: a
vertical curtain dropped from the perimeter vertices the chunk already has. It
*adds* geometry and modifies none, which is what keeps the rule above literal
rather than merely respected. A stitched transition row was considered and
rejected: it closes the crack exactly and costs the purity, because
`build_chunk()` would take its four neighbours' levels as arguments and every
chunk on a ring boundary would need a full rebuild whenever a *neighbour*
changed ring.

**Skirt depth is measured, and getting the measurement right mattered more than
the safety factor.** Measuring the relief between *adjacent* perimeter vertices
is wrong in one direction, and not the obvious one: a level 0 chunk has 1 m
steps so its skirt came out short, but what it has to hide is a level 3
neighbour's 8 m chord. The *fine* side is the side that cracks. Measuring
relief over the coarsest neighbour's cell width fixed it and let the factor drop
from 3.0 to 1.5 while coverage improved. `tests/terrain_lod_tests.cpp` prints
the margin left at every level pairing over real terrain rather than asserting
a skirt merely exists — worst crack 1.803 m against a thinnest margin of
1.247 m.

**Anything else draped on the terrain now has a range.** Road ribbons, props and
decals are placed on the *level 0* drawn surface via `mesh_height_at()`. Where
the ground under them is drawn coarser they are no longer the same surface, by a
measured amount: mean 0.025 m and worst 1.221 m at level 1, mean 0.258 m and
worst 6.130 m at level 3. That is the same "what you touch is what draws" rule
with a second consumer, and the answer is a draw distance, not a tolerance.

**Collision never comes from a coarsened mesh.** `build_chunk_collision()`
refuses a `lod > 0` mesh outright and says so, and it reads only the ground
index span so a vertical skirt face cannot enter a set whose whole contract is
that normals point up. The streamer holds the chunks the player can touch at
level 0 — including the four *diagonal* neighbours, which a radius of 1 leaves
out because ring membership is a circular test and their squared distance is 2.
A car near a chunk corner has wheels there.

---

## Rendering

**`gl_state` is the only place in the engine that binds.** A redundant
`glBindTexture` / `glUseProgram` / VAO bind is a driver round trip for nothing,
and a frame that draws a thousand batches does a lot of nothing.

**The trap, and it costs a day every time somebody rediscovers it: GL object
ids are recycled.** Windows drivers hand a freshly deleted id straight back to
the next `glGen*`. The bind cache then sees "id 7 is already bound", skips the
bind, and the new texture never arrives. You get **black textures on PC while
macOS looks perfect**, because Apple's GL recycles ids lazily. There is no way
to detect the mistake from inside the cache, and it will not reproduce on the
machine you develop on.

So: **every `glDelete*` in this engine must be paired with the matching
`gl_state::on_*_deleted()` call**, immediately, before the id can be reissued.
`Shader::destroy()` and `Mesh::destroy()` already do it and are the pattern to
copy. Binding a VAO also invalidates the cached element-buffer slot, because a
VAO carries its own element-buffer binding and caching through it would skip a
genuinely needed rebind.

**Culling is a sim-side decision; the renderer only consumes the result.**
`Scene` stores what exists and answers "what can the camera see", handing back
a list of opaque integer handles it never dereferences. `Frustum` and `AABB`
are pure maths in `core/`. That split is what lets a headless test cull a real
scene built by real generators — the failure mode this engine most wants to
avoid is a consumer test passing on hand-built inputs while the real producer
feeds garbage.

**The cull sort is what makes instancing work.** `Scene::cull()` sorts
survivors by batch key so equal keys land adjacent, and `plan_draw_batches()`
collapses contiguous runs in one linear pass. Feed it an unsorted list and it
does not crash — it just finds almost nothing, which reads in a profile as
"instancing does nothing".

**The batch key excludes anything that rides per-instance.** `tint` and
`uv_scale` are deliberately absent from `batch_key()`, so nodes differing only
in those still collapse into one draw. Anything added to `Renderable` that must
differ per node has to be added to the per-instance payload, **not** to the
key. Adding it to the key instead silently degrades batching to one draw per
object while still "working".

**A per-node draw distance only ever shortens visibility.** Letting an authored
value extend past the global limit means one node can defeat the streaming
budget from the far side of the world.

**An inverted AABB passes every plane test.** A box that has never been
expanded has `min = +inf, max = -inf`, which is inside every frustum plane and
at every distance, so it would draw forever. Cull code checks `valid()` first.

**The camera lives in `gfx`, not in the sim.** It is pure maths and holds no GL
object, so it *could* live in `core` — it does not, because letting sim code
hold a camera is how "just cull against the camera" turns into gameplay that
depends on where the player was looking.

**Guard the aspect ratio.** A minimised window reports height 0; an aspect of
infinity produces an all-NaN projection matrix, which poisons the frustum and
culls the entire world for the rest of the session.

**Verify the GL context you got, not the one you asked for.** A driver may hand
back something lower than requested, and the failure then arrives much later as
an unexplained blank screen. Check it as a floor, not an equality — macOS
legitimately returns 4.1 core for any 3.2+ core request.

**Use drawable size, not logical size.** On a Retina display the two differ by
the backing scale factor, and using logical size for the GL viewport renders
the frame into the bottom-left quarter of the window.

*Status in apricot:* **there is a render pass, and this note used to say there
was not.** `gl_state`, `Shader`, `Texture` (procedural only), `Mesh` including
the instanced attribute stream, `Camera`, `Sky`, `Precipitation`, `Hud` and
`Renderer` are all implemented — see `src/gfx/README.md` for what is still
deliberately absent (no transparency pass, no shadows, no hot-reload, and the
renderer's resource tables never free).

Measured on this machine, `./build/bin/apricot --frames 1200`: four shader
programs linked, 364 visible nodes drawn in 5 batches (3 instanced) in **5 draw
calls**, 1050 rain quads, the HUD's 50 quads in **one** draw against a
procedurally generated glyph atlas with no font file on disk, and the GL error
queue clean for the whole session.

---

## Physics and gameplay

**Collision comes from the geometry that draws.** `TerrainCollider` is not a
copy of the terrain data and not a downsampled grid — it calls the same pure
`height_at()` the chunk mesher calls, so the ground the car touches is *by
construction* the ground the player sees. Any parallel re-derivation drifts
from the visual mesh, and the resulting "the car floats on that hill" bug is
invisible until someone drives there.

**And so does the MATERIAL.** The same rule, one layer up, and it has now been
paid for twice. `TerrainCollider::material()` calls `surface_kind_at()` — the
classifier the mesher splats with — and physics classifies nothing.

It used to. `terrain_collider.cpp` carried its own `classify_surface()`: a hard
cutoff on `normal.y` plus a patch-noise coin flip, written when terrain owned
no materials, with a comment saying to delete it the day terrain shipped one.
Terrain shipped one, and exposed `surface_kind_at()` for exactly this caller.
Nobody deleted the copy. Measured across 11,559 land samples in the home basin
over three seeds, the two classifiers named a **different material 40.85% of
the time** — 44.90% island-wide — so the grip under the car disagreed with the
texture under the car nearly everywhere, not just at edges. Worse, physics
never returned sand *anywhere on the island*, because its sand test was an
altitude 27 m below sea level: every beach in the game gripped like grass.

The pattern to recognise is that this is the identical failure to the one
`height()`/`normal()` had, and it was found the same way — by measuring the
disagreement rather than by driving around. Heights agreed to 15 microns but
normals were out by up to 1.02, most of a right angle. Both now read
`0.000000`, and so does the material disagreement, pinned as a **count** in
`tests/terrain_collision_tests.cpp`.

There is exactly one material enum, `terrain::Surface`, because terrain draws
the ground. Physics owns only what a material *grips like*
(`TyreSurface`, in `physics/surface.h`). A grip column in both modules is two
sources of truth for the car's handling, and when there were two they
disagreed: rock was 0.95 in terrain and 1.15 in physics.

**Physics must not depend on streaming state.** Ground queries are valid
anywhere, including in chunks that have never been meshed. A collider that only
answers inside resident chunks turns a streaming hiccup into a fall through the
world. `surface_kind_at()` evaluates the field rather than reading a mesh for
this reason: grip is answerable in a chunk that has never been built.

**Probes report penetration as a negative distance, and callers need that.**
`GroundHit::distance` is not clamped at zero: the sign is how a caller tells
"hovering" from "already under the surface".

**Vehicle state is plain data, complete and copyable.** It is what a replay
diff compares and what a save-state restore overwrites, so no pointers, no
back-references, no cached handles. Wheel spin is accumulated in the state
rather than derived in the renderer from frame time, so a replay reproduces the
wheel rotation exactly instead of approximately. Steering is smoothed in the
physics, not interpolated in the renderer, so the physics never disagrees with
what the player sees.

**Wheel order is a contract.** Replay tapes and tuning index by it. Front-left,
front-right, rear-left, rear-right.

**Any trigger volume needs a direction check and a swept test.** Both rules were
paid for by the rally's checkpoint gates, and both outlive it. Without a
direction check, reversing back and forth over a trigger fires it repeatedly.
Without sweeping the segment from the car's *previous* position — not its
pre-step position, its end-of-last-step position — anything moving fast enough
passes straight through between steps. A trigger tested as a sphere at a point
in time is a trigger that a fast car does not reliably hit.

**Weather reaches the tyres, and only the tyres.** `game/conditions.h` is a pure
function of `(seed, absolute sim step)` and folds into `VehicleTuning::grip_scale`,
which sizes the friction circle. It deliberately does not touch engine or brake
torque: scaling both applies the weather twice, once to the tyre that is sliding
and again to an engine that does not know what it is driving on.

*Status in apricot:* `step_vehicle()` is real. Suspension, a tyre model with a
friction circle, an engine torque curve, a gearbox with an RPM readout, load
transfer, handbrake and rollover recovery are all implemented and pinned by
`tests/vehicle_tests.cpp` — throttle drives the car, brake stops it, the
handbrake breaks the rear loose and a slide can be caught. Static prop boxes are
solid.

`height_at()` has been retuned for Pinatty and now ends by applying the
authored terrain operators in `src/city/terrain_ops.h` (PENG-41). The island is
15.95 km² of land in a 6144 m box, 74.1% of it under 5°, peaking at 129.8 m on
Ferrone Hill — measured and printed by `tests/city_map_tests.cpp` on every run,
not quoted from the design. `kHomeRadiusMetres` is gone; the spawn guarantee it
provided is now an authored Flatten, so `height_at(seed, 0, 0)` is 12.0 m
exactly for every seed. It keeps its signature; physics, meshing and the
collider all call through it.

**The operators go LAST, on metres, and the design document had that wrong.**
§1.3 puts them inside the normalised shape, before the metre mapping. That does
not survive contact with the map for two reasons, and the second decides it:
targets would have to be authored in normalised shape units rather than metres,
and a Carve could not reach a stated depth below sea level at all — but worse,
the island mask MULTIPLIES the shape, so an operator applied before it gets
scaled down by the mask and TILTED by the mask's gradient. Every flattened area
in Pinatty is near the coast: the dock apron, the Strand promenade, the airfield
on its spit, the causeway. A flatten inside the mask comes out neither flat nor
at the height it was asked for, exactly where it matters most.

---

## Audio

**Synthesise first. There are zero audio files on disk and there is no
loader.** Every sound is generated at load from parameters, so there is nothing
to ship, nothing to license and nothing to go missing on a fresh clone.
`miniaudio` is compiled with encoding, decoding and generation all disabled —
dead weight is also dead surface.

**Fade the head and tail of a generated clip.** Without it the clip starts and
ends on a non-zero sample and every playback begins with an audible click. It
is the single most common synthesised-audio bug. A *looping* clip is the
exception and takes no fade, because the ramp would pump on every cycle.

**Accumulate oscillator phase in double.** A float phase over a few seconds at
48 kHz drifts audibly flat by the end of the clip.

**The gain maths is header-only and device-free**, because it is exactly the
arithmetic that goes wrong silently. Keeping it in the sim library means a
headless test can assert that muting music does not duck the engine, with no
sound hardware in sight.

**One mixer, shallow routing:** `final = emitter_gain * category[c] * master`.
Deeper graphs — per-emitter buses, sends — are where "why is this one sound
quiet" becomes unanswerable. Add a category before you add a layer.

**Trims clamp to `[0, 1]`; emitter gain clamps only at zero.** A trim above
unity is how you get clipping that appears only when several loud things happen
at once. Emitter gain legitimately exceeds unity for distance attenuation and
one-shot emphasis, but a *negative* gain inverts the waveform's phase rather
than making it quiet, and that reads as a weirdly hollow mix rather than as an
error.

**A mixer that defaults to silent is a bug report waiting to happen.** A
value-initialised `std::array` is all zeroes, so `Mixer` fills its categories to
unity in a constructor rather than relying on member initialisation.

*Status in apricot:* `mixer.h` and `synth_tone()` are real and complete.
`synth_engine_loop()` returns a bare tone — a placeholder with a ticket.
`AudioDevice::start()` deliberately does not half-open a device: it logs and
returns false, and callers must treat that as "no audio this session" and carry
on. Audio is never load-bearing for the app starting. Nothing in `src/app/`
wires audio up yet.

---

## Process

**Never push red.** `tools/ci.sh` is the gate: purity guard, configure,
`-Werror` build, headless `ctest`. It is the same set of gates the build is
expected to survive, and it runs the guard first because it is instant.

**Every test suite links `apricot_sim` and only `apricot_sim`.** That is not a
convenience, it is the architecture assertion: if a test ever needs a window, a
GL context or an audio device to run, the logic under test is in the wrong
library. Move the logic, do not relax the test.

**Test the real producer, not just consumers.** The expensive failure mode is
"ships green but broken in-game": a consumer test with hand-built inputs passes
while the real producer feeds garbage. The classic on pengine was kinematic AI
cars reporting zero velocity into a hitbox builder, silently disabling a whole
feature while its tests stayed green. Instantiate the real system.

**Rebuild before a feel-check.** A stale binary wastes everyone's time, twice
over — once when it looks broken and once when it looks fixed.

**A passing test is necessary, never sufficient, for anything a player can see,
hear or feel.** Determinism tests prove a drive is reproducible. They cannot
tell you the car is fun to drive, that the engine note sounds like an engine, or
that a landmark reads from far enough away to navigate by. Watch it, listen to
it, drive it.
