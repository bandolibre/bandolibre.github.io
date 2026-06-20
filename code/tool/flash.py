#!/usr/bin/env python3
"""Flash firmware to every connected ST-Link whose chip is registered as the target board type.

Example:
  flash.py wing-g474 build/Debug/wing-g474.bin   # flashes all connected wing boards
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from registry import BOARDS, UID_ADDR, UID_LEN, load_registry, read_uid


def probe_serials() -> list[str]:
    result = subprocess.run(["st-info", "--probe"], capture_output=True, text=True)
    if result.returncode != 0:
        print("Error: st-info --probe failed.", file=sys.stderr)
        sys.exit(1)
    return [
        line.split(":", 1)[1].strip()
        for line in result.stdout.splitlines()
        if line.strip().startswith("serial:")
    ]


def flash_binary(serial: str, binary: Path) -> None:
    result = subprocess.run(
        ["st-flash", "--serial", serial, "--reset", "write", str(binary), "0x08000000"]
    )
    if result.returncode != 0:
        print(f"Error: flash failed for ST-Link {serial}.", file=sys.stderr)
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=BOARDS, help="Board type to flash")
    parser.add_argument("binary", type=Path, help="Binary (.bin) file to flash")
    args = parser.parse_args()

    serials = probe_serials()
    if not serials:
        print("Error: no ST-Link found. Connect at least one board.", file=sys.stderr)
        sys.exit(1)

    registry = load_registry()
    targets: list[tuple[str, str]] = []

    for serial in serials:
        uid = read_uid(serial)
        if uid not in registry:
            print(
                f"Error: chip {uid} (ST-Link {serial}) is not registered. "
                "Run 'registry.py add' first.",
                file=sys.stderr,
            )
            sys.exit(1)
        if registry[uid] == args.board:
            targets.append((serial, uid))

    if not targets:
        print(f"Error: no connected board is registered as '{args.board}'.", file=sys.stderr)
        sys.exit(1)

    for serial, uid in targets:
        print(f"Flashing {args.board} (chip {uid}) via ST-Link {serial}…")
        flash_binary(serial, args.binary)
        print("  Done.")


if __name__ == "__main__":
    main()
