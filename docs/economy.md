# The economy

The player's money: where it comes from, where it goes, and why the numbers
are the size they are. The balance itself is `PlayerEconomy::cash`
(`src/game/player_economy.h`), saved from save version 5
([save-games.md](save-games.md)). Every change goes through `earn_cash`,
`spend_cash`, `buy_weapon` or `charge_cash`; nothing writes the field by hand.

## The wallet

| Event | Amount | Where |
|---|---|---|
| New game | $250 (`kNewGameCash`) | `game/player_economy.h` |
| Lou's delivery completes | +$750 (`kDeliveryPayout`) | `App::pay_delivery`, from `finish_opening` |
| Arrested | −$100 × 2^stars: $200, $400, $800, $1,600, $3,200 (`arrest_fine`) | `App::arrest_player` |
| Wasted | −$250 (`kHospitalBill`) | `App::charge_hospital_bill`, from `begin_player_death` |
| F1 → PLAYER & VEHICLE → ADD $1,000 CASH | +$1,000 (`kDevMenuCashGrant`) | QA only |

Amounts live in `src/game/wallet_rules.h`, pinned by `wallet_rules_tests`.

Spending, today, is Brassline Arms alone: the pistol at $500
(`kGunStorePistolPrice`) and rounds at $50 a box (`kGunStoreAmmoPrice`), in
`game/gun_store_shop.h` ([gun-store.md](design/gun-store.md)).

**The scale.** A pistol is $500 and a box of rounds $50; a bank job takes tens
of thousands. The start cannot buy the pistol, and one delivery can,
with change for ammunition. That is the point of $250: the first thing money
does is buy the first gun, and the first job is how you get it.

**Fines double per star** because a linear fine makes the fifth star a rounding
error. At one star an arrest costs a little over a box of rounds; at three it
costs more than the delivery paid; at five it is most of a small heist's float.
The fine is priced off the level the player was arrested at, read before the
arrest clears it.

**The hospital bill is flat.** A bill that grew with the wanted level would
charge twice for a chase the death has already ended — dying clears the stars.

**A penalty never goes negative.** `charge_cash` takes what is there and says
how much. The banner shows both figures when they differ (`FINE $3,200  -  PAID
$350`), or `NO CASH ON YOU`, so it never states a sum the wallet did not see.

**A payout is paid once.** It hangs off `complete_delivery()`'s one true
return, and it is paid before the checkpoint is written, so the saved wallet
already holds it.

## On screen

The cash sits in a plate beside the clock, top right, in the clock's panel ink
with a green edge where the clock has amber. Not under the clock: that column
is the wanted stars, the cooldown meter, the recorder badge and the on-foot
health bar, all of which move. Any change flashes to the left of the plate for
2.6 s, green for money in, red for money out, and changes in the same direction
while one is showing add up (a gun and a box of rounds reads as one `-$550`).

The flash is `CashFlash`, and it **watches the balance rather than being
told**: every frame it compares `economy_.cash` with what it last saw. A shop
or a heist that moves money shows up without calling anything. The one thing it
must be told is when the balance is *replaced* rather than changed — a load or
a new game — which `resync()` covers in `App::load_game` and
`App::begin_new_game`.

## Checking it

`--wallet-check --frames 3000` runs, unattended, in the real game: the start
balance, the delivery cutscene started and skipped for real, a two-star arrest,
a death, and a five-star arrest the wallet cannot cover. It checks the balance
after each and saves `build/wallet-check.{start,payout,arrested,wasted,broke}.png`.
It stops when it is done. The arrests go through `arrest_player()`, the same
door a real officer's arrest uses, without staging the officer;
`--police-officer-check` walks a real one up to the player.

## Not there yet

- Arrest does not jail, relocate the player or confiscate anything.
- The respawn is still beside the current car, not at a hospital.
- Rook's resprays and car bombs are free.
- None of this has been played by hand; the check's screenshots are the
  evidence.
