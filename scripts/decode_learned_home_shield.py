#!/usr/bin/env python3
"""
Decode a learned Home Shield frame (pulse array from GET /learned-home-shield)
into hub symbols and print the 10 command symbols for HUB_HOME_SHIELD.

Use without flashing: GET the frame from the device, then run this script.

  curl -s http://<device-ip>/learned-home-shield > frame.json
  python3 scripts/decode_learned_home_shield.py frame.json

Or pipe JSON directly:

  curl -s http://<device-ip>/learned-home-shield | python3 scripts/decode_learned_home_shield.py

Logic matches harbor_breeze.cpp: hubPairToSymbol (HUB_TOL ±150 µs, REST 8–12 ms),
and the decoder's start-offset handling (skip first pulse if > 5000 µs, try start 0,1,2).
"""

import json
import sys
from typing import Optional

# Match harbor_breeze.cpp
HUB_TOL = 150
HUB_REST_MIN = 8000
HUB_REST_MAX = 12000

def near(v: int, target: int, tol: int) -> bool:
    return (target - tol) <= v <= (target + tol)

def hub_pair_to_symbol(on_us: int, off_us: int) -> Optional[str]:
    if near(on_us, 400, HUB_TOL) and near(off_us, 500, HUB_TOL):
        return "SS"
    if near(on_us, 400, HUB_TOL) and near(off_us, 950, HUB_TOL):
        return "SL"
    if near(on_us, 850, HUB_TOL) and near(off_us, 950, HUB_TOL):
        return "LL"
    if near(on_us, 850, HUB_TOL) and near(off_us, 500, HUB_TOL):
        return "LS"
    if near(on_us, 400, HUB_TOL) and HUB_REST_MIN <= off_us <= HUB_REST_MAX:
        return "SR"
    return None

def decode_pulses(pulses: list) -> Optional[list]:
    """Decode pulse array to 25 symbols. Tries start offsets 0, 1, 2; skips first pulse if > 5000 µs."""
    n = len(pulses)
    if n < 50:
        return None
    start_min = 1 if (pulses[0] > 5000) else 0
    for start in range(start_min, min(3, n - 49)):
        syms = []
        for i in range(25):
            on_u = pulses[start + i * 2]
            off_u = pulses[start + i * 2 + 1]
            s = hub_pair_to_symbol(on_u, off_u)
            if s is None:
                break
            syms.append(s)
        if len(syms) == 25:
            return syms
    return None

def main() -> None:
    if len(sys.argv) > 1:
        with open(sys.argv[1]) as f:
            data = json.load(f)
    else:
        data = json.load(sys.stdin)

    frame = data.get("frame")
    if not frame:
        learned = data.get("learned", True)
        if not learned:
            print("No Home Shield frame in JSON (learned: false).", file=sys.stderr)
        else:
            print("JSON has no 'frame' array.", file=sys.stderr)
        sys.exit(1)

    if len(frame) < 50:
        print(f"Frame has {len(frame)} pulses; hub decode needs 50.", file=sys.stderr)
        sys.exit(1)

    syms = decode_pulses(frame)
    if not syms:
        print("Frame did not decode as hub protocol (expected 400/500/850/950 µs pairs).", file=sys.stderr)
        sys.exit(1)

    full = ", ".join(syms)
    cmd = syms[15:25]

    print("Full 25 symbols:", full)
    print()
    print("Command symbols (last 10) for HUB_HOME_SHIELD:")
    print("  ", cmd)
    print()
    print("C array line for harbor_breeze.cpp:")
    print('  static const char* HUB_HOME_SHIELD[] = { "' + '","'.join(cmd) + '" };')

if __name__ == "__main__":
    main()
