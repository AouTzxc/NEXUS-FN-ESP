#!/usr/bin/env python3
"""Inspect direct AArch64 callers of a known libUnreal routine.

This is an offline helper.  It maps the ELF .text segment, decodes direct BL
instructions without changing the binary, and prints bounded Capstone context
for each caller.  The output is intended for locating the first consumer of a
shot-direction cache rather than treating cache storage as the shot request.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs


def sections(data: bytes) -> tuple[int, int, int]:
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected 64-bit little-endian ELF")
    table = struct.unpack_from("<Q", data, 0x28)[0]
    entry_size, count, strings_index = struct.unpack_from("<HHH", data, 0x3A)
    rows = []
    for index in range(count):
        row = table + index * entry_size
        name, _kind, _flags, address, offset, size = struct.unpack_from("<IIQQQQ", data, row)
        rows.append((name, address, offset, size))
    _name, _address, string_offset, string_size = rows[strings_index]
    names = data[string_offset:string_offset + string_size]
    for name_offset, address, offset, size in rows:
        end = names.find(b"\0", name_offset)
        if names[name_offset:end] == b".text":
            return address, offset, size
    raise ValueError("missing .text")


def sign_extend(value: int, width: int) -> int:
    return value - (1 << width) if value & (1 << (width - 1)) else value


def direct_call_target(word: int, address: int) -> int | None:
    if word & 0xFC000000 != 0x94000000:
        return None
    return address + (sign_extend(word & 0x03FFFFFF, 26) << 2)


def function_start(text: bytes, text_va: int, target: int, scan_back: int) -> int:
    offset = target - text_va
    if offset < 0 or offset >= len(text):
        raise ValueError("target is outside .text")
    # ARM64 UE functions normally begin with a frame save or PAC instruction.
    # Return the closest plausible start; the caller output still includes a
    # prelude window when a function uses a nonstandard leaf prologue.
    floor = max(0, offset - scan_back)
    starts = []
    for index in range(floor, offset, 4):
        word = struct.unpack_from("<I", text, index)[0]
        if word in (0xA9BF7BFD, 0xD503233F, 0xD503237F):
            starts.append(text_va + index)
    return starts[-1] if starts else target


def context(cs: Cs, text: bytes, text_va: int, center: int, before: int, after: int) -> list[str]:
    start = max(text_va, center - before)
    start -= (start - text_va) % 4
    end = min(text_va + len(text), center + after)
    block = text[start - text_va:end - text_va]
    return [
        f"{insn.address:#010x}: {insn.mnemonic:<8} {insn.op_str}"
        for insn in cs.disasm(block, start)
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("so", type=Path)
    parser.add_argument("--target", required=True, type=lambda value: int(value, 0))
    parser.add_argument(
        "--exact-entry",
        action="store_true",
        help="treat --target as a known function entry instead of searching for a preceding prologue",
    )
    parser.add_argument("--entry-scan-back", type=lambda value: int(value, 0), default=0x2000)
    parser.add_argument("--context-before", type=lambda value: int(value, 0), default=0x80)
    parser.add_argument("--context-after", type=lambda value: int(value, 0), default=0x120)
    args = parser.parse_args()

    data = args.so.read_bytes()
    text_va, text_offset, text_size = sections(data)
    text = data[text_offset:text_offset + text_size]
    entry = args.target if args.exact_entry else function_start(text, text_va, args.target, args.entry_scan_back)
    print(f"text={text_va:#x}+{text_size:#x} target={args.target:#x} inferred_entry={entry:#x}")

    cs = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    print("TARGET_CONTEXT")
    print("\n".join(context(cs, text, text_va, entry, 0, 0x500)))

    callers = []
    for offset in range(0, len(text), 4):
        word = struct.unpack_from("<I", text, offset)[0]
        address = text_va + offset
        if direct_call_target(word, address) == entry:
            callers.append(address)
    print(f"DIRECT_CALLERS count={len(callers)}")
    for caller in callers:
        print(f"CALLER {caller:#x}")
        print("\n".join(context(cs, text, text_va, caller, args.context_before, args.context_after)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
