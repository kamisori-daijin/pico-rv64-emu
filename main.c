#include <stdio.h>
#include <pico/stdlib.h>
#include "cpu.h"

__attribute__((section(".rodata"))) 
const uint8_t xv6_kernel_rom[] = {
    #include "kernel.bin.inc" 
};

#define K_BIN_SIZE     30816
#define VIRT_RAM_START 0x80000000ULL

__attribute__((aligned(4096)))
uint8_t virtual_ram[256 * 1024];

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("Starting RV64 Emulator on Hazard3...\n");

    RV64_CPU_t cpu = {0};
    cpu.pc = 0x80000000ULL;

    while (1) {
        cpu.x[0] = 0; // x0は常時ゼロ
        
        uint32_t raw_insn = 0;
        uint64_t offset = cpu.pc - VIRT_RAM_START;
        
        // Fetch
        if (offset < K_BIN_SIZE) {
            raw_insn = *(const uint32_t*)(xv6_kernel_rom + offset);
        } else {
            raw_insn = *(uint32_t*)&virtual_ram[offset];
        }

        // 命令長の判定
        int insn_len = ((raw_insn & 0x3) != 0x3) ? 2 : 4;
        
        // デバッグ表示
        printf("PC: 0x%016llx, INSN: 0x%08x\n", cpu.pc, (insn_len == 2) ? (raw_insn & 0xFFFF) : raw_insn);

        // 分割した関数へ丸投げ（Decode & Execute）
        if (insn_len == 4) {
            execute_instruction_32(&cpu, raw_insn);
        } else {
            execute_instruction_16(&cpu, raw_insn & 0xFFFF);
        }

        cpu.pc += insn_len;
        sleep_ms(100);
    }
}
