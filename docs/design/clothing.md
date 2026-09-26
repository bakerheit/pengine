# Clothing and accessories

## Trench coat

User direction, September 26, 2026: make the Bellwether trench coat clothing
that the player can buy in a store when the clothing/shop systems are built.

| Item | Decision |
| --- | --- |
| Reserved item ID | `trench_coat` |
| Category / slot | Clothing / outerwear |
| Look | Weathered olive-brown coat, lapels, belt, storm flap, split tails |
| Acquisition | Purchase from a future clothing store |
| Starting ownership | Not owned |
| Starting equipment | Regular player outfit; coat unequipped |
| Price and store | To be decided with the shop/economy work |

## Current implementation

The regular `player_male_01` model is the default again, including Bellwether
starts. The coat reference sheet, mesh generator and cooked prototype are
retained. There is no purchase, wardrobe, ownership or equipment system yet.
The coat is not a required asset for starting the game.

The prototype is currently a complete skinned outfit variant that reuses
Johnny's base rig and body parts. It is not yet a separate mesh that layers
over arbitrary shirts or characters.

## When clothing stores are built

- Offer the coat as an optional outerwear purchase with a preview.
- Charge for it once and record ownership only after a successful purchase.
- Let the player equip and remove owned clothing while keeping Johnny's
  identity, rig and gameplay state.
- Persist ownership and equipped clothing. Older saves begin in the regular
  outfit without silently granting the coat.
- Keep the coat cosmetic unless a later design explicitly gives it effects.
- Review the outfit during walking, sprinting, aiming, climbing and vehicle
  entry/seated poses before making it available for purchase.

Sources: [reference sheet](references/bellwether/detective.png),
[asset provenance](../assets/bellwether.md),
[`make_trench_detective.py`](../../tools/make_trench_detective.py).
