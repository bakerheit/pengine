# The Pinatty Savings & Trust heist

The bank on the block northeast of Halloway Gas already had a way in: the
manager's note in the back office prints the vault code, the keypad takes it,
and the door swings. This is what is behind the door, and what it costs to
take it.

It is a sandbox job, not a mission. Nothing tells the player to do it; the
cash is simply there, and the city answers when it goes.

## The loop

1. **Crack it.** Read the note, punch the code, the door opens. Opening the
   vault is not a crime on its own: the alarm is on the cash, not the door.
2. **Grab it.** Five piles sit inside, each taken on foot with E / A when you
   stand beside it. A taken pile is gone from the room.

   | Pile | Where | Take |
   |---|---|---|
   | Three stacks of strapped bricks | on the vault table | $6,500 each |
   | Two cash carts of shrink-wrapped bales | by the deposit wall and the south wall | $12,000 each |
   | **Total** | | **$43,500** |

3. **Trip the alarm.** The first grab trips it. Heat goes straight to three
   stars, and every further pile adds one more point of heat, so a full sweep
   of the room leaves at four. Greed is a choice: two stacks and out is a
   three-star escape. An alarm bell, synthesised in code and positioned over
   the front door, rings for 45 seconds; it has not yet been tuned by ear.
4. **Carry it.** The take is not money yet. It is on screen as **TAKE**, with
   the instruction to lose the cops, for as long as the heist is live.
5. **Get paid, or don't.** The take is banked the moment the wanted level is
   back to zero, through `earn_cash`, so the wallet cap is respected and the
   banner shows what actually went in. Arrested or wasted first, and the whole
   carried take is gone.

## Why banked-on-escape

Paying on pickup would make the alarm meaningless: the money would already be
safe while the cops arrive. Holding it until the stars clear is the GTA rule
and it makes the chase the job. Losing it on arrest and on death is what gives
the chase stakes; the wallet agent's fine and hospital bill come on top and are
not the heist's business.

## No farming

- **The vault restocks thirty minutes of play after the alarm tripped** (sim
  time, `kBankHeistRestockSteps`), and only once the carried take has been
  settled either way. Piles left behind stay in the room: coming back for them
  later is a second heist and trips the alarm again.
- **The vault's state is saved** (save version 6: which piles are gone, and the
  sim step the vault restocks at), next to the wallet it paid into. So a
  save/load cannot hand back a full vault with the money already banked, and a
  relaunch does not restock it either.
- **The carried take is not saved.** The wanted level is not saved either, so a
  save made mid-escape loads with no heat and no take: the same as being
  caught. The vault stays emptied.
- **New Game** restocks it, with everything else.

## Where it lives

- `game/bank_heist.h` — every rule above, headless: the piles and their values,
  the grab reach, the heat, settling the take, the restock. `bank_heist_tests`
  drives it against a real `WantedSystem` and a real `PlayerEconomy`.
- `city/bank_heist_layout.h` — the drawn piles and the two carts, built from
  the same pile table, so what you reach for is where it draws. The carts are
  solid; the cash is not, and is hidden when taken.
- `app/heist_gameplay.cpp` — the App glue: the E / A grab, the alarm bell, the
  TAKE readout and the banners.
- `app/heist_check.cpp` — `--heist-check`, the unattended run that goes through
  the real keypad and the real grab key, then escapes and captures the payout.

## Not in this

No crew, no planning, no drilling minigame, no dye packs, no guards. No money
bag on the player's back. Police respond through the existing wanted system
only; there is no heist-specific dispatch or roadblock.
