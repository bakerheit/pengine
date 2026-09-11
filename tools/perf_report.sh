#!/usr/bin/env bash
#
# Read back a session's frame log.
#
#   tools/perf_report.sh                  # the newest session in build/perf
#   tools/perf_report.sh build/perf/session-004.csv
#   tools/perf_report.sh --worst 40       # show more of the slow frames
#
# The CSV already carries its own summary in the `#` trailer; this adds the
# two views that a trailer cannot hold: a second-by-second timeline of where
# the frame rate went, and the list of individual dips with the counters that
# were high when they happened. Both are what you want BEFORE opening the raw
# rows, and most of the time they are the whole investigation.
#
# Columns, for anyone slicing it further with awk:
#   1 frame    6 mark      11 mode        16 mesh_ms    25 chunks_built
#   2 t_s      7 x         12 speed_mph   17 light_ms   30 cars
#   3 ms       8 y         13 sim_steps   18 fill_ms    32 npcs
#   4 fps      9 z         14 clamped     19 draw_calls 34 lights
#   5 spike   10 place     15 cull_ms     21 visible    37 weather
set -euo pipefail

cd "$(dirname "$0")/.."

WORST=20
FILE=""
while [ $# -gt 0 ]; do
    case "$1" in
        --worst) WORST="$2"; shift 2 ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) FILE="$1"; shift ;;
    esac
done

if [ -z "$FILE" ]; then
    # Newest by mtime, not by name: past session-999 the numbering stops
    # sorting the way it reads.
    FILE="$(ls -1t build/perf/session-*.csv 2>/dev/null | head -1 || true)"
fi
if [ -z "$FILE" ] || [ ! -f "$FILE" ]; then
    echo "no frame log found. Play a session (it records by default), or pass a path." >&2
    echo "sessions live in build/perf/ unless --perf-log said otherwise." >&2
    exit 1
fi

echo "=== $FILE ==="
echo

# The trailer first: it is the session in ten lines, and it was written by the
# recorder itself rather than re-derived here.
if grep -q '^# ---- session summary' "$FILE"; then
    sed -n '/^# ---- session summary/,$p' "$FILE" | sed 's/^# \{0,1\}//'
else
    echo "(no trailer — the session did not exit cleanly; rows below are still good)"
fi
echo

echo "--- one second at a time (one # per 4 ms of the worst frame; * marks a dip) ---"
awk -F, '
    NR == 1 || /^#/ { next }
    $3 == "" { next }
    {
        sec = int($2);
        n[sec]++;
        total[sec] += $3;
        if ($3 > worst[sec]) { worst[sec] = $3; }
        slow[sec] += ($5 == 1 ? 1 : 0);
        if ($10 != "") { place[sec] = $10; }
        mode[sec] = $11;
    }
    END {
        for (s = 0; s <= 100000; s++) {
            if (!(s in n)) continue;
            mean = total[s] / n[s];
            bar = "";
            # One block per 4 ms of the WORST frame in the second. The mean
            # hides exactly what this report is for.
            ticks = int(worst[s] / 4);
            if (ticks > 30) ticks = 30;
            for (i = 0; i < ticks; i++) bar = bar "#";
            printf "%6ds %6.1f fps  mean %5.1f  worst %6.1f ms %s%-32s %-18s %s\n",
                   s, (mean > 0 ? 1000 / mean : 0), mean, worst[s],
                   (slow[s] > 0 ? "*" : " "), bar, place[s], mode[s];
        }
    }
' "$FILE"
echo

echo "--- the $WORST slowest frames, and what was busy ---"
awk -F, '
    NR == 1 || /^#/ { next }
    $3 == "" { next }
    { print }
' "$FILE" | sort -t, -k3 -g -r | head -n "$WORST" | awk -F, '
    BEGIN {
        printf "%9s %8s  %-18s %-7s %7s %7s %7s %7s %7s %7s %6s %5s %5s\n",
               "ms", "t_s", "place", "mode", "cull", "mesh", "light", "fill",
               "draws", "built", "cars", "npcs", "steps";
    }
    {
        printf "%9.2f %8.1f  %-18.18s %-7s %7.2f %7.2f %7.2f %7.1f %7d %7d %6d %5d %5d\n",
               $3, $2, $10, $11, $15, $16, $17, $18, $19, $25, $30, $32, $13;
    }
'
echo

MARKS="$(awk -F, 'NR > 1 && $0 !~ /^#/ && $6 != "" { print }' "$FILE" || true)"
if [ -n "$MARKS" ]; then
    echo "--- moments you marked with F4 (grep the rows either side of t_s) ---"
    echo "$MARKS" | awk -F, '{ printf "  t=%.1f s  (%.0f, %.0f)  %s  %s\n", $2, $7, $9, $10, $6 }'
fi
