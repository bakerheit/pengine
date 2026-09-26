#!/usr/bin/env bash
#
# The version rules, in one place. `VERSION` holds a semver; CMake reads it into
# project() and APRICOT_VERSION. The git hooks in .githooks/ are thin wrappers
# that call into this script, so the rules cannot drift between them.
#
#     tools/version.sh                     print the working-tree version
#     tools/version.sh bump <level>        stage VERSION one <level> past HEAD's
#     tools/version.sh install             point git at .githooks/ (ci.sh does it)
#     tools/version.sh hook <name> [...]   called by .githooks/<name>
#
# Levels, and the full reasoning, are in docs/versioning.md. In short: every
# commit made on main advances VERSION. The committer picks the level with
# BUMP=patch|minor|major (default patch), or by staging a bump by hand; a
# commit that changes kReplayTapeVersion is forced up to at least minor while
# the major is 0, and to major after that, because it strands every recorded
# tape.
#
# Written for the bash 3.2 that macOS ships: no associative arrays, no mapfile.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
TAPE_HEADER="src/core/replay_tape.h"
ZERO_SHA="0000000000000000000000000000000000000000"

die() { printf 'version: %s\n' "$*" >&2; exit 1; }
say() { printf 'version: %s\n' "$*" >&2; }

is_semver() { [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; }

# VERSION as recorded at a revision, or in the index for ":". Empty if absent.
version_at() {
    local spec="$1:VERSION"
    [[ "$1" == : ]] && spec=":VERSION"
    git show "$spec" 2>/dev/null | head -n 1 | tr -d '[:space:]' || true
}

# Prints -1, 0 or 1.
compare() {
    local a1 a2 a3 b1 b2 b3 pair
    IFS=. read -r a1 a2 a3 <<<"$1"
    IFS=. read -r b1 b2 b3 <<<"$2"
    for pair in "$a1 $b1" "$a2 $b2" "$a3 $b3"; do
        set -- $pair
        if (( 10#$1 < 10#$2 )); then echo -1; return; fi
        if (( 10#$1 > 10#$2 )); then echo 1; return; fi
    done
    echo 0
}

max_version() { if [[ "$(compare "$1" "$2")" == -1 ]]; then echo "$2"; else echo "$1"; fi; }

next_version() {
    local x y z
    IFS=. read -r x y z <<<"$1"
    case "$2" in
        patch) echo "$x.$y.$((z + 1))" ;;
        minor) echo "$x.$((y + 1)).0" ;;
        major) echo "$((x + 1)).0.0" ;;
        *) die "unknown level '$2' (want patch, minor or major)" ;;
    esac
}

rank() { case "$1" in patch) echo 1 ;; minor) echo 2 ;; major) echo 3 ;; *) echo 0 ;; esac; }

# Which single step takes $1 to $2, or nothing if no single step does.
step_between() {
    local level
    for level in patch minor major; do
        if [[ "$(next_version "$1" "$level")" == "$2" ]]; then echo "$level"; return; fi
    done
}

# The lowest level the staged change is allowed to take, measured against
# commit $2. A replay-tape format change breaks every recorded tape, which is
# this project's one hard compatibility promise (saves read every older
# version, so they are not one). The diff is captured before grep sees it:
# under pipefail, `git diff | grep -q` fails whenever grep quits early.
required_level() {
    local diff
    diff="$(git diff --cached -U0 "$2" -- "$TAPE_HEADER")"
    if grep -qE '^\+.*kReplayTapeVersion[[:space:]]*=' <<<"$diff"; then
        if [[ "${1%%.*}" == 0 ]]; then echo minor; else echo major; fi
    else
        echo patch
    fi
}

on_main() { [[ "$(git symbolic-ref --quiet --short HEAD 2>/dev/null || true)" == main ]]; }

# Highest VERSION among the commits named in MERGE_HEAD (one per line; more
# than one for an octopus merge). Empty when no merge is in progress.
merge_head_version() {
    local file best="" sha v
    file="$(git rev-parse --git-path MERGE_HEAD)"
    [[ -f "$file" ]] || return 0
    while read -r sha; do
        v="$(version_at "$sha")"
        is_semver "$v" || continue
        if [[ -z "$best" ]]; then best="$v"; else best="$(max_version "$best" "$v")"; fi
    done <"$file"
    echo "$best"
}

# A pre-commit hook is not told about --amend. The parent process is the git
# that is committing, so its argv is the only place the flag is visible.
is_amend() {
    local args
    args="$(ps -o args= -p "$PPID" 2>/dev/null || true)"
    grep -qE -- '(^|[[:space:]])--amend([[:space:]]|$)' <<<"$args"
}

write_version() {
    printf '%s\n' "$1" >"$ROOT/VERSION"
    git add -- "$ROOT/VERSION"
}

# Checks a hand-staged VERSION against the base it must be one step past.
accept_staged() {
    local base="$1" staged="$2" need="$3" level
    level="$(step_between "$base" "$staged")"
    [[ -n "$level" ]] || die "VERSION is staged at $staged, which is not one step past $base. \
Stage exactly one of $(next_version "$base" patch), $(next_version "$base" minor) or $(next_version "$base" major)."
    if (( $(rank "$level") < $(rank "$need") )); then
        die "this commit changes kReplayTapeVersion, so it needs at least a $need bump; $staged is a $level."
    fi
    say "$base -> $staged ($level, staged by hand)"
}

hook_pre_commit() {
    on_main || return 0
    git rev-parse -q --verify HEAD >/dev/null || return 0   # the very first commit

    local base staged need level merge_v from=HEAD
    if is_amend && git rev-parse -q --verify HEAD^ >/dev/null; then
        from=HEAD^                                           # re-bump from the parent
    fi
    base="$(version_at "$from")"
    merge_v="$(merge_head_version)"
    [[ -n "$merge_v" ]] && base="$(max_version "$base" "$merge_v")"
    is_semver "$base" || die "HEAD's VERSION is not a semver ('$base')."

    staged="$(version_at :)"
    is_semver "$staged" || die "the staged VERSION is missing or not a semver ('$staged')."
    need="$(required_level "$base" "$from")"

    case "$(compare "$staged" "$base")" in
        1)  accept_staged "$base" "$staged" "$need"; return 0 ;;
        -1) [[ -n "$merge_v" ]] || die "VERSION is staged at $staged, behind HEAD's $base." ;;
    esac

    level="${BUMP:-patch}"
    (( $(rank "$level") > 0 )) || die "BUMP='$level'; want patch, minor or major."
    if (( $(rank "$level") < $(rank "$need") )); then
        say "this commit changes kReplayTapeVersion; raising the bump from $level to $need."
        level="$need"
    fi
    write_version "$(next_version "$base" "$level")"
    say "$base -> $(next_version "$base" "$level") ($level)"
}

# `git commit -- <paths>` commits from a temporary index, so the bump the
# pre-commit hook staged never reaches the real one, and the next `git status`
# shows VERSION staged back to its old value. Put the real index back in step.
hook_post_commit() {
    on_main || return 0
    local head_v
    head_v="$(version_at HEAD)"
    if [[ "$(version_at :)" != "$head_v" && "$(head -n 1 "$ROOT/VERSION" | tr -d '[:space:]')" == "$head_v" ]]; then
        git reset -q HEAD -- VERSION
    fi
}

# A merge that git commits by itself gives no hook a chance to add a file, so a
# clean merge into main cannot bump. Refuse it and say how to redo it so the
# pre-commit hook can.
hook_pre_merge_commit() {
    on_main || return 0
    local base staged merge_v
    base="$(version_at HEAD)"
    merge_v="$(merge_head_version)"
    [[ -n "$merge_v" ]] && base="$(max_version "$base" "$merge_v")"
    staged="$(version_at :)"
    if is_semver "$staged" && [[ "$(compare "$staged" "$base")" == 1 ]]; then
        accept_staged "$base" "$staged" "$(required_level "$base" HEAD)"
        return 0
    fi
    die "a merge into main has to take its own version, and git gives a merge
commit no hook that can add one. Back it out and let the commit bump it:

    git merge --abort
    git merge --no-ff --no-commit <branch>   # or: git pull --no-commit
    git commit --no-edit                  # BUMP=minor for a feature"
}

# The backstop for everything the other hooks cannot see: fast-forwards,
# cherry-picks, rebases, --no-verify, and `git push origin <branch>:main` from a
# worktree. Whatever lands on the remote's main must be ahead of what is there.
hook_pre_push() {
    local local_ref local_sha remote_ref remote_sha new old
    while read -r local_ref local_sha remote_ref remote_sha; do
        [[ "$remote_ref" == refs/heads/main ]] || continue
        [[ "$local_sha" != "$ZERO_SHA" && "$remote_sha" != "$ZERO_SHA" ]] || continue
        git cat-file -e "$remote_sha^{commit}" 2>/dev/null || continue   # unfetched: git refuses it anyway
        new="$(version_at "$local_sha")"
        old="$(version_at "$remote_sha")"
        is_semver "$new" || die "the VERSION being pushed to main is not a semver ('$new')."
        is_semver "$old" || continue
        if [[ "$(compare "$new" "$old")" != 1 ]]; then
            die "main on the remote is at $old and this push leaves it at $new.
Every landing on main advances VERSION. Stage a bump and commit it first:

    tools/version.sh bump patch           # or minor / major
    git commit -m \"Bump version to \$(tools/version.sh)\""
        fi
    done
}

cmd_bump() {
    local level="${1:-}" base staged
    (( $(rank "$level") > 0 )) || die "usage: tools/version.sh bump patch|minor|major"
    base="$(version_at HEAD)"
    is_semver "$base" || die "HEAD's VERSION is not a semver ('$base')."
    staged="$(version_at :)"
    [[ "$staged" == "$base" ]] || die "VERSION is already staged at $staged (HEAD has $base). Unstage it first: git restore --staged --worktree VERSION"
    write_version "$(next_version "$base" "$level")"
    say "staged $base -> $(next_version "$base" "$level") ($level)"
}

cmd_install() {
    if [[ "$(git config --local core.hooksPath 2>/dev/null || true)" != .githooks ]]; then
        git config --local core.hooksPath .githooks
        say "git hooks now run from .githooks/ (core.hooksPath)"
    fi
}

case "${1:-}" in
    "")      head -n 1 "$ROOT/VERSION" | tr -d '[:space:]'; echo ;;
    bump)    cmd_bump "${2:-}" ;;
    install) cmd_install ;;
    hook)
        case "${2:-}" in
            pre-commit)       hook_pre_commit ;;
            post-commit)      hook_post_commit ;;
            pre-merge-commit) hook_pre_merge_commit ;;
            pre-push)         hook_pre_push ;;
            *) die "unknown hook '${2:-}'" ;;
        esac ;;
    *) die "usage: tools/version.sh [bump <level> | install | hook <name>]" ;;
esac
