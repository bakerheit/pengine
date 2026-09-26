# Jev playtest experiment — initial local results

The tool and usage instructions are in [tools/playtest](../tools/playtest/README.md).
This is a narrow real-game input experiment, not a navigation benchmark.

## Matched comparison

Command: `python3 tools/playtest/playtest.py compare --repeats 2 --output build/playtest-experiment-final`

Local macOS run using the current modified checkout based on `1d6ac57`.
Each policy started a fresh isolated game at the same parking-lot pose,
facing open space. Repeat seeds were 42 and 43. All policies used the same
SDL event bridge, normal game simulation, and screenshot collection.

| Controller | Successful runs | Mean test time | Vehicle damage |
| --- | --- | --- | --- |
| Fixed script | 2 / 2 | 10.71 s | 0 |
| Rules | 2 / 2 | 7.84 s | 0 |
| Jev 1.13.0 | 2 / 2 | 10.49 s | 0 |

Times exclude game startup and include action waits, screenshots, network
calls when applicable, and shutdown. Jev request medians were 187 ms and
171 ms; the largest request in these two runs was 351 ms. These are two
samples per controller, insufficient for reliable performance rankings.
The rule controller was fastest on this deliberately simple scenario.
No direct GPT/Claude-per-action baseline was measured.

Each pass was checked from game state: walk at least 0.8 metres, return beside
the car, enter, drive at least three metres, stop below 0.3 m/s, and exit.
Player survival, vehicle damage, reported GL errors, and clean process exit
were also checked. Final exit screenshots were visually inspected. This
does not validate all animation, audio, traffic, mission, or rendering paths.

Raw evidence: `build/playtest-experiment-final/comparison.json`, each run's
`actions.jsonl`, `decisions.jsonl`, `game.log`, and screenshots. The extra
`mcp-verification.json` records a successful complete Jev run through the
actual JSON-RPC stdio tool, including an embedded screenshot response.
Claude Code's MCP connection check also succeeded.

## Findings during development

The first fixture faced a storefront. It was unsuitable for a clean straight
drive and was replaced with a pose facing out of the lot. Initial brake
mapping held S plus Space; S engaged reverse after stopping. The bridge now
uses Space alone for its bounded brake action. Failed development runs remain
under `build/playtest-experiment-1` and `build/playtest-experiment-2`; the table
above describes the corrected implementation only.

## Scope of the result

The tool works as a local experiment for Codex and Claude Code. Jev can choose
and execute short game actions using telemetry, while the parent agent receives
screenshots. A complete scenario runs within one tool call, avoiding a parent
model round trip for each keypress. This result does not establish that Jev
outperforms fixed logic or that it can freely navigate the city.

Useful next experiments are blocked vehicle entry, recovery from an obstacle,
animated-door vehicles, and a bounded route with multiple valid paths. Those
need their own observable pass conditions before comparing policies.
