#!/usr/bin/env python3
"""Run the integer FFT dogfood harness on an emulated Cortex-M0.

Loads a cortex-m0plus ELF built with tests/fft_target_startup.s +
tests/test_integer_fft.c + lib/phase/int_fft.c into Unicorn (Thumb-only,
nRF51-style map: flash @ 0x0, 16KB RAM @ 0x20000000) and intercepts the
two semihosting marker functions by symbol address:
  semihost_write(const void *buf, int n): r0/r1 = args at entry; the hook
      emits the bytes to stdout, then sets PC = LR (return address) and
      skips the call body.
  semihost_exit(uint32_t reason): r0 = reason at entry; the hook stops
      emulation with that exit code.

Unicorn's Thumb mode raises UC_ERR_EXCEPTION on the BKPT instruction
itself, so the marker functions are no-op (bx lr) and interception happens
at function entry instead. On real hardware the same functions would issue
the ARM semihosting BKPT 0xAB (SYS_WRITE 0x05 / SYS_EXIT 0x18).

Usage: python3 tests/run_fft_target.py /tmp/int_fft_target.elf
Exit: 0 if the harness's own check count reports 0 failures.
"""

import struct
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_LR,
)

FLASH_BASE = 0x00000000
FLASH_SIZE = 256 * 1024
RAM_BASE = 0x20000000
RAM_SIZE = 16 * 1024


def load_elf_segments(path):
    """Return ([(vaddr, memsz, blob)], symtab {name: addr})."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"not an ELF: {path}")
    little = data[5] == 1
    endian = "<" if little else ">"
    e_phoff = struct.unpack_from(endian + "I", data, 28)[0]
    e_phentsize = struct.unpack_from(endian + "H", data, 42)[0]
    e_phnum = struct.unpack_from(endian + "H", data, 44)[0]
    e_shoff = struct.unpack_from(endian + "I", data, 32)[0]
    e_shentsize = struct.unpack_from(endian + "H", data, 46)[0]
    e_shnum = struct.unpack_from(endian + "H", data, 48)[0]
    e_shstrndx = struct.unpack_from(endian + "H", data, 50)[0]

    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset = struct.unpack_from(endian + "II", data, off)
        p_vaddr = struct.unpack_from(endian + "I", data, off + 8)[0]
        p_filesz, p_memsz = struct.unpack_from(endian + "II", data, off + 16)
        if p_type == 1:  # PT_LOAD
            segments.append((p_vaddr, p_memsz, data[p_offset:p_offset + p_filesz]))

    # Section headers: first pass finds .shstrtab (names), second pass
    # locates .symtab and .strtab.
    shstr = None
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        if e_shstrndx == i:
            sh_offset = struct.unpack_from(endian + "I", data, off + 16)[0]
            sh_size = struct.unpack_from(endian + "I", data, off + 20)[0]
            shstr = data[sh_offset:sh_offset + sh_size]
            break
    if shstr is None:
        raise SystemExit("no .shstrtab")

    symtab = None
    strtab = None
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh_name, sh_type = struct.unpack_from(endian + "II", data, off)
        sh_offset = struct.unpack_from(endian + "I", data, off + 16)[0]
        sh_size = struct.unpack_from(endian + "I", data, off + 20)[0]
        name_end = shstr.find(b"\x00", sh_name)
        name = shstr[sh_name:name_end].decode("utf-8", "replace")
        if sh_type == 2 and name == ".symtab":
            symtab = data[sh_offset:sh_offset + sh_size]
        elif sh_type == 3 and name == ".strtab":
            strtab = data[sh_offset:sh_offset + sh_size]

    symbols = {}
    if symtab and strtab:
        for i in range(0, len(symtab), 16):
            st_name, st_value = struct.unpack_from(endian + "II", symtab, i)
            if st_name == 0:
                continue
            name_end = strtab.find(b"\x00", st_name)
            name = strtab[st_name:name_end].decode("utf-8", "replace")
            if st_value:
                symbols[name] = st_value

    return segments, symbols


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: run_fft_target.py <elf>")
    elf_path = sys.argv[1]

    segments, symbols = load_elf_segments(elf_path)
    if not segments:
        raise SystemExit("no PT_LOAD segments")

    write_addr = symbols.get("semihost_write")
    exit_addr = symbols.get("semihost_exit")
    if write_addr is None or exit_addr is None:
        raise SystemExit("missing semihost marker symbols in ELF symtab")
    # ELF symbol values for Thumb code carry the Thumb bit; Unicorn reports
    # even instruction addresses.
    write_addr &= ~1
    exit_addr &= ~1

    mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    mu.mem_map(FLASH_BASE, FLASH_SIZE)
    mu.mem_map(RAM_BASE, RAM_SIZE)

    entry = None
    for vaddr, memsz, blob in segments:
        mu.mem_write(vaddr, blob.ljust(memsz, b"\x00"))
        if vaddr == FLASH_BASE:
            # Entry = vector table slot 1 (Reset). Keep the Thumb bit set —
            # Unicorn's Thumb mode expects the low bit asserted.
            entry = struct.unpack_from("<I", blob, 4)[0]

    output = []
    exit_code = None

    hook_count = [0]
    def hook_code(_mu, address, _size, _user_data):
        nonlocal exit_code
        hook_count[0] += 1
        if address == write_addr:
            buf = _mu.reg_read(UC_ARM_REG_R0)
            n = _mu.reg_read(UC_ARM_REG_R1)
            if n > 0:
                output.append(bytes(_mu.mem_read(buf, n)).decode("utf-8", "replace"))
            print(f"[dbg] write@{address:#x} n={n}", file=sys.stderr)
            _mu.reg_write(UC_ARM_REG_PC, _mu.reg_read(UC_ARM_REG_LR))
            return True  # skip the marker body
        if address == exit_addr:
            exit_code = _mu.reg_read(UC_ARM_REG_R0) & 0xFF
            print(f"[dbg] exit@{address:#x} code={exit_code}", file=sys.stderr)
            # Stop emulation: jump PC past the function.
            _mu.reg_write(UC_ARM_REG_PC, _mu.reg_read(UC_ARM_REG_LR))
            return True
        return None

    mu.hook_add(UC_HOOK_CODE, hook_code)

    mu.reg_write(UC_ARM_REG_SP, RAM_BASE + RAM_SIZE)
    mu.reg_write(UC_ARM_REG_PC, entry)

    try:
        mu.emu_start(entry, 0, timeout=60_000_000, count=0)
    except Exception as exc:  # UcError on invalid instruction etc.
        pc = mu.reg_read(UC_ARM_REG_PC)
        try:
            insn = bytes(mu.mem_read(pc, 4)).hex()
        except Exception:
            insn = "?"
        print("".join(output), end="")
        raise SystemExit(f"emulation error at pc={pc:#x} insn={insn}: {exc}")

    print(f"[dbg] entry={entry:#x} write_addr={write_addr:#x} exit_addr={exit_addr:#x} hooks={hook_count[0]}", file=sys.stderr)
    print("".join(output), end="")
    if exit_code is None:
        raise SystemExit("target did not reach semihost_exit (timeout / hang)")
    print(f"\n[target] SYS_EXIT code={exit_code}")
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
