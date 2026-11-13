#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-implementation check for the LaserHAT trigger wire format.
#
# Compiles our encoder, runs it to emit packets for a set of vectors, then asks
# LaserDriver/Pi/udp_trigger.py -- the reference the broker is written against
# -- to encode the SAME vectors, and diffs the bytes. A disagreement about a
# field offset does not crash anything; it just means the laser never fires, so
# it has to be caught here rather than at the bench.
#
#   test/run_protocol_test.sh [path/to/LaserDriver]
#
# Defaults to ../LaserDriver next to this repo. Needs only a C++ compiler and
# python3 -- no JUCE, no Open Ephys, no Raspberry Pi.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

LASERDRIVER="${1:-../LaserDriver}"
REF="$LASERDRIVER/Pi/udp_trigger.py"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "building the C++ encoder ..."
c++ -std=c++17 -O1 -Wall -Wextra -Werror test/test_protocol.cpp -o "$WORK/test_protocol"
"$WORK/test_protocol" | tee "$WORK/cpp.txt"

if [ ! -f "$REF" ]; then
    echo
    echo "SKIP: reference implementation not found at '$REF'"
    echo "      self-consistency checks passed; cross-check skipped."
    echo "      pass its path: test/run_protocol_test.sh /path/to/LaserDriver"
    exit 2
fi

echo
echo "cross-checking against $REF ..."
python3 - "$REF" "$WORK/cpp.txt" <<'PY'
import importlib.util, pathlib, sys

ref_path, cpp_path = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location("udp_trigger", ref_path)
ref = importlib.util.module_from_spec(spec)
# Register before exec: @dataclass resolves its own module out of sys.modules,
# and raises AttributeError on None if the module was never put there.
sys.modules["udp_trigger"] = ref
spec.loader.exec_module(ref)

lines = pathlib.Path(cpp_path).read_text().splitlines()
start, end = lines.index("CPP_VECTORS"), lines.index("END")

bad = 0
for ln in lines[start + 1:end]:
    mtype, seq, ts, got = ln.split()
    want = ref.pack_request(int(mtype), int(seq), int(ts)).hex()
    if got != want:
        bad += 1
        print(f"  MISMATCH type={mtype} seq={seq} ts={ts}")
        print(f"    c++   {got}")
        print(f"    ref   {want}")
    else:
        print(f"  ok  type={mtype:>3} seq={seq:<10} -> {got}")

# the constants themselves, not just the bytes they produce
for name, ours, theirs in (("MAGIC", 0x5254484C, ref.MAGIC),
                           ("VERSION", 1, ref.VERSION),
                           ("TRIGGER", 0x01, ref.TRIGGER),
                           ("PING", 0x02, ref.PING),
                           ("ACK", 0x81, ref.ACK),
                           ("PORT", 17017, ref.DEFAULT_PORT)):
    if ours != theirs:
        bad += 1
        print(f"  MISMATCH {name}: ours={ours:#x} reference={theirs:#x}")

if bad:
    print(f"TB_FAIL  Errors: {bad}")
    sys.exit(1)
print("TB_PASS  Errors: 0  (wire format agrees with the reference)")
PY
