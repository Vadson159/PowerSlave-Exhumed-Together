#!/usr/bin/env bash
# Full live regression of the sync rework on one PC (test copy only): bash regress_all.sh <root> <out-dir>
# Every smoke runs, then log_check.py on the journals it produced. Summary lines go to <out-dir>/summary.txt.
set -u
ROOT="$1"; OUT="$2"; T="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"
run() { # name, env..., script, args...
  local name="$1"; shift
  echo "=== $name $(date +%H:%M:%S)" >> "$OUT/summary.txt"
  env "$@" > "$OUT/$name.txt" 2>&1
  python -X utf8 "$T/log_check.py" "$ROOT" > "$OUT/${name}_logs.txt" 2>&1
  grep -E "^(PASS|FAIL|RESULT)|True|False|problems|joined|order|rebuilt|sounds|mover" "$OUT/$name.txt" | grep -v "^  " | cut -c1-200 >> "$OUT/summary.txt"
  grep -E "PROBLEM|in play: [1-9]" "$OUT/${name}_logs.txt" >> "$OUT/summary.txt"
}
P="python -u -X utf8"
run m9      X=1 $P "$T/smoke_m9.py" "$ROOT"
run m7      X=1 $P "$T/smoke_m7.py" "$ROOT"
run s1      X=1 $P "$T/smoke_s1.py" "$ROOT"
run s2      X=1 $P "$T/smoke_s2.py" "$ROOT"
run s2tomb  SMOKE_S2_TOMB=1 $P "$T/smoke_s2.py" "$ROOT"
run s3      X=1 $P "$T/smoke_s3.py" "$ROOT"
run s4      X=1 $P "$T/smoke_s4.py" "$ROOT"
run n3      SMOKE_N_CAMPAIGN=1 $P "$T/smoke_n.py" "$ROOT" 3
run n2fire  SMOKE_N_FIRE=1 $P "$T/smoke_n.py" "$ROOT" 2
run campaign SMOKE_RESTART=1 SMOKE_CAMPAIGN=1 $P "$T/smoke.py" "$ROOT"
run scene   SMOKE_SCENE=1 $P "$T/smoke.py" "$ROOT"
run input   SMOKE_INPUT=1 PSE_DEBUG_TELEPORT_CLIENT=0,-1250,3.14159 $P "$T/smoke.py" "$ROOT"
echo "=== done $(date +%H:%M:%S)" >> "$OUT/summary.txt"
