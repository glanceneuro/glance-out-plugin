#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for laserhat-brokerd, for testing the plugin without a Pi.

Speaks enough LHTR to exercise the client: decodes requests, answers PING with
PONG and TRIGGER with FIRED, and remembers sequence numbers so a repeat comes
back DUPLICATE -- the same behaviour that makes a client with a stuck seq fail
to fire.

Decodes with the real LaserDriver/Pi/udp_trigger.py when it can be found, so a
disagreement between the plugin and the reference shows up as a decode failure
here rather than as a laser that quietly does nothing.

    fake_broker.py [--laserdriver DIR] [--port N]

Prints "READY <port>" on stdout once bound, then one line per datagram.
"""

import argparse
import importlib.util
import socket
import struct
import sys
import time

MAGIC = 0x5254484C
VERSION = 1
TRIGGER, PING, ACK = 0x01, 0x02, 0x81
FIRED, DUPLICATE, PONG, BAD_VERSION, BAD_TYPE = 0, 1, 2, 3, 4

_REQ = struct.Struct("<IBBHIQ")
_REP = struct.Struct("<IBBBBIQQQ")


def load_reference(laserdriver):
    """Import the real udp_trigger.py, or return None if it isn't there."""
    if not laserdriver:
        return None
    path = f"{laserdriver}/Pi/udp_trigger.py"
    spec = importlib.util.spec_from_file_location("udp_trigger", path)
    if spec is None:
        return None
    mod = importlib.util.module_from_spec(spec)
    # Register before exec: @dataclass looks its own module up in sys.modules.
    sys.modules["udp_trigger"] = mod
    try:
        spec.loader.exec_module(mod)
    except (OSError, ImportError):
        return None
    return mod


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--laserdriver", default=None)
    ap.add_argument("--port", type=int, default=0)  # 0 = let the OS pick
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    ref = load_reference(args.laserdriver)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", args.port))
    port = sock.getsockname()[1]
    print(f"READY {port}", flush=True)
    if ref is not None:
        print("  decoding with the reference udp_trigger.py", flush=True)

    seen = set()
    sock.settimeout(args.timeout)

    while True:
        try:
            data, peer = sock.recvfrom(256)
        except socket.timeout:
            print("timeout, exiting", flush=True)
            return 0

        rx_ns = time.monotonic_ns()

        if len(data) < _REQ.size:
            print(f"  short datagram ({len(data)} bytes), ignored", flush=True)
            continue

        magic, ver, mtype, _flags, seq, client_ts = _REQ.unpack_from(data)

        # Cross-check our own decode against the reference, when we have it.
        if ref is not None and mtype in (TRIGGER, PING):
            expect = ref.pack_request(mtype, seq, client_ts)
            if expect != data[: _REQ.size]:
                print(f"  DECODE MISMATCH got={data[:_REQ.size].hex()} "
                      f"reference={expect.hex()}", flush=True)

        if magic != MAGIC:
            print(f"  foreign magic {magic:#x}, ignored", flush=True)
            continue

        if ver != VERSION:
            status = BAD_VERSION
        elif mtype == PING:
            status = PONG
        elif mtype == TRIGGER:
            status = DUPLICATE if seq in seen else FIRED
            seen.add(seq)
        else:
            status = BAD_TYPE

        # F_GPIO_SIM: there is no real GPIO here, and the client should say so.
        edge_ns = time.monotonic_ns() if status == FIRED else rx_ns
        reply = _REP.pack(MAGIC, VERSION, ACK, status, 0x04,
                          seq, client_ts, rx_ns, edge_ns)
        sock.sendto(reply, peer)

        name = {TRIGGER: "TRIGGER", PING: "PING"}.get(mtype, f"type {mtype}")
        label = {FIRED: "FIRED", DUPLICATE: "DUPLICATE", PONG: "PONG",
                 BAD_VERSION: "BAD_VERSION", BAD_TYPE: "BAD_TYPE"}[status]
        print(f"  {name} seq={seq} -> {label}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
