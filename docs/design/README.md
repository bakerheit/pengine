# Probable Cause — Game Design

Probable Cause is a third-person driving and crime game set in an authored,
fictional world. The player is Johnny Mercer, whose first excuse to leave his
gas-station shift becomes a delivery for a manager who hints at a criminal
past. Driving, finding places, choosing when to get out, and dealing with the
city's response form the core experience.

The current project combines an authored opening and first delivery with a
growing free-roam sandbox. It does not yet define a complete campaign, economy,
or progression ladder. This document connects the existing design work and
current systems so new content has a shared direction.

[Documentation home](../README.md) · [Lore and world](../lore/README.md) ·
[Vehicle brands](vehicle-brands.md) · [Vehicle reference photos](references/1991-selected-vehicles.md)

## What the player should feel

These are working design principles distilled from the opening, city briefs,
and implemented systems. They guide additions; they are not a claim that every
part of the current game already delivers them.

- **Know the place by driving it.** A dock, hillside, hotel strip, neighborhood
  shop, and airport should offer different approaches and recognizable views.
  Named roads and landmarks should become useful knowledge during a pursuit.
- **Ordinary life sits close to crime.** The opening begins with work, boredom,
  and a lotto conversation. Suspicion enters through what Lou leaves unsaid.
  Keep human motives readable before escalating the stakes.
- **The city reacts through things the player can see.** Traffic signals,
  yielding cars, officers leaving cruisers, vehicle damage, and cleared snow
  should explain why movement becomes easier, harder, or dangerous.
- **Vehicles have character.** Body shape, sound, handling, damage, and brand
  identity should give a reason to recognize a car beyond its top speed.
- **Humor belongs inside the world.** Fictional brands and dry dialogue can be
  funny while people treat their jobs and immediate problems seriously.

## The opening and first mission

The authored opening establishes this sequence:

1. **New Game:** Johnny and Lou talk inside Halloway Gas. Lou implies that the
   money for the station came from a bank truck. Johnny volunteers to make
   Lou's delivery so he can get out of the store.
2. **Take control:** the scene leaves Johnny inside the store. The first cue
   directs him to his Legacy Car 5, parked outside. Entering that car is the
   designed handoff to the delivery destination.
3. **Drive to Ostend:** the mission marker leads to Devon inside Ostend Bait &
   Tackle. The player chooses the route and approaches Devon on foot.
4. **Deliver the package:** interaction begins *We are square*. Devon asks
   whether anyone followed Johnny, accepts the parcel, and tells him to tell
   Lou they are square.
5. **Return to play:** completion or skipping the delivery scene marks the
   delivery complete and returns control. No follow-on mission or payout is
   currently established by this handoff.

The scenes support pause and skip. A scene that cannot load its required cast
or assets must not silently award mission completion. The parcel is carried
during the authored scenes; ordinary walking does not have a persistent parcel
carry presentation. Its contents and Lou's motives remain deliberately open.

The opening script is [Johnny Mercer — The lotto](johnny-mercer-opening.md).
The gameplay contracts are [In-game opening](../game-opening.md) and
[Mission 1: We are square](../mission1-delivery-cutscene.md). Mission stages
live in [save_game.h](../../src/game/save_game.h), with transitions in
[delivery_mission.h](../../src/game/delivery_mission.h) and
[app_cutscene.cpp](../../src/app/app_cutscene.cpp). The sequence above describes
the authored design; this documentation pass did not replay the mission.

## Free-roam play

Outside scenes, the player can travel on foot or in vehicles, explore authored
sites, interact with available local features, and create or escape a police
response. A typical session is to pick a destination, drive there through
traffic, get out to explore, then continue with the condition of the current
car and the consequences of recent actions. This is the sandbox's present
shape, not a repeatable job or reward system.

### Movement and vehicles

Johnny can walk, sprint, jump, and climb suitable low obstacles with room to
stand and land. The jump action tries an eligible climb before an ordinary
jump. Cover, ceilings, walls, slopes, and landing space are part of the same
physical environment used for vehicle interactions.

Road driving uses suspension, steering, braking, handbrake slides, collisions,
and body damage. Classic GTA is the current default handling profile; alternate
profiles are development tuning choices, not player skills or unlocks. The
chase camera adapts to speed and obstructions. Damaged cars can leak fluids and
suffer engine failure. Stopping in an eligible bay at Rook's Auto Repair restores
body and mechanical condition after the service dwell; a repair payment system
is not established.

Rook's also resprays, free. Drive into a bay and stop, press R or pad X, and a
booth opens beside the car with 24 preset paints and a custom hue,
saturation and brightness picker; the car previews the colour live and the
world waits. Confirming sprays the car for a second. A respray drops the wanted
level only if no cop had eyes on the car while it pulled into the bay; if one
did, the car is painted and the stars stay. The pull-in is the point: getting
out and back in inside the bay, or arriving any way but driving in, means
driving out and pulling in again. Every drivable car can be painted, police,
ambulance and fire engines included. A stolen car keeps the traffic livery it
was driving in until it is resprayed, and a checkpoint keeps the current car's
paint. Three cars' paint masks are rough — Halcyon Six, the Car 8 ambulance and
the firetruck — and their follow-up is an authored mask for each.

Road-car entry checks speed and door clearance. Supported vehicles have staged
driver transitions. Stopped civilian traffic can be taken over; previously
driven cars can remain parked and re-enterable during the session. Active
police vehicles are locked. A selectable car in the developer menu is not
evidence of a dealership, purchase price, ownership unlock, or mission reward.

Boat, fixed-wing aircraft, and helicopter interactions also exist. They have
their own movement and boarding rules. The boat requires a safe shore or dock
exit; swimming is not supplied by that feature. The Harrow tractor and freight
trailer support coupling, reversing, dropping, and later pickup. Cargo jobs
and an articulated-truck economy are outside the current trailer contract.

Sources: [character controller](../../src/game/character.h),
[climbing](../../src/game/climb.h),
[vehicle interactions](../../src/app/vehicle_interaction.cpp),
[repair shop](../../src/game/repair_shop.h),
[respray rules](../../src/game/respray_shop.h),
[vehicle catalog](../../src/app/player_car_catalog.h),
[driving characterization](../driving-extremes.md), and
[tractor and trailer](../tractor-trailer.md).

### Traffic, police, and consequences

Traffic follows authored roads and shared signals, stop signs, and yield rules.
Following distances, gap choices, and bounded impatience give drivers some
variation. Congestion is part of the simulation; an authored road connection
does not by itself prove a junction works well under load. Engaged emergency
vehicles can cause nearby traffic to pull aside and later merge back.

Offenses add heat, displayed through five wanted levels. Witnessed driving
offenses include red-light crossings, stop-sign runs, and speeding. A visible
drawn weapon can be an armed threat. Player-caused cruiser impacts and harm to
people have separate reporting paths. Some violent actions report without a
nearby witness, so the sight rules for traffic offenses are not a universal
rule for every crime.

Pursuers route through the road network and use local steering maneuvers for
turning, passing obstructions, and approaching suspects. When sight is lost,
the search uses a last-seen center; renewed sight updates it. Dispatch delays,
responder budgets, detection range, and escape timing vary with district or
wanted level. The current response profiles permit deliberate ramming from
two stars. Police can dismount, pursue on foot, and fire at an armed on-foot
suspect when their combat conditions and line of sight allow it.

An unarmed player on foot can be arrested after an officer maintains clear
sight within 2 m for 3 seconds. Arrest clears the pursuit and displays the
arrest message at the current location. It does not yet implement a jail
transfer, fines, court process, or an item-confiscation loop.

Read [traffic behavior](../traffic-realism.md),
[police pursuit](../police-pursuit.md), and
[officers and arrest](../police-officers.md) for the detailed designs and their
historical checks. Current offense and response authority is
[police_gameplay.cpp](../../src/app/police_gameplay.cpp),
[wanted_system.h](../../src/game/wanted_system.h),
[police_ai.h](../../src/city/police_ai.h), and
[crowd.cpp](../../src/traffic/crowd.cpp). Some older pursuit descriptions retain
earlier tuning; use the current profile table for numeric balance decisions.

### Combat, damage, and recovery

The current weapon set is unarmed, pistol, and molotov. Punches and bullets
share the same human-health model across Johnny, pedestrians, and officers.
The pistol is semi-automatic with aiming and reload states. Molotovs consume
stock, travel as thrown projectiles, and can create local fires. These are
current loadout mechanics, not evidence of a completed weapon-shopping or
inventory-progression system.

People can be wounded or killed; killed ambient characters remain down until
their streamed presence is retired. Player damage includes combat, vehicle
hits, falls, and serious driving impacts. Death freezes control for the WASTED
sequence, then restores Johnny beside his current car with full health and
the pursuit cleared. The authored hospital exists, but hospital respawn routing
is not part of this recovery flow.

Sources: [weapon states](../../src/game/weapon.h),
[molotov](../../src/game/molotov.h), [fire](../../src/game/fire.h),
[shared body damage](../../src/city/body_damage.h),
[police combat](../../src/game/police_combat.h), and
[player damage and respawn](../../src/app/player_damage.cpp).

### Weather, navigation, and local interactions

Daylight and weather change the look and driving conditions of the world.
The current weather system includes rain, storms, snow, blizzards, hail,
heatwaves, flooding, and tornado episodes. Surface wetness and snow affect
grip; severe conditions influence visibility and lighting. Snow has physical
depth, shelter exclusion with drift under open canopies, per-vehicle snow
loads, and local clearing by autonomous municipal plows.
Plowing leaves a driven path with improved snow conditions; it is not a
playable snowplow job or a citywide depot schedule.

The atlas has Explore, Roads, and Places modes, with pan, zoom, recentering, and
a player waypoint. The rotating minimap shows nearby streets, landmarks, the
active destination, and the nearest road name. It supplies bearing and local
context, without GPS routing or spoken turn instructions. Route choice and
learning the place remain the player's work.

Authored interiors contain individual interactions where implemented. Examples
include the bank's manager-note/keypad/vault interaction, service at Rook's,
and drinking at The Bent Elbow. A furnished shop, restaurant price board, or
map icon does not automatically imply a functioning transaction system.

Sources: [conditions](../../src/game/conditions.h),
[weather hazards](../../src/game/weather_hazards.h),
[snowplows](../snowplows.md), [city atlas](../map-viewer.md),
[bank interaction](../../src/app/bank_interaction.cpp), and
[host gameplay integration](../../src/app/app.cpp).

## Progress and saves

Continue, New Game, Save Game, and Load Game operate on one local checkpoint
slot. Finishing the opening and delivery can save mission progress. A new game
keeps the previous slot until a replacement checkpoint is written. Loading
must validate a checkpoint before changing the active session.

The current save data includes mission stage, Johnny's position and view,
on-foot/car mode, current car model and pose, handling profile, damage,
mechanical reserves and failure state, registration, freight trailer state,
session seed, and simulation clock. Vehicles resume stationary. Saving requires
finishing transitions and leaving boats or aircraft.

This is a checkpoint rather than a full world snapshot. Extra session-parked
cars, ambient traffic, pedestrians, weapons/ammunition, player health, pursuit,
and unrelated interaction state are not a complete serialized world. Loading
clears extra parked cars and pursuit, and resets local snow-clearance history.
Do not build a mission dependency on an unsaved object surviving a reload.

See [Saved games](../save-games.md),
[save schema](../../src/game/save_game.h), and
[save/load integration](../../src/app/app_save.cpp). The source schema includes
trailer and registration fields beyond the shorter save guide.

## World, art, and sound

The world has two states: O'Haven, containing Pinatty, and Florangia, containing
Miandi. Pinatty supplies the opening's working streets, shops, docks, suburbs,
and terrain contrasts. Miandi's direction emphasizes a bright, flat coast,
palms, hotels, towers, nightlife, and working waterfront. Geography and names
come from [the lore guide](../lore/README.md) and the authored
[state/city table](../../src/city/states.h); older planning documents sometimes
use superseded names.

The visual direction is recognizable, semi-realistic low-poly/PSX work:
readable silhouettes, deliberately limited texture detail, tangible storefronts,
and cars that still read as manufactured objects. Compare additions with real
game assets under matching light and camera conditions. The
[Cruiser 91-C style pass](../assets/municipal-cruiser-91c-style.md) gives a
concrete example of preserving shape, wheel fit, and useful texture values.
It is a reference case, not a universal triangle or texture-size budget.

Sound supports place and action through vehicle voices, horns, sirens,
footsteps, ambient sound, dialogue, and weapon feedback. The opening's spoken
direction is relaxed, conversational, and dry. It avoids making each line a
punchline. Dialogue playback and waveform checks establish different things
from a listening review of the performance.

Brand names and emblems should feel like products sold in this world. Use
[the vehicle roster](vehicle-brands.md) for manufacturer/model distinctions
and draft status. In particular, BWC has an emblem direction but no playable
first model established by that design work. The world's exact calendar year
is still unresolved; an era reference on one asset does not set the whole game.

## What is defined, and what still needs design

| Area | Current boundary |
| --- | --- |
| Opening and first delivery | Authored scenes, mission stages, contact interaction, and checkpoint integration exist. |
| Free-roam systems | Traversal, vehicles, traffic, police, combat, map, weather, and several local interactions have runtime code. |
| World expansion | Authored locations coexist with proposals and staged work; each location brief must be checked against current content. |
| Campaign after Devon | Further missions, branching, failure/retry rules, and ending are not established here. |
| Economy and ownership | No shared design yet for payouts, prices, wages, fines, purchasing, garages, or durable owned-vehicle collections. |
| Recovery consequences | Current arrest and death recovery are defined above; jail and hospital gameplay remain separate design decisions. |
| Progression | Ranks, skills, faction standing, unlocks, and campaign completion targets are not established by existing mission stages. |
| Era and wider fiction | Exact year, parcel contents, and wider criminal relationships remain open. |

The next campaign design should answer what Johnny wants after the delivery,
what the player chooses, what failure costs, and what must survive saving.
Vehicle acquisition and service pricing should be decided together with the
economy, not inferred from menu prices painted on scenery. Pursuit balance
needs play review at the intended wanted levels before numerical profiles
become promises about difficulty.

## Adding to this design

Give a new feature or location a short linked brief stating its player purpose,
entry point, interaction, success/failure outcome, and save behavior. Separate
established facts, proposed direction, and implementation status. A new model
needs a place in the brand roster; a new business needs a consistent name and
role in the lore; a new mission needs an explicit handoff back to free roam.

For physical additions, road access, walkable entrances, terrain support,
collision, traffic circulation, and map placement must agree. For gameplay
changes, preserve clear player feedback and a recoverable path through pause,
skip, failure, death, arrest, and load where relevant. Mark a feature integrated
only after its implementation exists; record focused checks and actual game
or visual review separately. Do not promote a dated smoke run into a claim
that every route or interaction works.

Useful deeper briefs include [Pinatty's original map design](pinatty.md),
[Florangia terrain](florangia.md), [Miandi's master plan](miandi.md),
[hospital campus](hospital-campus.md), [Loom cultural quarter](loom-cultural-quarter.md),
and [Westmere Estates](westmere-estates.md). These retain their own scope and
historical status; this overview does not mark all of their proposals complete.

## Source boundary

Reviewed against the local working tree on **2026-09-12**, including ongoing
uncommitted work. This was a documentation and source review, with no fresh
build, gameplay run, listening session, or whole-city validation. Linked
verification notes describe their own dates and scopes. Use current source
for implemented behavior and current lore decisions for fiction; preserve
explicitly unresolved design questions until they are decided.
