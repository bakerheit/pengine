#!/usr/bin/env bash
#
# Read back a session's frame log.
#
#   tools/perf_report.sh                  # the newest session, wherever it is
#   tools/perf_report.sh build/perf/session-004.csv
#   tools/perf_report.sh --worst 40       # show more of the slow frames
#
# The CSV carries its own summary in the `#` trailer, including the phase
# breakdown that says WHERE a frame went. This adds the two views a trailer
# cannot hold: a second-by-second timeline, and the slowest frames with their
# phases spelled out. Most investigations end in one of the two.
#
# Columns are looked up BY NAME from the header line, never by position. That
# is what lets a column be added to the recorder without this script silently
# reading the wrong number out of every older log on disk — a failure that
# produces confident, wrong answers rather than an error.
set -euo pipefail

cd "$(dirname "$0")/.."

WORST=20
FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --worst) WORST="$2"; shift 2 ;;
        -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) FILE="$1"; shift ;;
    esac
done

# Sessions live beside the save game, because that is the one folder that is
# writable no matter how the app was launched. build/perf is still searched so
# logs recorded before that changed still open.
PERF_DIRS=(
    "$HOME/Library/Application Support/Bakerheit/Probable Cause/perf"
    "build/perf"
)

if [ -z "$FILE" ]; then
    # Newest by mtime, not by name: past session-999 the numbering stops
    # sorting the way it reads.
    # Newest by mtime, compared explicitly rather than piped through
    # `ls -1t | head`. That pipeline had two ways to fail quietly: under
    # `set -o pipefail` a missing directory made the whole substitution return
    # non-zero, which `set -e` turned into an exit with NO output at all; and
    # an empty `xargs -0 ls` lists the working directory instead of nothing.
    # A report tool that prints nothing and exits 1 is indistinguishable from
    # having no logs, which is the one answer it must never give by accident.
    newest_mtime=0
    while IFS= read -r candidate; do
        [ -f "$candidate" ] || continue
        mtime="$(stat -f %m "$candidate" 2>/dev/null || echo 0)"
        if [ "$mtime" -gt "$newest_mtime" ]; then
            newest_mtime="$mtime"
            FILE="$candidate"
        fi
    done <<EOF
$(for d in "${PERF_DIRS[@]}"; do
      if [ -d "$d" ]; then
          find "$d" -maxdepth 1 -name 'session-*.csv'
      fi
  done)
EOF
fi
if [ -z "$FILE" ] || [ ! -f "$FILE" ]; then
    echo "no frame log found. Play a session (it records by default), or pass a path." >&2
    echo "sessions are written to:" >&2
    for d in "${PERF_DIRS[@]}"; do echo "  $d" >&2; done
    exit 1
fi

echo "=== $FILE ==="
echo

# The trailer first: it is the session in twenty lines, and the recorder wrote
# it rather than this script re-deriving it.
if grep -q '^# ---- session summary' "$FILE"; then
    sed -n '/^# ---- session summary/,$p' "$FILE" | sed 's/^# \{0,1\}//'
else
    echo "(no trailer — the session did not exit cleanly; rows below are still good)"
fi
echo

# One awk shared by both views. Builds the header map first, so every field is
# addressed as f("swap_ms") and a missing column reads as empty rather than as
# whatever happens to sit at that index.
AWK_HEADER='
    function idx(name) { return (name in col) ? col[name] : 0 }
    function f(name,   i) { i = idx(name); return (i > 0) ? $i : "" }
    function num(name,   v) { v = f(name); return (v == "") ? 0 : v + 0 }
    NR == 1 { for (i = 1; i <= NF; i++) col[$i] = i; next }
    /^#/ { next }
'

echo "--- one second at a time (one # per 4 ms of the worst frame; * marks a dip) ---"
awk -F, "$AWK_HEADER"'
    f("ms") == "" { next }
    {
        sec = int(num("t_s"));
        n[sec]++;
        total[sec] += num("ms");
        if (num("ms") > worst[sec]) worst[sec] = num("ms");
        slow[sec] += (num("spike") == 1 ? 1 : 0);
        if (f("place") != "") place[sec] = f("place");
        mode[sec] = f("mode");
        gpu[sec] += num("gpu_ms");
    }
    END {
        for (s = 0; s <= 100000; s++) {
            if (!(s in n)) continue;
            mean = total[s] / n[s];
            bar = "";
            ticks = int(worst[s] / 4);
            if (ticks > 30) ticks = 30;
            for (i = 0; i < ticks; i++) bar = bar "#";
            printf "%6ds %6.1f fps  mean %5.1f  worst %6.1f  gpu %5.1f ms %s%-26s %-18s %s\n",
                   s, (mean > 0 ? 1000 / mean : 0), mean, worst[s], gpu[s] / n[s],
                   (slow[s] > 0 ? "*" : " "), bar, place[s], mode[s];
        }
    }
' "$FILE"
echo

echo "--- the $WORST slowest frames, and where each one went ---"
# Stage 1 resolves every column by name and emits a fixed, known layout with a
# sort key in front. Stage 2 can then be positional and dumb, which is the only
# place positional access is safe: it is reading this script's own output.
awk -F, "$AWK_HEADER"'
    f("ms") == "" || f("mark") != "" { next }
    {
        printf "%012.4f %.1f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %d %s %s %d %d\n",
               num("ms"), num("t_s"), num("sim_ms"), num("sim_traffic_ms"),
               num("sim_police_ms"), num("police_vis_ms"), num("world_ms"),
               num("visual_ms"), num("render_ms"), num("swap_ms"),
               num("unaccounted_ms"), num("gpu_ms"), num("draw_calls"),
               (f("place") == "" ? "-" : f("place")), f("mode"),
               num("cars"), num("npcs");
    }
' "$FILE" | sort -r | awk -v keep="$WORST" 'NR <= keep' | awk '
    BEGIN {
        printf "%8s %6s | %6s %6s %6s %6s | %6s %6s %6s %6s %6s | %6s %6s  %-15s %-6s %5s %5s\n",
               "ms", "t_s", "sim", "traf", "pol", "pol.vis", "world", "visual",
               "render", "swap", "unacc", "gpu", "draws", "place", "mode",
               "cars", "npcs";
    }
    {
        # Place names contain spaces, so rebuild it from the middle fields.
        place = "";
        for (i = 14; i <= NF - 3; i++) place = place (place == "" ? "" : " ") $i;
        printf "%8.2f %6.1f | %6.2f %6.2f %6.2f %6.2f | %6.2f %6.2f %6.2f %6.2f %6.2f | %6.2f %6d  %-15.15s %-6s %5d %5d\n",
               $1 + 0, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13,
               place, $(NF - 2), $(NF - 1), $NF;
    }
'
echo

MARKS="$(awk -F, "$AWK_HEADER"'f("mark") != "" { printf "  t=%.1f s  (%.0f, %.0f)  %s  %s\n", num("t_s"), num("x"), num("z"), f("place"), f("mark") }' "$FILE" || true)"
if [ -n "$MARKS" ]; then
    echo "--- moments you marked with F4 (grep the rows either side of t_s) ---"
    echo "$MARKS"
fi
