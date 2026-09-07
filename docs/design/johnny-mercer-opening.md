# Johnny Mercer — The lotto

Draft 7. Interior opening at Halloway Gas. The Cutscene Studio preview runs 69.41 seconds with nineteen GPT-Realtime-2.1 voice takes, subtitles, a walk behind the counter and a box pickup/handoff. Johnny uses cedar; Lou uses ash with a rough-edged, relaxed manager delivery. Voice performances remain a first take for review.

## Staging

Johnny and Lou are inside the store, facing each other across the checkout counter, about 2.45 metres apart. Johnny is on the working side of the register; Lou is on the shop side. Keep the delivery conversational, at normal indoor volume.

Open on both of them. Cut to restrained singles as the story turns. Keep every camera on the same side of their shared eyeline. No calling across the forecourt or doorway shots.

Lou remains a working name for the manager. Johnny retains the approved driver voice direction.

## Script

Johnny glances toward the lotto display. Lou is occupied with the counter, listening without much interest.

**JOHNNY:** Lotto's up to twelve million.

**LOU:** Don't waste your money.

**JOHNNY:** Somebody's gotta win.

**LOU:** I hit it once. Never played again.

Johnny looks at him properly now.

**JOHNNY:** You? How much?

**LOU:** Enough to buy this place.

**JOHNNY:** No shit. What'd you do, pick birthdays?

**LOU:** Didn't pick numbers. Picked a truck.

**JOHNNY:** A truck?

**LOU:** Had the bank's name on the side.

A beat. Johnny gets it. Lou offers no further explanation.

**JOHNNY:** Right.

Lou checks the time and moves the conversation along.

**LOU:** Anyway. I gotta go make a delivery.

**JOHNNY:** Let me do it.

**LOU:** You?

**JOHNNY:** Yeah. I've been in here all day. Let me get out of the store for a bit.

Lou is about to refuse. Something occurs to him. He starts referring to whoever is waiting for the delivery, then catches himself.

**LOU:** Ya, actually, they... that might not be a bad idea.

Hold on Lou long enough to notice that Johnny volunteering has changed his calculation. Johnny hears permission to leave. The audience hears the unfinished thought.

Lou walks around the east end of the checkout and behind the counter, speaking as he goes.

**LOU:** You just need to take this to the Ostend docks up north west a bit.

Lou reaches for the taped box on the counter, lifts it, turns toward Johnny and passes it into his hands. Johnny takes its weight before Lou lets go.

**JOHNNY:** Okay I think I know where that is.

**LOU:** Devon will be working in the bait and tackle shop, just let him know I sent you.

Johnny keeps hold of the box as Lou finishes the instructions.

## Performance direction

Johnny begins bored and daydreaming. His interest in the lotto story is real; do not turn every reply into a punchline. He volunteers for the delivery because he wants out of the store, without yet knowing what it involves.

Lou treats the old robbery as something settled and unremarkable. The bank-truck line is an implication, not a big reveal or a boast. His last line needs a distinct hesitation after “they,” followed by a casual recovery.

The delivery goes to Devon at the bait and tackle shop at Ostend docks, northwest of the station. The contents of the box and Lou's motives remain unstated.

## Mission handoff

Johnny takes over Lou's delivery: carry the box to Devon at Ostend Bait & Tackle, a little northwest. Box contents, transport and payment remain open. New Game now plays this scene in the game, then returns control to Johnny inside the store and activates the delivery objective. The package is represented by mission state after the handoff. Devon is persistent inside the shop; the mission marker leads to him, and an on-foot interaction completes and saves the delivery. A reward and visible carry animation are still to be authored.

## Preview scope

The viewer stages both existing character models inside the real gas-station interior, using its checkout, textures, and ceiling-light positions. Eleven shots cover the exchange and delivery handoff. The shot and subtitle timing now follows the recorded dialogue, with small conversational gaps and a final hold. The first sixteen originals and balanced WAVs are under `assets/audio/dialogue/johnny_lotto/take_01/`; the new recordings, full 69.41-second listening reel and current timing metadata are under `assets/audio/dialogue/johnny_lotto/delivery_01/`. Playback copies receive constant per-take volume adjustments; original takes are untouched.

Twenty dialogue-timed gestures now blend into the idle clips: Johnny uses open-handed explanations, shrugs and a hand toward his chest when volunteering; Lou uses smaller dismissive motions and nods. Both glance and shift their upper-body posture. Feet remain planted during the conversation. Lou's walk and the pickup, transfer and Johnny's hold retain priority over gestures. Specific prop interactions such as checking a watch, facial acting, finger animation and lip sync remain future work. The earlier exterior study is preserved under `assets/cutscenes/archive/`.

The delivery recordings and complete listening reel are in `assets/audio/dialogue/johnny_lotto/delivery_01/`. `tools/stage_johnny_delivery.py` reproduces the staging from the archived voiced lotto scene and the three recordings. It also builds the original low-poly cardboard box asset. The v3 scene format stores action keys and conversation gestures so the pass can be replayed or scrubbed from any time. Older v1 and v2 scenes still load.



The parcel is now a single 42 x 30 x 10 cm cuboid (12 triangles) with an image-generated diffuse atlas, `models/props/delivery_box/cardboard_generated.png`. Lou lifts it, rotates it outward in front of his body, then passes it to Johnny. The action test now checks a torso clearance envelope through the entire turn, alongside wrist contact and the route around the counter.

Current casting: every Lou line uses Ash from `audio/dialogue/johnny_lotto/lou_ash_04/balanced/`. Direction is a lightly raspy working-class voice, relaxed and dryly amused, with clear everyday speech. No pitch, speed or pause edits are applied; only constant volume matching. Johnny keeps the original Cedar recordings. The current listening reel and timing manifest are in `lou_ash_04/`. Previous Ballad takes remain available.
