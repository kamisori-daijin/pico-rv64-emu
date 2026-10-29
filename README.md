# xv6-riscv64 on RP2350

This is a lightweight 64-bit RISC-V software emulator and bare-metal port designed to run **xv6-riscv** within the limited SRAM capacity of the RP2350 (Raspberry Pi Pico 2).

It emulates virtual memory based on the Sv39 MMU, a 16550 UART, CLINT, PLIC, and a VirtIO block device (for disk I/O) with a sector cache. This enables the full execution of user-mode processes, including a shell (`sh`) and various user utilities (such as `ls` and `echo`). ---

## Features

- **RV64 CPU Emulation**
- **ISA Support**: RV64I / RV64C (compressed instructions) / RV64M (multiplication and division instructions)
- **Privilege Modes**: Machine (M), Supervisor (S), and User (U) modes
- **Traps and Exceptions**: Full implementation of exception trap handling (e.g., `ecall`, `sret`, `mret`, page faults)
- **Sv39 Memory Management Unit (MMU)**
- 3-level page table translation
- Page permission checks (`PTE_R`, `PTE_W`, `PTE_X`, `PTE_U`)
- Virtual address mapping for kernel stacks, trampolines, and trap frame regions
- **Peripheral Emulation**
- **UART (16550)**: Asynchronous console I/O with IRQ 10 support
- **CLINT**: Timer interrupt handling via `mtime` and `mtimecmp`
- **PLIC**: Interrupt controller for managing VirtIO and UART interrupts
- **VirtIO Block Device**: Disk I/O utilizing an in-memory sector cache (128 sectors / 64 KB SRAM)
- **Memory Optimization for RP2350**
- Guest RAM footprint: **416 KB** (fits comfortably within the RP2350's 520 KB SRAM)
- Reduced memory allocation unit in `umalloc.c` from 64 KB to **4 KB (one page)** to prevent memory exhaustion in constrained RAM environments

---

## Project Structure

```text
.
├── CMakeLists.txt         # CMake configuration for Pico SDK
├── main.c                 # Main execution loop, memory access routines, and trap handling
├──
``` cpu.h                  # Definitions for CPU registers and CSRs (Control and Status Registers)
├── mmu.c / mmu.h          # Sv39 MMU page table translation logic
├── decode_i.c             # 32-bit instruction decoder for RV64I / RV64M / System instructions
├── decode_c.c / decode_c.h# 16-bit compressed instruction decoder for RV64C
├── uart.c / uart.h        # 16550 UART device emulation
├── clint.c / clint.h      # CLINT timer emulation
├── plic.c / plic.h        # PLIC interrupt controller emulation
├── virtio.h               # VirtIO block device and sector cache

```

---

## Build and Installation Guide

### Prerequisites

- **Raspberry Pi Pico SDK** must be installed, and the `PICO_SDK_PATH` environment variable must be set.
- `picotool` must be installed.
- **RISC-V Toolchain**: `riscv64-unknown-elf-gcc` (for building the xv6 kernel and user binaries).

### 1. Preparing the xv6 Kernel and File System
```bash
build_xv6.sh
```

### 2. Building the Emulator

After generating `kernel.bin.inc` and `fs_img.inc` and placing them in the project's root directory, execute the following command:

```bash
run_emu.sh
```

The emulator will be automatically flashed onto the device using `picotool`.

---

## 🖥️ Execution Example

Connect to the USB serial console using a baud rate of `115200`. ```text
========================================
RV64 Emulator on RP2350 (RVC + MMU + UART)
========================================
Kernel binary loaded successfully. Size: 31504 bytes
Starting CPU execution loop with MMU support...

xv6 kernel is booting

init: starting sh
$ ls
. 1 1 1024
..             1 1 1024
README         2 2 2226
cat            2 3 24232
echo           2 4 23080
forktest       2 5 13272
grep           2 6 27560
init           2 7 23600
kill           2 8 23000
ln             2 9 22960
ls             2 10 26448
mkdir          2 11 23120
rm             2 12 23104
sh             2 13 41968
stressfs       2 14 24016
usertests      2 15 148408
grind          2 16 38160
wc             2 17 25344
zombie         2 18 22408
console        3 19 0
$ echo Hello from RP2350!
Hello from RP2350!
```

---

## Important Implementation Notes

- **Memory Constraints**: The RP2350 features 520 KB of SRAM. `416 KB` is allocated for guest RAM, leaving sufficient headroom for the RP2350's own execution code, stack, and the VirtIO sector cache.
- `usertests` will not run due to insufficient memory.