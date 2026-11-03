#!/usr/bin/env python3
"""Find AArch64 PC-relative references to selected strings in libUnreal.so."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM
from capstone import Cs


DEFAULT_NAMES = (
    "CachedProjectileShootDir",
    "FortProjectileTrajectory",
    "SetupProjectileTrajectory",
    "CalculateProjectileTrajectory",
    "WeaponFireEventIndex",
    "ShootCameraPoint",
    "FortTraceWeaponComponent",
    "FortMultiTraceWeaponComponent",
)


def read_sections(path: Path) -> tuple[bytes, int, int, bytes, int]:
    data = path.read_bytes()
    # ELF64 little endian section table fields.
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit("expected ELF64 little-endian input")
    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    shstr_off, shstr_size = 0, 0
    sections: dict[str, tuple[int, int, int]] = {}
    for index in range(e_shnum):
        off = e_shoff + index * e_shentsize
        name, sh_type, _flags, sh_addr, sh_offset, sh_size = struct.unpack_from("<IIQQQQ", data, off)
        if index == e_shstrndx:
            shstr_off, shstr_size = sh_offset, sh_size
        sections[index] = (name, sh_addr, sh_offset, sh_size)
    names = data[shstr_off:shstr_off + shstr_size]
    by_name: dict[str, tuple[int, int, int]] = {}
    for name_off, addr, offset, size in sections.values():
        end = names.find(b"\0", name_off)
        name = names[name_off:end].decode("ascii", "replace")
        by_name[name] = (addr, offset, size)
    ro_addr, ro_off, ro_size = by_name[".rodata"]
    text_addr, text_off, text_size = by_name[".text"]
    return data, ro_addr, ro_off, data[ro_off:ro_off + ro_size], text_addr, text_off, text_size


def find_strings(rodata: bytes, ro_addr: int, names: tuple[str, ...]) -> dict[int, str]:
    targets: dict[int, str] = {}
    for name in names:
        needle = name.encode("ascii") + b"\0"
        start = 0
        while True:
            pos = rodata.find(needle, start)
            if pos < 0:
                break
            targets[ro_addr + pos] = name
            start = pos + 1
    return targets


def adrp_target(insn) -> int | None:
    # Encoding: op=1 at bit 31, fixed 10000 at bits 28:24.
    word = insn.bytes[0] | (insn.bytes[1] << 8) | (insn.bytes[2] << 16) | (insn.bytes[3] << 24)
    if (word & 0x9F000000) != 0x90000000:
        return None
    immhi = (word >> 5) & 0x7FFFF
    immlo = (word >> 29) & 0x3
    imm = ((immhi << 2) | immlo)
    if imm & (1 << 20):
        imm -= 1 << 21
    return ((insn.address & ~0xFFF) + (imm << 12))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("so", type=Path)
    parser.add_argument("--name", action="append", dest="names")
    parser.add_argument("--window", type=int, default=8)
    args = parser.parse_args()
    data, ro_addr, ro_off, rodata, text_addr, text_off, text_size = read_sections(args.so)
    names = tuple(args.names or DEFAULT_NAMES)
    targets = find_strings(rodata, ro_addr, names)
    print(f"rodata={ro_addr:#x}+{len(rodata):#x} text={text_addr:#x}+{text_size:#x}")
    for address, name in sorted(targets.items()):
        print(f"string {name} va={address:#x} file={ro_off + address - ro_addr:#x}")
    target_pages = {address & ~0xFFF for address in targets}
    cs = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    cs.detail = False
    text = data[text_off:text_off + text_size]
    hits = 0
    # Disassemble in chunks; keep instruction addresses at ELF VAs.
    for insn in cs.disasm(text, text_addr):
        target = adrp_target(insn)
        if target not in target_pages:
            continue
        for address, name in targets.items():
            if (address & ~0xFFF) != target:
                continue
            print(f"xref candidate {insn.address:#x} -> {name} ({address:#x}) {insn.mnemonic} {insn.op_str}")
            hits += 1
    print(f"xref_candidates={hits}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
