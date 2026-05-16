# Risk register & working doctrine

This document captures patterns this project has learned from its own work, risks we're tracking but haven't acted on, and process principles that have earned their place. It's not a public-facing document — it's institutional memory for the team.

Think of this as a living document, not a graveyard. Entries either get acted on, get archived when they stop being true, or stay current because they keep being useful. If something sits here for six months untouched, that's a signal — either it's not actually a risk, or we've been avoiding it.

The voice is deliberate: pattern observations rather than rules, principles rather than commandments. Future contributors should read this and feel they've been handed knowledge, not handed a policy document.

---

## Part 1: Working doctrine

These are principles the project has learned through doing the work. They earned their place by paying off concretely, often multiple times. Many originate in specific tickets noted in parentheses or in body text.

A note about the corpus: most of this doctrine emerged during the first month of work on a small team. A small corpus produces rich lessons but also means single tickets sometimes appear under multiple principles. PBD-015 (the doubled-wheels regression) is the clearest example — it tested premise verification *and* demonstrated the latent-state pattern. That's a feature of how lessons accumulate, not a citation error.

### Build the net, then walk the tightrope

Before refactoring code without test coverage, build the characterization tests first. The refactor itself should be uneventful — the safety net's job is to never need to fire.

PBD-006 → PBD-007 is the canonical example: characterization tests written specifically for the upcoming traffic.cpp split. The tests held the green bar through five extractions, never reported a failure, and were exactly the artifact that made the refactor safe to do.

This sequencing is doctrine for any future refactor of comparable scope.

### Premise verification before implementation

Tickets whose premise depends on quantitative claims about the codebase (counts, presence, absence) benefit measurably from a quick verification step before implementation begins. A grep takes thirty seconds. Discovering the premise was wrong after a day of work costs a day.

Validated three times during the project's first month:
- **PBD-004** retired entirely: the premise ("logger is unused, 12 raw print sites") was a grep error. The codebase actually had 133 PE_* call sites and zero raw stdout/stderr writes.
- **PBD-003** reshaped before implementation: the premise ("~20 source files, cook them all via glob") collapsed against reality (~125 source files, ~20 in use). The reshape produced the manifest approach instead.
- **PBD-015** resolved in ten minutes: the premise verification surfaced the exact filename convention (`Car5_nowheels.obj`) that explained the doubled-wheels regression.

Apply this for any ticket whose premise depends on a count or a "this thing is unused / broken / missing" claim. Skip it when the premise is architectural ("traffic.cpp is large") rather than quantitative.

### Exercising code along new paths surfaces latent state

When code that has only ever been exercised one way starts being exercised differently — through automation, through tests, through CI — assumptions that were silently true become visible. The bugs were always there; the new exercise path makes them findable.

Observed instances:
- **PBD-011** (meshconv skinning regression): re-cooking rigged characters via the new build pipeline produced static .emesh files. Hand-cooks worked because someone had verified them once; automation removed the verification step.
- **PBD-006** (GL context requirement): TrafficSystem's "graceful fallback" for missing assets required a working GL context to fall back gracefully. Manual launch always had GL available; the test fixture didn't, until we wired the engine's Window class in.
- **PBD-015** (doubled wheels): manual cooks used `Car5_nowheels.obj` sources; the automated manifest pointed at `Car5.obj`. The naming convention was tribal knowledge that the automation removed.

The three instances are different mechanisms — automation, testing, mismatched manifest — but the same shape: code exercised along a path it hadn't been exercised on before. PBD-014 (CI) is the predicted next instance; CI will be the third "new exercise path" and will surface something. Pre-budget accordingly.

**Sizing implication (operational, not retrospective):** when a ticket exercises code along a path it hasn't been exercised on before — automation work, new test targets, CI setup, new build environments — multiply the estimate by 1.5x and budget the surplus for follow-up tickets. List likely discovery surface areas in the ticket itself.

Trigger for becoming a ticket-shaped concern: any new automation, test, or build-environment work. Expect at least one latent-state discovery per major ticket of this shape. Pre-budget accordingly.

### Design checkpoint before code

Every meaty ticket has had a pre-work scoping note that the PO thumbs-upped before coding started. Each one caught something material:
- PBD-003's source-tree-vs-manifest reality (and the resulting design pivot)
- PBD-006's Car-field-write-for-setup question (and the explanatory comment template)
- PBD-007's commit-shape decision (five commits, not one)

Cost: ~20 minutes per ticket. Value: multi-day saves on at least three of the above.

This pattern survives team scaling. Do not let new devs skip it on the grounds of "small ticket" — small tickets are where premises go undertested.

### Tiered ticket structure by size

The PBD-007 ticket template (premise verification, full acceptance criteria, scope boundaries, time-box, rationale) was right for an L-size refactor. Applying it to a one-day ticket would crush velocity.

**Tiering principle:**
- **L and above:** full ticket structure. Premise verification, acceptance criteria, scope boundaries, time-box, rationale, notes for the assignee.
- **M:** lighter version. Premise check + acceptance criteria + scope boundaries. Rationale can be in conversation.
- **S and below:** scoping by conversation. Tickets become artifacts of decisions made elsewhere (chat thread, PR comment, shared doc), not the venue for making them.

**Triggers for upgrading from lower to higher tier, regardless of size:** premise unclear, scope contested, design implications beyond the work itself, multiple plausible approaches with different downstream costs. Any contributor can request the upgrade without it feeling like escalation.

**To be validated.** This tiering is prescriptive based on PBD-007's experience and the observation that PBD-001 and PBD-002 (both S/XS) got heavy ticket treatment when they probably didn't need it. We haven't yet had an S-sized ticket where we deliberately applied the lighter touch. Revisit this entry after the first 3–4 S/XS tickets to confirm the rhythm works at smaller sizes, or adjust it if "lighter touch" turns out to skip steps we actually needed.

### Explicit out-of-scope sections

Every ticket that's caused scope creep would have caught itself earlier with an explicit out-of-scope section. The "fix bugs in follow-up tickets, not this one" rule has fired during PBD-011 discovery, PBD-015 discovery, and the GL-context surprise during PBD-006.

Without it, every ticket creeps into adjacent fixes. With it, the work stays bounded and discoveries get filed correctly.

Continue applying. Specifically include the "if you discover a bug while doing this work, pin it and file a follow-up" pattern wherever the work has characterization-test or refactor shape.

### Credit both halves

When work goes well, name both the structural assistance and the human judgment. The ticket structure makes problems legible; the engineer's accumulated context recognizes the answers. Either alone is much weaker than the combination.

The risk this guards against: when contributors under-credit themselves, future-them reading the history won't know to protect the part that depends on individual judgment. Discipline that gets credit gets reinforced; judgment that doesn't get credit gets eroded.

This is a habit, not a process. New contributors will reflexively under-credit themselves. Continue catching it.

### No second-guessing after sign-off

Once a ticket is assigned and signed-off, the PO doesn't poke at progress or revise the spec. Engineers get space to think. Mid-work redirection from the PO is one of the most common ways a PO-engineer relationship goes sour.

The exception is "the engineer surfaces a finding that reshapes the work" — at which point the conversation reopens, but on the engineer's initiative. PBD-003's manifest pivot, PBD-006's GL-context surprise, PBD-015's quick resolution all worked because the engineer felt licensed to pause and surface findings.

### Resize, don't grind

Time-boxes are real. When work takes longer than the box predicted, the correct response is to stop and report, not to push through. The information ("this is bigger than we thought") is more valuable than the partial result of grinding.

Has fired correctly in:
- **PBD-003**, when the meshconv skinning regression surfaced — the work stopped, PBD-011 was filed, PBD-003 resumed cleanly afterward.
- **PBD-006** would have applied if the fixture had taken longer than 5 seconds to construct (it didn't, but the rule was in place).

Continue applying. Do not reward grinding; reward correct stopping.

### Context-capture at ticket completion

At the end of each ticket, ask: "What context did this ticket build in my head that isn't in any artifact? If something, write it down."

Costs nothing when the answer is "none." Captures real value when there is something. Mitigates the fragility of contributor-specific deep context — the kind that compounds during sequential work in the same code region (PBD-003 → PBD-006 → PBD-007 → PBD-011 → PBD-015 were all the same code region; the compounding context made the engineer fast, but losing the engineer would have lost the context).

Operationalize as a checklist question, not a separate process step.

### Distribute review load

The "one reviewer for everything" pattern works at team size two. It does not scale. Structural risk: when the team grows, the original reviewer calcifies into a gatekeeper position, and the deep trust built one-on-one becomes a chokepoint instead of an asset.

**Structural requirement:** new engineers participate in review by their first sprint, on low-stakes work (docs, risk register updates, small bugs). Review is taught, not earned. Pair new devs to review each other's work on entry-level tickets so the pattern of review becomes normal practice rather than something reserved for senior contributors.

**Trigger for retiring this entry:** when at least three engineers other than the original reviewer have shipped reviews on at least three tickets each, the load is genuinely distributed. Until then, this entry stays active as a structural reminder.

### Engineers should propose work, not only execute it

If every ticket originates with the PO, engineers stop proposing. The pattern calcifies into "the PO tells us what to do." That dynamic is bad for engineer judgment, bad for product instinct, and bad for the rate at which the team learns about its own codebase.

**Shape (not yet fully designed):** a bounded mechanism for engineers to pick up curiosity-shaped or hygiene-shaped work — investigations into testability concerns, exploratory tooling, refactor prep, documentation passes. Bounded time (a day, maybe two). Bounded scope. Results shared with the team regardless of outcome.

This needs to be co-designed with the incoming devs, not imposed on them. Filed as something to act on, not something already resolved.

### Playtest, don't just launch

There is a category of bug that automation cannot catch. Visual regressions like the doubled wheels (PBD-015) were caught by a human, not by tests or smoke checks. Experiential issues — timing, feel, response, "is this fun" — show up only when someone plays the game with intent rather than launching it with a checklist.

**Process recommendation:** the game should be played, not just launched, on some regular schedule. By whom is open: the team, friends and family, eventually a small playtest group. The point is that "launches cleanly" is necessary but radically insufficient as a definition of working.

This becomes meaningful only once there's a coherent slice to playtest against (see PBD-018, the Vertical Slice Definition). Until then, "playtesting" is just "trying the engine demo." But the framework should be in place so that as soon as the slice exists, the playtest cadence is too.

---

## Part 2: Known risks (active)

Items the team has identified but isn't yet acting on. Each has a "trigger" — the condition that would promote it from risk-register entry to ticket.

### Heightmap singleton state is a testability smell

The Heightmap is currently accessed as singleton-style state. This bit the PBD-006 fixture work (required wiring the engine's Window class) and will bite again the next time someone writes a system-level test that needs Heightmap isolation.

By "system-level test" we mean a test that instantiates a real engine subsystem and exercises its public API — the pattern established in `tests/traffic_system_tests.cpp`. Contrast with helper-level tests in `tests/traffic_ai_tests.cpp`, which test pure functions and small types without engine scaffolding.

**Trigger for ticketing:** anyone writes a system-level test that needs to test a Heightmap-free or alternate-Heightmap configuration. Or a second system-level test target gets created that conflicts with the existing one's Heightmap setup.

### Empty src/ai/ is accruing organizational debt

The directory exists but is unused. Gameplay AI lives under src/game/ (pedestrian_ai.cpp, traffic_drive.cpp, etc.). This is fine for two systems; it gets worse with each new AI system added in the wrong place.

**Trigger for ticketing:** a third gameplay AI system gets added (combat AI, animal AI, NPC dialogue AI, anything beyond peds and traffic). At that point the extraction is worth doing because there are genuine shared primitives to lift.

### Direct-to-main with no PR workflow

Works at team size two with high trust. Breaks the day a new dev's third commit breaks main and nobody notices for a day.

**Trigger for ticketing:** any of (a) a regression slips into main and isn't caught for >24h, (b) team size grows past three engineers, (c) PBD-014 (CI) lands and we want PR-based status checks. Whichever fires first.

When raised, framing should be "here's what happened, here's what would have helped" rather than "we should mandate PRs." Concrete trigger beats abstract policy.

**Trigger fired 2026-05-16:** team crossed three engineers (Frank, Marin, Mira, Priya all shipping concurrently), with Frank/Marin colliding on `Application.cpp` regions on day one. Response is PBD-047 (lightweight PR workflow, two-sprint trial, retro afterward). See `docs/pr_workflow.md`. Entry stays open until the trial retro; revisit if PBD-014 lands inside the window.

### Source art checked into the repo

Tracked as PBD-012 in the active backlog. Not urgent today; gets meaningfully more expensive as the repo grows. The asset directory size estimate of "90+ MB" used earlier in conversation may have drifted — worth running `du -sh assets/` when PBD-012 is picked up and confirming the current figure. **Anticipated latent-state discovery:** PBD-012 will likely surface files that shouldn't have been in git in the first place (`.DS_Store`, build artifacts, accidentally-committed binaries). Pre-budgeted.

### CI requires real GL context

Tests written for the traffic system require a working GL context (per PBD-006's fixture work). Headless Linux CI runners need xvfb or equivalent. macOS CI runners may have a usable GL context out of the box — needs confirmation. Already noted as scope addition for PBD-014.

**Trigger for ticketing:** rolled into PBD-014 when that work begins.

### Visual regression testing is a gap

The doubled-wheels regression (PBD-015) was caught by a human, not by any automated check. Even with CI (PBD-014), we will not catch this class of bug — the game compiled, launched, produced zero [ERROR] lines, and looked wrong.

Not filing as a ticket yet — premature without seeing how often this class of bug actually bites us. But noting because the "automation will catch our regressions" narrative is incomplete and we should be honest about that.

**Trigger for ticketing:** any of (a) a second visual regression that compiled clean but looked wrong, (b) explicit team capacity for tooling work, (c) the project reaches a stage where visual continuity is a shipping concern.

### Build automation will surface more latent state

Two upcoming infrastructure tickets (PBD-012 LFS migration, PBD-014 CI) both exercise code along paths it hasn't been exercised on. Per the "Exercising code along new paths surfaces latent state" doctrine: expect at least one discovery per ticket.

**PBD-012 likely surface areas:** files that shouldn't have been in git, build artifacts in the tree, naming conventions assumed by tools but not by git.

**PBD-014 likely surface areas:** platform-specific assumptions in the build that only the macOS dev environment was papering over, paths assumed to exist, environment variables silently relied on, working-directory assumptions.

Size both tickets at 1.5x. Budget for follow-ups.

### Blocked-ticket theater

Tickets that go into "blocked" status can become permanent state if nobody periodically asks "is this still blocked?" PBD-006 sat in "blocked on traffic WIP" for over a week after the WIP had actually landed.

**Process recommendation:** periodic backlog review should explicitly verify the blocked status of every blocked ticket, not just the ones that look like they might be ready. "Blocked on X" should always be checkable; if the check can't be done quickly, the dependency was probably the wrong shape to track.

### The deep-context problem

A single contributor has been the engineering owner on every engineering ticket so far (PBD-003, PBD-006, PBD-007, PBD-011, PBD-015 — all in the same code region). The compounding context made the work fast; losing the contributor would lose that context.

**Partial mitigation already in place:** docs/assets.md, cmake/CookedAssets.cmake manifest comments, traffic_internal.h's rationale comments, the characterization tests. Together, these capture most of what's been learned in artifacts.

**Ongoing mitigation:** the "context-capture at ticket completion" doctrine above. New contributors entering the same code region will benefit from the captured context.

**Trigger for further action:** if context-capture starts producing "yes, something I learned isn't written down" consistently across multiple tickets, escalate to a dedicated documentation pass.

---

## Part 3: Patterns to watch for

These aren't risks exactly — they're failure modes the team should keep an eye out for. They're listed here so future contributors recognize them when they happen.

### "Process theater" tickets

Tickets that exist to track work that would happen anyway. PBD-005 (originally "Land in-flight traffic.cpp WIP") was the canonical example — the work was going to happen regardless of the ticket. We retired it.

**Rule of thumb:** do not create tickets for "someone will obviously do this thing they're already doing." Tickets are for work that needs tracking, assignment, scoping, or remembering. If it would happen anyway, no ticket.

### Tickets whose premise dissolves under contact

PBD-004 (logger sweep) is the canonical example. The premise was based on grep errors; the ticket couldn't survive its own premise verification. The pattern: a ticket gets written based on a confident claim, the claim turns out to be wrong, the ticket retires.

This is not failure — this is the system working. Premise verification exists specifically to surface these before implementation. Retired tickets keep their numbers (PBD-004, PBD-005, PBD-008) for traceability and to remind future contributors that retirement is normal.

### Vague "future improvement" entries that never get acted on

The risk register can drift toward "things we'd like to fix someday." That's a graveyard. Every entry here should have a trigger — the condition that would promote it from risk to ticket. If you can't write the trigger, the entry probably isn't ready for the register and is better left in someone's head or a chat thread.

### Doctrine that's actually preference

Some of the principles above are universal best practice (premise verification, scope boundaries). Some are choices that work for this team's specific shape (the design-checkpoint rhythm, the way credit gets distributed). New contributors should feel free to question the latter — they're not less true, they're just less general, and they should be deliberately preserved or revised as the team changes shape.

If you find yourself reading something here that doesn't make sense for the current team, that's a signal to raise it, not a signal that you're wrong.

---

## Maintenance

This document is updated when:
- A new pattern earns its place (paid off concretely, not just sounded good)
- A risk is acted on (move the entry, don't delete it — note when it was acted on)
- A risk turns out not to be real (delete it, with a note about why)
- A new team member joins (review the document together; they may have questions that surface gaps)

Last updated: PBD-009 initial draft.