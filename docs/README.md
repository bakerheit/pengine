# Probable Cause — Design & Lore

Start here for the game being built on Apricot: what the player does, where
it happens, who lives there, and what gives the world its identity.

This reference brings together the current source and authored direction as
of **2026-09-12**. It is a documentation pass, not a new gameplay QA report.
Detailed feature pages retain their own dated validation evidence.

## World

[World documentation](world/README.md) · [Vehicle manufacturers and models](world/vehicles/README.md)

## Vehicle photos and models

[**Open World → Vehicles**](world/vehicles/README.md) — Meridian, Switchback and Hookline, with front, rear, both sides, top and three-quarter views.

![GLM Meridian design reference](design/references/glm_meridian/front-three-quarter.png)

[Meridian](world/vehicles/GLM/Meridian.md) · [Switchback](world/vehicles/Rodeo/Switchback.md) · [Hookline](world/vehicles/Harrow/Hookline.md) · [Playable model renders and game captures](design/reviews/1991-vehicle-refinement/README.md)

## Start with the question

| I need to know… | Read |
| --- | --- |
| What kind of game are we making? | [Game design](design/README.md) |
| What is established about the fiction? | [Lore](lore/README.md) |
| How do the states, cities and districts fit together? | [World guide](lore/world.md) |
| Who are Johnny, Lou and Devon? | [Characters and opening story](lore/characters.md) |
| Which businesses and institutions belong here? | [Businesses and brands](lore/businesses.md) |
| Which vehicle makers exist, and what do their badges mean? | [Vehicle brands and emblems](design/vehicle-brands.md) |
| How is the engine put together? | [Engine architecture](architecture.md) |
| How do I build and run the project? | [Repository README](../README.md#build-and-run) |

## What belongs where

**Design** describes the player's experience, mechanics, visual direction and
content requirements. Put regional build plans and feature decisions in
`design/`; link to the code that supplies current behavior.

**Lore** describes the fiction: place names, people, relationships, brands and
the story as authored. A visible storefront establishes a business name; it
does not establish its owner, founding date or secret criminal ties.

**Implementation and asset records** explain how something was built and
checked. Existing feature guides stay at this level, and `assets/` keeps
provenance, cooking notes and visual evidence. Link to those records instead
of copying their test reports into the lore.

Use these distinctions in new entries:

| Label | Meaning |
| --- | --- |
| Established | An identity or story fact supported by the current authored sources or an explicit creative decision. |
| Authored scene | What a character says or does in the current script. A character's claim is not automatically objective history. |
| Current implementation | Behavior traced to the present source, with runtime evidence cited separately where available. |
| Direction / proposal | A creative target or option; its implementation and approval status must be stated. |
| Open | Something we have deliberately not decided. |

The game design and lore pages are the entry points. The linked source tables
own current spellings and identifiers; scene documents own dialogue and
staging; the vehicle roster owns manufacturer identities. Older planning
documents remain useful for rationale but may describe earlier names or
unfinished packages. Resolve a conflict against those specific sources and
update the affected summary in the same pass.

## Detailed design references

| Area | References |
| --- | --- |
| Opening and first mission | [Johnny's opening script](design/johnny-mercer-opening.md), [New Game handoff](game-opening.md), [Devon delivery scene](mission1-delivery-cutscene.md), [Lou's page and the payphone](pager.md) |
| Driving and traffic | [Driving extremes](driving-extremes.md), [Traffic realism](traffic-realism.md), [Junction validation](traffic-junction-validation.md), [Headlights](traffic-headlights.md), [Horns](traffic-horns.md) |
| Police response | [Pursuit](police-pursuit.md), [Officers](police-officers.md); current behavior is summarized in [Game design](design/README.md) |
| Vehicles and identity | [Manufacturers](design/vehicle-brands.md), [State plates](design/license-plates.md), [Rodeo emblem implementation](assets/rodeo-grazer-emblem.md), [Tractor and trailer](tractor-trailer.md) |
| Weather and persistence | [Snowplows](snowplows.md), [Save games](save-games.md) |
| Narrative tools | [Cutscene Studio](cutscene-studio.md) |
| World review | [Map viewer](map-viewer.md), [Neighborhood backlog](neighborhood-backlog.md) |

## Regional design references

The [world guide](lore/world.md) gives current naming and context. These are
the deeper authored plans; read each one's scope and status before treating
a proposed site or activity as present in the game.

| Region | Starting documents |
| --- | --- |
| Pinatty, O'Haven | [Original city plan](design/pinatty.md), [Hospital campus](design/hospital-campus.md), [Hospital overhaul](design/hospital-overhaul-master.md) |
| Pinatty neighborhoods | [Loom cultural quarter](design/loom-cultural-quarter.md), [Westmere Estates](design/westmere-estates.md), [East Arm Galleria](design/east-arm-plaza.md) |
| O'Haven outskirts | [Tidewater Farm](design/tidewater-farm.md), [Halberd Field](design/halberd-field.md) |
| Florangia | [Terrain and biome foundation](design/florangia.md) |
| Miandi | [City master plan](design/miandi.md), [Resort-city art and venue identities](design/miandi-vice-city-visual-direction.md), [Nightlife expansion](design/miandi-nightlife-expansion.md) |

## Adding to the reference

Give each new entry its purpose, established facts, open choices and links to
the relevant scene, asset or source table. Update the appropriate index here
and in Design or Lore. Keep names and creative direction in repository docs;
use the ignored `build/` directory for temporary review renders, not as the
only copy of a decision or emblem.

Changes to money, mission rewards, a world-wide date, faction history or a
character's motives need an explicit design or story decision. A plausible
idea can be written down as a proposal without becoming canon.
