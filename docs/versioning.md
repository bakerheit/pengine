# Versioning

`VERSION` at the repo root is a semver and the only place the version lives.
CMake reads it into `project()` and the `APRICOT_VERSION` macro, which the game
logs on its first line, puts in the window title, prints for `--version` and
shows in the overlay. **Every commit made on main advances it by one step.**
The git hooks in `.githooks/` do the bump; the rules are in
[`tools/version.sh`](../tools/version.sh), and
[`tools/test_version.sh`](../tools/test_version.sh) (ctest:
`version_hook_tests`) drives real git through every case below.

## Picking the level

Nothing can work the level out from the diff. A thousand-line vehicle is a
feature and a one-character constant can break every recorded replay, so line
counts say nothing about what a change means. **Whoever commits picks the
level, because they are the only one who knows what the change was.** Pick the
highest line that applies:

| Level | When | Example |
|---|---|---|
| **major** | Breaks something a player already has — after 1.0, a replay-tape format change. Before 1.0, major is the human's call only: 1.0 is the first build worth handing to someone. | `kReplayTapeVersion` 12 → 13 once past 1.0 |
| **minor** | Something new a player can see, hear or do: a system, a vehicle, a building, a mission, a district. Before 1.0, also any replay-tape format change. | the wanted cooldown meter; a new enterable building |
| **patch** | Everything else: fixes, tuning, refactors, docs, tooling, tests, asset touch-ups. **The default.** | re-cutting a collision bound; a README fix |

Saves are not a compatibility line: `save_game.cpp` reads every older save
version, so a save-format bump is a minor at most, not a break.

## How to commit

On main, just commit. The pre-commit hook bumps a patch.

```sh
git commit -m "Re-cut the pawn shop door bound"              # 0.4.2 -> 0.4.3
BUMP=minor git commit -m "Add the Harrow Hookline"            # 0.4.3 -> 0.5.0
```

Or stage the bump yourself, which the hook keeps rather than doubles:

```sh
tools/version.sh bump minor && git commit -m "Add the Harrow Hookline"
```

**Worktree and feature branches do not bump per commit.** If every branch
commit bumped, every merge would conflict on `VERSION`. A branch takes one bump
when it lands:

- **Merging into main:** `git merge --no-ff --no-commit <branch>`, then
  `git commit --no-edit` (with `BUMP=minor` if it is a feature). A plain
  `git merge` that commits by itself is refused: git runs no hook on a clean
  merge commit that is able to add a file, so there is no way to bump it.
- **Pushing a branch straight to main** (`git push origin <branch>:main`, the
  fast-forward landing): on the branch, run `tools/version.sh bump patch` (or
  minor) and commit it as the last commit before the push.

## What enforces it

| Hook | Does |
|---|---|
| `pre-commit` | On main: bumps one `BUMP` level (default patch) past HEAD, or accepts a bump already staged if it is exactly one step. On `--amend` it measures from the parent, so amending does not bump twice. Finishing a merge, it measures from the higher of the two parents. Raises the level if `kReplayTapeVersion` changed. |
| `post-commit` | Re-syncs the index after `git commit -- <paths>`, which commits from a temporary index the pre-commit hook's bump never reaches. |
| `pre-merge-commit` | On main: refuses a clean merge that has not advanced `VERSION`, and prints how to redo it. |
| `pre-push` | Refuses any push to `refs/heads/main` whose `VERSION` is not ahead of the remote's. This is the backstop for everything the others cannot see: fast-forwards, cherry-picks, rebases, `--no-verify` and branch-to-main pushes. Then runs `tools/ci.sh` and refuses the push if it is red (see below). |

`tools/ci.sh` runs `tools/version.sh install` first, which sets
`core.hooksPath` to `.githooks`, so a clone gets the hooks the first time
anyone runs the gate. Worktrees share the setting.

## What it cost, and where it is weak

- **Every merge into main and every conflicting pull is two commands**
  instead of one. That is the price of the version being in a tracked file
  rather than derived from tags; a derived version would not survive a build
  from a tarball or a worktree without tags fetched.
- **Two clones that each commit to main conflict on `VERSION` when one pulls
  the other.** It is a one-line conflict: keep either side and commit, and the
  pre-commit hook bumps past the higher of the two.
- **Per-commit is enforced only for commits made on main.** A fast-forward or
  branch push lands several commits under one bump; `pre-push` guarantees only
  that each landing advances the version.
- **All of it is client-side.** `--no-verify` on both commit and push gets
  past it. A server-side check would close that; a GitHub Action was turned
  down because it costs money, and it could not run the model suites anyway,
  since `assets/models/` is not in git.

## The gate runs before every push to main

On 2026-09-26 two pushes broke main in one morning, a link error and a model
test that stayed red for eleven hours, because nobody ran `tools/ci.sh` first.
So `pre-push` runs it, and a red gate stops the push.

`tools/ci.sh` builds the checkout, not a commit, so the gate only counts when
the checkout **is** the commit being pushed:

- **The pushed commit must be HEAD.** Pushing `main` while another branch is
  checked out is refused; check out what you push.
- **No tracked changes.** They would be built into the gate without being in
  the push, which is how a gate goes green on code that is not landing. Push
  from a clean worktree. Untracked files are ignored.
- **It does not run twice.** A green `tools/ci.sh` on a clean checkout writes
  the tree it passed to `build/gate-passed-tree`, and a push of that same tree
  skips the rerun. Run the gate, then push, and you pay for it once.

What it costs: a push to main whose tree has not been gated waits for a full
build and every suite, several minutes. The stamp does not cover the
gitignored models, so a green stamp stays green if the models change under it.

The gate limits builds to **four jobs** by default. A version change rebuilds
the full tree; unlimited compiler jobs exhausted the local Mac during the
Rearloader landing. Set `CMAKE_BUILD_PARALLEL_LEVEL` to a positive integer to
choose a different limit. Tests remain sequential by default.
