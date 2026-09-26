# Probable Cause playtest experiment

A local tool for Codex and Claude Code to control the real game through SDL
keyboard events. Jev chooses short actions from structured telemetry. The tool
also includes a fixed script and a rule-based controller for comparison.

Python 3.9+ standard library only; build the game first:

```sh
cmake --build build --target apricot -j 4
python3 -m unittest discover -s tools/playtest -p 'test_*.py' -v
python3 tools/playtest/playtest.py compare --repeats 2
python3 tools/playtest/playtest.py run --policy jev
```

Each run launches an unfocused game with an isolated save, clear daylight,
a fixed starting pose facing out of the opening parking lot, and a recorded
seed. Normal gameplay never enables the bridge. Tests operate in real time;
the world continues during API calls. Each policy receives the same initial
setup; wall-clock input timing means runs are not bit-identical replays.

## Agent tools

Start the MCP stdio server with `python3 tools/playtest/playtest.py mcp`.
It exposes:

- `probable_cause_run_test(policy)`: the whole measured scenario in one call.
  Choose `jev`, `rules`, or `scripted`. The fast loop stays inside the tool.
- `probable_cause_start()`: launch a private game session.
- `probable_cause_observe()`: fresh telemetry and a screenshot.
- `probable_cause_act(action, milliseconds)`: ordinary game inputs, 30–2000 ms.
- `probable_cause_jev_step(goal)`: one Jev decision and its resulting input.
- `probable_cause_stop()`: close only the tool's own game.

Example agent request: "Use probable_cause_run_test with policy jev. Inspect
the final screenshot and report the measured result, any damage, and latency."

For Claude Code, from this checkout:

```sh
claude mcp add --scope local probable-cause-playtest -- python3 "$PWD/tools/playtest/playtest.py" mcp
```

For Codex, merge this into the checkout's `.codex/config.toml`, replacing the
path with this checkout's absolute path. Keep machine-specific config local.

```toml
[mcp_servers.probable-cause-playtest]
command = "python3"
args = ["/absolute/path/to/pengine-apricot/tools/playtest/playtest.py", "mcp"]
startup_timeout_sec = 10
tool_timeout_sec = 180
```

Start a new agent session to load the server. Claude Desktop can use the same
command and arguments in its `mcpServers` configuration. This is a local stdio
server; it does not expose the game to the internet or install a ChatGPT web
connector. The latter would require a separate authenticated remote transport.

## Credentials

Set `TYPESAFE_API_KEY`, or put the key alone in
`~/.config/probable-cause/jev.key` with file permissions `0600`. It stays outside
the repository and is not passed to the game. The only model endpoint is
`https://api.typesafe.ai/v1/systemone`, pinned to `jev-1.13.0`.

Rotate/revoke the key at <https://console.typesafe.ai>, then replace that local
file or environment variable and restart the MCP server. No key belongs in MCP
config, logs, screenshots, or source control.

## Evidence and limits

Outputs live under `build/playtests/` unless `--output` selects another folder:

- `game.log`: engine diagnostics.
- `actions.jsonl`: before/after state and executed input durations.
- `decisions.jsonl`: policy stages, Jev requests, typed responses and latency.
- PNGs: actual game framebuffer at action completion.
- `result.json` / `comparison.json`: measured results.

The scenario requires walking, returning beside the car, completing entry,
moving the car at least three metres, stopping, and completing exit. Taking
more than one point of car damage fails the experiment. This tolerance allows
minor settling; it is not a claim of zero contact. Survival, renderer errors,
and clean process exit also matter. The fixed script deliberately uses fixed
durations; the rule controller and Jev react to current state. Compare success
and total time, not just API latency. Repeats use successive seeds.

Inputs expire even if the controller disappears. The game session exits after
three minutes. Network calls time out after ten seconds; responses older than
one second are discarded in favor of releasing controls or braking. Low Jev
confidence releases controls. Bridge files live in a private temporary folder,
with atomic command/state writes. The server owns one game per connection;
avoid running several clients or comparisons at once.

Jev cannot see screenshots. It gets position, speed, entry availability,
transition state, health, recent actions and a simple forward collision probe.
That probe is not a complete perception/navigation system. Visual bug checking
still belongs to the parent agent. This first experiment does not establish
city-navigation, combat, mission completion, OS keyboard injection, or support
for every vehicle type. SDL injection tests the game's normal input handling,
not the operating system's keyboard delivery.

Research: [TypeSafe state](https://docs.typesafe.ai/concepts/state),
[API](https://docs.typesafe.ai/api),
[known limitations](https://docs.typesafe.ai/model-jaggedness/jev-1.13),
[MCP tools](https://modelcontextprotocol.io/specification/2024-11-05/server/tools).
