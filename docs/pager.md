# The pager, and Lou's page after the delivery

Johnny carries a nineties pager. A page slides in at the top left, rattles on
two bursts of four beeps with its red LED flashing, and crawls across a
one-line green dot-matrix LCD twice before it slides away. Above the message
line the LCD shows how many pages the pager has had and the time of day.

The first page is Lou's. Ten seconds after the delivery to Devon completes,
the pager reads **Call me at store - Lou**. Once it has gone, the objective
line reads *PAGE: Call Lou at the store from a payphone*, and the minimap, the
city map and a floating **Call Lou** tag point at the nearest payphone. At any
payphone, on foot, **E / A** calls Lou: Johnny dials the store, the marker
clears and *CALLED LOU AT THE STORE* shows. The call saves a checkpoint, as
the delivery does.

**The call is where the next mission starts, and nothing follows it yet.** It
moves the mission stage to `LouCalled` and stops; there is no scene, no
dialogue and no next job. What Lou says on that call is unwritten.

## Where it lives

| Piece | Source |
| --- | --- |
| The pager: queue, crawl, slide, beep pattern | [`game/pager.h`](../src/game/pager.h) |
| Lou's page and the call: when it pages, what counts as calling | [`game/lou_page.h`](../src/game/lou_page.h) |
| Every payphone, the nearest one, and reach | [`game/payphones.h`](../src/game/payphones.h) |
| The beep and the touch-tone dial, synthesised | `synth_pager_beep`, `synth_payphone_dial` in [`audio/synth.cpp`](../src/audio/synth.cpp) |
| Stepping, the call, the markers and the drawing | [`app/pager_gameplay.cpp`](../src/app/pager_gameplay.cpp) |
| Mission stages `CallLou` and `LouCalled` | [`game/save_game.h`](../src/game/save_game.h), save version 7 ([Saved games](save-games.md)) |

To page the player from anything else, call `pager_.receive(text)` on the
fixed step. Pages queue oldest first, and each beeps once when it starts. A
page that arrives under another objective line sits below it. The pager keeps
no saved state: a page that should survive a load has to follow from saved
progress, as Lou's does from the mission stage.

## The rules, and what they cost

**Any payphone will do.** The marker points at the nearest of the twelve (the
ten district booths and the pedestal and wall handset on Halloway Gas's west
wall), but a player who walks up to a different one has still found a phone.
Pointing at one and accepting only that one would punish a player for
knowing the city.

**Nearest is a straight line, held with a 25 m margin.** There is no routing
in this game, only a bearing, so straight-line distance is the honest
measure. The marker keeps its phone until another is 25 m nearer, so it
cannot flick between two booths as the player crosses the halfway line. The
cost is that for those 25 m the marker is on the second-nearest phone.

**Reach is 1.5 m from the phone's ground point, within 1.2 m of height, on
foot.** The widest footprint is the booth, 0.87 x 0.89 m, so the 0.32 m
character pressed against it stands 0.76 m out face-on and 0.94 m at a
corner. The height window keeps a player on a roof or bridge above a phone
from calling on it. A player in a car gets out first; near a booth, E tries
the call before it tries to enter or steal a car.

**The page waits ten seconds, in sim time.** That is past the 6.25 s Mission
Success card, so the two never share the screen. The wait restarts whenever
the stage leaves `DeliveryComplete`, so a load waits the whole delay again.
Everything is stepped on the fixed step, so a page lasts the same number of
sim steps at any frame rate.

**The beep and the rattle are one pattern held in two places.** `pager.h`
defines the pattern and the HUD shakes to it; the synthesised clip sounds it.
`pager_tests` measures the real clip window by window against the pattern, so
changing one side alone fails the build.

## Verification

```sh
ctest --test-dir build -R '^(pager|save_game|audio_synth)_tests$' --output-on-failure
build/bin/apricot --pager-check --frames 3000 --save-file /tmp/pager-check.save
```

`pager_tests` covers the crawl, the slide, the queue and step-size
independence; the beep clip against the pattern; the page's timing and the
restart of its wait; the twelve payphones, the nearest one and its hold at
the halfway line; reach on foot and not from a car or another storey; and
the real character walked into each of the twelve real cooked payphone boxes
from four sides on the real terrain and roads, required to be able to call
from where it stops. That last part skips, saying so, when the private
payphone models are not cooked.

`--pager-check` runs the real game from a delivery just completed at Devon's
counter. It requires the page to arrive after the delay and beep once, and
the marker on the Ostend Docks booth. It captures the pager mid-rattle and
with *Call me at store* whole on the LCD. It presses E at Devon's counter and
requires nothing to happen. Then the real character walks out of the bait
shop, round the waiting bench at its door and 23 m up the bank to the booth,
the marker holding on it, and E there has to call Lou. It captures
`.page.png`, `.message.png`, `.objective.png`, `.payphone.png` and
`.called.png` beside `--screenshot`, `build/pager-check` by default. It was
run on 2026-09-26 and passed, with a clean GL error queue. `--delivery-check`
and `--wallet-check` still pass; the wallet check runs long enough for the
page to arrive, between its arrest and its death, and the pager shows under
the WASTED tint like the rest of the HUD.

Not checked: the audio was verified by measuring the clips (the beep's
2.9 kHz fundamental and odd harmonics, the dial decoding as 555-0142), not
by a listening session; and the walk to a payphone was run only from Devon's
shop to the Ostend Docks booth.
