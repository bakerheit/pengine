#!/usr/bin/env bash
#
# Drives real git through the version hooks in a throwaway repo: ordinary
# commits, BUMP levels, --amend, `commit -- <path>`, branches, clean and
# conflicted merges, the replay-tape escalation, and the pre-push backstop.
# Registered in ctest as version_hook_tests; runs anywhere git does.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/apricot-version-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

failures=0
check() {   # check <description> <expected> <actual>
    if [[ "$2" == "$3" ]]; then printf 'ok    %s\n' "$1"
    else printf 'FAIL  %s: expected [%s], got [%s]\n' "$1" "$2" "$3"; failures=$((failures + 1)); fi
}
# "refused" only when the version hook said why; any other failure is a bug.
refused() {
    local out
    if out="$("$@" 2>&1)"; then echo accepted
    elif grep -q '^version: ' <<<"$out"; then echo refused
    else echo "failed without a version message: $out"; fi
}

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
# Run from inside a hook, git's own variables would point at the real repo.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_PREFIX
unset BUMP

git init -q --bare -b main "$WORK/remote.git"
git init -q -b main "$WORK/repo"
cd "$WORK/repo"
mkdir -p tools src/core
cp "$SRC/tools/version.sh" tools/
cp -R "$SRC/.githooks" .
echo 0.1.0 >VERSION
echo 'inline constexpr uint32_t kReplayTapeVersion = 8;' >src/core/replay_tape.h
echo a >a.txt
git add -A && git commit -q -m init
tools/version.sh install 2>/dev/null
git remote add origin "$WORK/remote.git" && git push -q origin main

v() { git show "${1:-HEAD}:VERSION"; }

echo b >a.txt; git commit -q -am patch 2>/dev/null
check "plain commit on main bumps patch" 0.1.1 "$(v)"

echo c >a.txt; BUMP=minor git commit -q -am minor 2>/dev/null
check "BUMP=minor bumps minor" 0.2.0 "$(v)"

echo d >a.txt; git add a.txt; git commit -q --amend --no-edit 2>/dev/null
check "--amend keeps the amended commit's version" 0.2.0 "$(v)"

echo e >a.txt; echo f >b.txt; git add b.txt; git commit -q -m only -- a.txt 2>/dev/null
check "commit -- <path> bumps" 0.2.1 "$(v)"
check "commit -- <path> leaves the index in step" "" "$(git diff --cached --name-only -- VERSION)"
git add -A; git commit -q -m tidy 2>/dev/null

tools/version.sh bump minor 2>/dev/null; git commit -q -m staged 2>/dev/null
check "a hand-staged bump is kept, not doubled" 0.3.0 "$(v)"

echo 0.5.0 >VERSION; git add VERSION
check "a hand-staged jump of two steps is refused" refused "$(refused git commit -q -m jump)"
git restore --staged --worktree VERSION

sed -i.bak 's/= 8/= 9/' src/core/replay_tape.h && rm src/core/replay_tape.h.bak
git commit -q -am tape 2>/dev/null
check "a replay-tape change is raised to minor below 1.0" 0.4.0 "$(v)"

git checkout -q -b feature
echo g >a.txt; git commit -q -am on-branch 2>/dev/null
check "commits off main are not bumped" 0.4.0 "$(v)"
git checkout -q main
echo h >c.txt; git add c.txt; git commit -q -m main-moves 2>/dev/null   # 0.4.1

check "a clean merge into main is refused" refused "$(refused git merge -q --no-edit feature)"
git merge --abort 2>/dev/null || true
git merge -q --no-ff --no-commit feature >/dev/null 2>&1; git commit -q --no-edit 2>/dev/null
check "merge --no-commit then commit bumps" 0.4.2 "$(v)"

git checkout -q -b ff-able
echo k >d.txt; git add d.txt; git commit -q -m ff-able 2>/dev/null
git checkout -q main
git merge -q --no-ff --no-commit ff-able >/dev/null 2>&1; git commit -q --no-edit 2>/dev/null
check "a fast-forwardable branch landed with --no-ff --no-commit bumps" 0.4.3 "$(v)"

git checkout -q -b clash HEAD~2
echo 0.9.0 >VERSION; echo i >a.txt; git commit -q -am clash 2>/dev/null
git checkout -q main
git merge -q clash >/dev/null 2>&1 || true
git checkout --ours VERSION a.txt 2>/dev/null; git add VERSION a.txt
git commit -q --no-edit 2>/dev/null
check "a conflicted merge bumps past the higher parent" 0.9.1 "$(v)"

git push -q origin main 2>/dev/null
echo j >a.txt; git commit -q --no-verify -am unbumped
check "pre-push refuses a main that did not advance" refused "$(refused git push -q origin main)"
tools/version.sh bump patch 2>/dev/null; git commit -q --no-verify -m bump
check "pre-push accepts it once VERSION advances" accepted "$(refused git push -q origin main)"

echo 1.0.0 >VERSION; git add VERSION; BUMP=major git commit -q --no-verify -m one-oh
sed -i.bak 's/= 9/= 10/' src/core/replay_tape.h && rm src/core/replay_tape.h.bak
tools/version.sh bump minor 2>/dev/null; git add -A
check "from 1.0 a replay-tape change refuses a minor" refused "$(refused git commit -q -m tape-minor)"

if (( failures )); then echo "$failures check(s) failed"; exit 1; fi
echo "version hooks: all checks passed"
