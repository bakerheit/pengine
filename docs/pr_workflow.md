# PR workflow — trial scope

This is a trial workflow, not a policy. It's narrowly aimed at the situation we hit today: four engineers shipping concurrent work, with Frank (PBD-049, MapBuilder extraction) and Marin (PBD-051, honesty sweep) both editing regions of `Application.cpp` at the same time. The risk-register entry "Direct-to-main with no PR workflow" had its team-size trigger fire when we crossed three engineers; this doc is the response. If after two sprints it feels like ceremony, we drop it. Pushback during the trial is welcome — see "Doctrine that's actually preference" in the risk register.

## When a PR is required

- Any change that touches a file another in-flight ticket is also touching. Today, that means anything in `src/Application.cpp` or `src/Application.h`.
- Any change sized M or larger (per the tiered ticket structure).
- Anything that changes a public header another subsystem depends on.

## When direct-to-main is fine

- Doc-only changes (`docs/**`, `README.md`, comments-only edits).
- Hotfixes that unblock the team or fix a broken build. Push, then post in the team channel so someone can eyeball after the fact.
- XS/S tickets that touch files no one else is in. Use judgment; if you're not sure, open a PR — it costs five minutes.
- Follow-up tickets surfaced mid-PR (file them, don't fold them in).

## Who reviews

- One reviewer, not two. We're four engineers; gating on two reviewers will stall everyone.
- Default reviewer is whoever else is currently touching the same file or subsystem. Today: Frank and Marin review each other on `Application.cpp` work. Mira reviews streamer work. Priya reviews test infrastructure.
- If nobody obvious, ping in chat and the first available engineer takes it. New devs review too — review is taught, not earned (see "Distribute review load").
- Andrew is not the default reviewer. The chokepoint risk is exactly what the risk register flagged.

## What the bar is

- Build clean from a fresh `cmake --build`.
- Relevant tests green. If your change touches a system with a test target (`traffic_system_tests`, `map_builder_round_trip_tests`), run it.
- For L+ tickets, link the design checkpoint note in the PR description.
- Premise verification, if the ticket needed one, lives in the PR description.
- No code-review checklist yet — that's a separate conversation, filed as future work.

## Same-file collision convention

When you start a ticket that will touch a file someone else is actively in, post in the team channel: **"Starting PBD-NNN, touching `path/to/file.cpp` regions X and Y. Anyone else in there?"** Before opening the PR, repeat: **"PBD-NNN PR up against `path/to/file.cpp`."** The second one is what would have surfaced the Frank/Marin overlap on `Application.cpp` this morning.

This is a signaling mechanism, not a who-wins-when rule. Conflict resolution — who rebases onto whom, whether one ticket waits — is the SDM call when it comes up. The convention just makes sure it comes up before the merge instead of during.

## Trial scope

Two sprints. After that, a retro: did this catch anything real, or was it ceremony? Adjust, keep, or drop. The risk-register entry stays open until then; if PBD-014 (CI) lands inside the trial window, we revisit the bar (status checks change the calculus).

## Out of scope for this doc

GitHub branch-protection config, required-reviewer enforcement, a formal review checklist, and any policy for testing/design-checkpoint thresholds (those live in the risk register). This doc assumes the world without CI. PBD-014 will reshape it.
