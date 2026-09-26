# Brassline Arms — gun store near Second Chance Pawn

Environment built and checked on 2026-09-05 from the three-agent planning draft. The shop is enterable and staffed, with original display weapons, stock shelves, cases, a counter, safe, parking, curb access, sign, and interior lighting. Since 2026-09-26 the counter sells the pistol and pistol ammunition; see [Buying at the counter](#buying-at-the-counter).

## Location and layout

Brassline now occupies the open Sixth Street parcel at grid `(-230,-93)`, one block from The Bent Elbow and 109.29 m from Second Chance Pawn in a straight line. The move preserves the existing storefront, parking, and Sixth Street curb access while putting the shop 207.51 m from the hospital origin and fully outside both its parking garage and southeast shipping/receiving yard.

| Parameter | Built value |
|---|---|
| Authored grid center | `(-230,-93)` |
| World X/Z origin | `(-149.018889,-156.532083)` |
| Basis and ground | `kGridCos`, `kGridSin`, yaw −6°, ground 12 m |
| Lot | 32 × 34 m, local center `(0,0)` |
| Shell | 16 × 12 m, local center `(0,-4)` |
| Front | Local `+Z`, wall at `z=+2`, toward Sixth Street, road ID 36 |
| Driveway | 6 m wide; solver-selected local X center 12.5 m |
| Entrance | 2.4 m authored opening, approximately 2.2 m between frame faces |
| Height | 4.1 m ceiling underside, 4.6 m parapet; raised sign and HVAC above |

The lot stays fixed. The production access baker connects the gap to Sixth's sidewalk and makes the curb opening from the same triangles used for road collision. Allowed spine `{36}` prevents a connection to Seventh behind the shop. The access solver moves the requested driveway center from 13 to 12.5 m to reserve its edge margin.

The implemented test checks the full lot every 0.5 m against current authored roads, samples real terrain support, checks neighboring access parcels plus the newer bar/towers, and directly rejects the hospital shipping/receiving envelope. The whole-city authored-lot test separately includes that service-yard envelope alongside the hospital, every current downtown parcel, and the outlying authored sites.

Coordinates and the shared basis govern authoring; some older source comments reverse compass directions.

![Original layout schematic](gun-store-plan.svg)

The schematic uses local coordinates with the street at the bottom. Final sign height, fixture lighting, and driveway center follow the implementation described here.

## Art and interior

Pale masonry, brick side/rear walls, charcoal steel, a worn oxblood sign, scuffed concrete floor, and a timber display wall. The storefront uses open framed spans with thin security bars. The central customer route stays clear, with display furniture on the sides and broad passages around each counter end.

The generated sign is preserved intact at `assets/textures/world/neighborhood/brassline-arms.png`. Its 2121 × 741 aspect ratio maps to a 6.4 × 2.236 m raised fascia. The sign sits forward of the roof trim, which otherwise crossed its lettering. [Exact imagegen prompt and provenance](../assets/brassline-arms-generated-texture.md).

Interior coordinates below are shell-local, with the front at `z=+6`. Subtract 4 from Z to get site-local coordinates. Floor top is 0.20 m above site ground.

| Element | Shell-local center `(x,z)` | Width × depth |
|---|---|---|
| Counter | `(0,-2.4)` | 10 × 1.2 m |
| Clerk | `(0,-4)` | Faces the entrance |
| Rear weapon rack | `(0,-5.55)` approximately | 8 m wide |
| Side shelves | `(±7.35,0.8)` | 0.75 × 5 m each |
| Low display island | `(-4,1.3)` | 1.6 × 2.6 m |
| Safe | `(6.6,-4.75)` | 1.3 × 1.5 m |

Four original long-gun silhouettes hang on the rear wall; two compact firearm props rest on counter mats. Shelves hold boxed stock, and hard cases sit on the island and window plinths. These are decorative objects, with furniture providing collision.

Four ceiling downlights, three rack lights, two angled counter lights, and two exterior lamps use actual runtime light sources. The rack/counter lights were added after night inspection showed that downlights alone left vertical stock and the clerk too dark. Nearby materials are reused; no new weapon or character pack is required.

Outside, site-local `z=2…4.4` is the storefront walk, `z=4.4…10.4` is the 6 m maneuver aisle, and `z=10.4…15.9` contains four 2.8 × 5.5 m bays at `x={-7.2,-4.4,4.4,7.2}`. The 2.4 m central walk is interrupted by a flush painted crossing through the vehicle aisle.

## Integration

- `src/city/gun_store.h`: authored site, plan, fixtures, and support-surface predicate.
- `src/city/building_access.h`: fixed parcel inventory and Sixth-only access.
- `src/app/world.cpp`: materials, sign, scene registration, oriented wall/fixture collision, and floor/walk support.
- `src/city/authored_staff.h`: appended stable clerk identity; Devon retains index 1.
- `src/app/app.cpp`: fixture-derived lights, with optional local aiming for the new wall washes.
- `src/app/game_ui.cpp`: both pawn and gun-store footprints and Places entries. Icon choice is explicit rather than tied to array position; navigation remains bearing-only.
- `src/main.cpp`, `src/app/app.h`, `src/app/app.cpp`: `--start-player-at X Z` places the on-foot player independently of the parked car for interior QA. Non-finite inputs and combination with `--start-driving` are rejected.

The reusable local skill is `/Users/andrewbaker/.codex/skills/apricot-city-planner/SKILL.md`. It covers parcel survey, authored geometry, access/curb integration, shared collision, staff, map, lights, and behavioral plus visual validation. The bundled skill validator passes.

## Validation

All six relocation-focused suites pass: `gun_store_tests`, `building_access_tests`, `authored_city_layout_tests`, `pawn_shop_tests`, `minimap_tests`, and `ui_flow_tests`.

The new suite uses the real character controller and baked world collision to walk from the sidewalk through the entrance, browse, reach the counter, pass around the staff side, and leave. Counter and sealed-door negative controls block the same movement. The real vehicle simulation crosses the final driveway in both directions at 5 m/s with zero impacts; all four bays have a tested pedestrian path to the entrance. Full vehicle parking turns and live player input were not automated by this suite.

The shared access suite validates all 24 authored entrances, their slopes, lamp clearance, and exact rendered/collision triangle correspondence. New-site bounds and staff checks also pass. The new QA CLI's invalid-input and incompatible-mode checks return exit code 2 as intended.

Rebuilt and launched the actual game after the move. The street-level daylight capture shows the complete Brassline storefront, lot, parking, crosswalk, and neighboring city fabric with no hospital service geometry crossing the parcel. The run completed with a clean GL queue. The full-map lab also rendered cleanly and shows the new Brassline footprint away from the hospital campus.

Current relocation evidence:

- `build/qa/brassline-move/street-day.png`
- `build/qa/brassline-move/map.png`

Original captures under the ignored build directory predate the parcel move and are retained only as interior/art references:

- `build/brassline-exterior-day.png`
- `build/brassline-interior-day.png`
- `build/brassline-interior-night.png`
- `build/brassline-map.png`

Reproduce from the repository root:

```sh
cmake --build build --target apricot gun_store_tests building_access_tests authored_city_layout_tests pawn_shop_tests apricot_map_lab -j8
ctest --test-dir build --output-on-failure -R '^(gun_store_tests|building_access_tests|authored_city_layout_tests|pawn_shop_tests|minimap_tests|ui_flow_tests)$'
./build/bin/apricot --start-at -148.5091 -211.7798 --start-player-at -151.6321 -131.6690 --start-heading -6 --frames 180 --clear --daylight --save-file /tmp/brassline-move-qa-save.json --screenshot build/qa/brassline-move/street-day.bmp
./build/bin/apricot_map_lab --zoom 16 --x -149 --z -157 --layer 2 --screenshot build/qa/brassline-move/map.png
```

Screenshots are SDL BMPs. The preview PNGs are format conversions without image edits. The full project suite was not rerun for this scoped environment change.

## Buying at the counter

Stand on the shop floor in front of the counter and the prompt `E / A  SHOP - BRASSLINE ARMS` appears. E or pad A opens the Brassline Arms menu; the game pauses while it is open, like the bank keypad and Rook's booth.

| Item | Price | What you get | Refused when |
|---|---|---|---|
| Pistol | $500 | ownership of the pistol, which the wheel can then equip | already owned (`OWNED`), or cash short |
| Pistol ammo | $50 | a box of 24 rounds into the pistol's reserve | no pistol yet (`BUY THE PISTOL FIRST`), the box would take the reserve past 240 (`AMMO FULL`), or cash short |

The menu shows your cash, each price, and the ammo row's `CARRYING n / 240`. A row that cannot be bought is greyed with the reason; pressing Accept on it shows the reason (for example `NOT ENOUGH CASH`) and charges nothing. A buyable row asks `PAY $500 FOR PISTOL?` first: Accept pays, ESC / B goes back to the list without charging. ESC / B from the list, or `LEAVE THE COUNTER`, closes it. W / S or the D-pad moves the selection.

**Where you can shop from.** `at_gun_store_counter()` in `src/game/gun_store_shop.h` is a site-local box: `|x| <= 4.6`, `z` from −5.8 (the counter's face) to −4.0 (where the island display begins), with the feet within the floor height band, on foot. Every side of that box is inside the shell, so no point outside the building, on the roof, behind the counter or in the staff passage is in it; that is the through-a-wall rule, and it needs no line-of-sight test that could leak. `gun_store_tests` checks it against the real character walk: the counter stop and the pressed-against-the-counter spot are in the zone, the staff passage and the clerk's side are not.

**The rules live in `src/game/gun_store_shop.h`** (headless): prices, `gun_store_refusal()`, which both greys a row and refuses the purchase so they cannot disagree, `gun_store_buy()`, and the `GunStoreMenu` state machine. The pistol goes through `buy_weapon` and a box through `spend_cash` after every check has passed, so nothing moves on a refusal. `src/app/gun_store_counter.cpp` only opens the menu, routes input to it, and draws it.

**A bought pistol comes loaded.** The pistol's magazine and reserve exist from the start of a game (12 and 48) and nothing can spend them before the pistol is owned, so buying it hands you those 60 rounds.

### You can only use what you own

A new game owns bare hands and molotovs; a v1–v4 save owns everything (see [save version 5](../save-games.md)). The weapon wheel draws an unowned sector dark with `LOCKED` and `NOT OWNED - BUY IT AT BRASSLINE ARMS`. You can point at it, but releasing equips nothing (`close_weapon_wheel()` in `src/game/weapon_ownership.h`). The 1/2/3 keys only move the wheel's hover, so they go through the same gate. `enforce_weapon_ownership()` also runs every frame before the weapons step and holsters anything unowned, because `equipped` is a plain field that QA scripts and future pickups can write directly. `--weapon-check`, `--damage-check` and the police officer check grant themselves the pistol for that reason.

### Death keeps what you bought

Dying keeps ownership (it is in the economy, which death does not touch) and keeps the pistol's magazine and reserve. Only the draw, a reload in progress and the recoil are dropped (`respawn_weapon_use()`). Before the shop, death refilled the pistol to 12/48; that would have made dying the cheapest box of rounds in town. Molotovs are not sold, so their stock still comes back full on respawn. Taking it away would leave a player with no way to get more.

### Tests and the in-game check

`gun_store_shop_tests` covers exact funds, one dollar short, a second pistol, cancel at the confirm, Back and LEAVE, ammo without the pistol, the ammo cap (including a reserve already past it from an old save, which is refused, never trimmed), the zone's inside and outside points (staff side, rear and side walls, storefront, roof, under the floor, in a car), the death rule, and a purchase through the real `encode_game_save`/`decode_game_save` that then equips through the gate. An unbought pistol stays locked after the same round trip.

`--gun-store-check` drives the real keys in the real game, unattended. It starts on the customer side of the counter, sets the wallet to $1,000, and checks each step:

1. The wheel refuses the unowned pistol.
2. The prompt shows and E opens the menu.
3. The pistol is bought through the confirm ($500).
4. A box of rounds is bought (reserve 48 → 72, $450 left).
5. A second pistol is refused.
6. With $20 left, a box is refused as `NOT ENOUGH CASH`.
7. ESC leaves, and the wheel then equips the bought pistol.

Every step asserts the exact cash and reserve and saves a screenshot. It exits non-zero on any failure.

```sh
./build/bin/apricot --gun-store-check --frames 600 --save-file /tmp/gun-store-qa-save.json \
    --screenshot build/qa/gun-store/check
```

Captures (`build/qa/gun-store/check.<stage>.png`): `wheel-locked`, `prompt`, `menu`, `confirm`, `bought-pistol`, `bought-ammo`, `already-owned`, `no-cash`, `wheel-owned`, `armed`. Checked by eye on 2026-09-26 at 2560×1440, daylight.

**Not built yet:** earning money (payouts and fines belong to the wallet work), selling molotovs or anything else, resale, robbery, attachments, and refusing to serve a wanted player.
