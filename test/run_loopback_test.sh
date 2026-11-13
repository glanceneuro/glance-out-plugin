#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drives the real LhtrClient against test/fake_broker.py on localhost.
#
# Unlike run_protocol_test.sh, this one needs JUCE, because LhtrClient uses
# juce::Thread for the ACK listener. It compiles juce_core only -- not the GUI
# -- which takes about ten seconds and needs no X11, no sound card and no
# Open Ephys build.
#
#   test/run_loopback_test.sh [path/to/plugin-GUI] [path/to/LaserDriver]
#
# Defaults match a checkout beside plugin-GUI. If plugin-GUI is not found the
# script exits 2 with SKIP rather than failing: run_protocol_test.sh is the one
# that must always pass.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

GUI_BASE_DIR="${1:-${GUI_BASE_DIR:-../../plugin-GUI}}"
LASERDRIVER="${2:-../../LaserDriver}"

if [ ! -f "$GUI_BASE_DIR/JuceLibraryCode/JuceHeader.h" ]; then
    echo "SKIP: no plugin-GUI at '$GUI_BASE_DIR'"
    echo "      pass its path: test/run_loopback_test.sh /path/to/plugin-GUI"
    exit 2
fi

WORK=$(mktemp -d)
BROKER_PID=""
cleanup() {
    [ -n "$BROKER_PID" ] && kill "$BROKER_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

JUCE_FLAGS=(
    -std=c++17 -O1 -fPIC
    -DOEPLUGIN -DNDEBUG=1 -DJUCE_USE_CURL=0 -DJUCE_DISABLE_NATIVE_FILECHOOSERS=1
    # -isystem, not -I: JUCE and the Open Ephys headers are warning-noisy, and
    # -Wall -Wextra below is meant to be about OUR code.
    -isystem "$GUI_BASE_DIR/JuceLibraryCode"
    -isystem "$GUI_BASE_DIR/JuceLibraryCode/modules"
    -isystem "$GUI_BASE_DIR/Plugins/Headers"
)

# On macOS juce_core must come from the Objective-C++ translation unit: the
# .cpp pulls in native/juce_mac_*.mm, which a plain C++ compile cannot parse.
# The frameworks are the ones juce_core.h declares under OSXFrameworks.
case "$(uname -s)" in
    Darwin)
        JUCE_TU="$GUI_BASE_DIR/JuceLibraryCode/include_juce_core.mm"
        JUCE_TU_FLAGS=(-x objective-c++)
        LINK_LIBS=(-framework Cocoa -framework Foundation -framework IOKit
                   -framework Security -lpthread)
        ;;
    *)
        JUCE_TU="$GUI_BASE_DIR/JuceLibraryCode/include_juce_core.cpp"
        JUCE_TU_FLAGS=()
        LINK_LIBS=(-lpthread -ldl -lrt)
        ;;
esac

# juce_core is the only JUCE module LhtrClient reaches into (Thread, String).
# Cached between runs because it is the slow part.
JUCE_OBJ="${JUCE_CORE_OBJ:-$WORK/juce_core.o}"
if [ ! -f "$JUCE_OBJ" ]; then
    echo "compiling juce_core (about ten seconds) ..."
    c++ "${JUCE_FLAGS[@]}" "${JUCE_TU_FLAGS[@]}" -c "$JUCE_TU" -o "$JUCE_OBJ"
fi

echo "building the loopback test ..."
c++ "${JUCE_FLAGS[@]}" -Wall -Wextra -c Source/LhtrClient.cpp -o "$WORK/LhtrClient.o"
c++ "${JUCE_FLAGS[@]}" -Wall -Wextra -c test/test_loopback.cpp -o "$WORK/test_loopback.o"
c++ "$WORK/test_loopback.o" "$WORK/LhtrClient.o" "$JUCE_OBJ" \
    -o "$WORK/test_loopback" "${LINK_LIBS[@]}"

echo "starting the stand-in broker ..."
REF_ARG=()
[ -f "$LASERDRIVER/Pi/udp_trigger.py" ] && REF_ARG=(--laserdriver "$LASERDRIVER")

python3 test/fake_broker.py "${REF_ARG[@]}" --timeout 30 > "$WORK/broker.log" 2>&1 &
BROKER_PID=$!

# Wait for the bind, and pick up the port the OS handed it.
PORT=""
for _ in $(seq 1 100); do
    if grep -q '^READY ' "$WORK/broker.log" 2>/dev/null; then
        PORT=$(awk '/^READY /{print $2; exit}' "$WORK/broker.log")
        break
    fi
    kill -0 "$BROKER_PID" 2>/dev/null || break
    sleep 0.05
done

if [ -z "$PORT" ]; then
    echo "the stand-in broker never bound:"
    cat "$WORK/broker.log"
    exit 1
fi
echo "  listening on 127.0.0.1:$PORT"
echo

set +e
"$WORK/test_loopback" "$PORT"
RESULT=$?
set -e

echo
echo "what the broker saw:"
sed 's/^/  | /' "$WORK/broker.log"

if grep -q "DECODE MISMATCH" "$WORK/broker.log"; then
    echo "TB_FAIL  the broker could not decode what we sent"
    exit 1
fi

exit $RESULT
