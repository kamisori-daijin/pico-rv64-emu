#include "multicore_sub.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "cpu/cpu.h"
#include "cpu/decode_c.h"
#include "memory.h"
#include <stdio.h>

extern RV64CPU *g_cpu;

void check_pending_interrupts(RV64CPU *cpu) {
    uint32_t pending = cpu->mip.lo & cpu->sie.lo;
    if (!pending) return;

    uint64_t sstatus_val = ((uint64_t)cpu->sstatus.hi << 32) | cpu->sstatus.lo;
    bool sie_enabled = (sstatus_val & (1ULL << 1)) != 0;

    if (cpu->priv_mode == 0 || (cpu->priv_mode == 1 && sie_enabled)) {
        if (pending & (1U << 9)) { // SEIP
            cpu->pc_host_ptr = NULL; cpu->data_host_ptr = NULL; cpu->sepc = cpu->pc;
            cpu->scause.lo = 9U; cpu->scause.hi = 0x80000000U;
            uint64_t spie = (sstatus_val >> 1) & 1;
            sstatus_val &= ~(1ULL << 8);
            if (cpu->priv_mode == 1) sstatus_val |= (1ULL << 8);
            sstatus_val = (sstatus_val & ~(1ULL << 5)) | (spie << 5);
            sstatus_val &= ~(1ULL << 1);
            cpu->sstatus.lo = (uint32_t)sstatus_val; cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);
            cpu->priv_mode = 1; cpu->pc.lo = cpu->stvec.lo & ~0x3U; cpu->pc.hi = cpu->stvec.hi;
            return;
        }

        if (pending & (1U << 5)) { // STIP
            cpu->pc_host_ptr = NULL; cpu->data_host_ptr = NULL; cpu->sepc = cpu->pc;
            cpu->scause.lo = 5U; cpu->scause.hi = 0x80000000U;
            uint64_t spie = (sstatus_val >> 1) & 1;
            sstatus_val &= ~(1ULL << 8);
            if (cpu->priv_mode == 1) sstatus_val |= (1ULL << 8);
            sstatus_val = (sstatus_val & ~(1ULL << 5)) | (spie << 5);
            sstatus_val &= ~(1ULL << 1);
            cpu->sstatus.lo = (uint32_t)sstatus_val; cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);
            cpu->priv_mode = 1; cpu->pc.lo = cpu->stvec.lo & ~0x3U; cpu->pc.hi = cpu->stvec.hi;
            return;
        }
    }
}

void core1_main(void) {
    //Core0 to Core1 interruput
    multicore_lockout_victim_init();

    printf("[Core 1] RV64 CPU Execution Loop Started.\n");

    RV64CPU *cpu = g_cpu;
    unsigned long long inst_count = 0;

    while (__builtin_expect(!cpu->halted, 1)) {
        check_pending_interrupts(cpu);
        cpu->trap_raised = false;
        uint32_t vaddr = cpu->pc.lo;
        uint32_t page_va = vaddr >> 12;
        uint32_t offset = vaddr & 0xFFF;

        if (__builtin_expect(cpu->pc_host_ptr != NULL && cpu->pc_page_va == page_va && offset <= 4092, 1)) {
            uint32_t inst;
            __builtin_memcpy(&inst, cpu->pc_host_ptr + offset, sizeof(inst));
            if ((inst & 0x3) == 0x3) execute_instruction(cpu, inst);
            else execute_compressed_instruction(cpu, (uint16_t)(inst & 0xFFFF));
        } else {
            uint16_t half = mem_fetch16(vaddr);
            if (cpu->halted || cpu->trap_raised) continue;

            if ((half & 0x3) == 0x3) {
                if (vaddr % 2 != 0) {
                    printf("[ERROR] Misaligned instruction fetch! PC = 0x%08X\n", vaddr);
                    cpu->halted = true; break;
                }
                uint16_t half_high = mem_fetch16(vaddr + 2);
                if (cpu->halted || cpu->trap_raised) continue;
                uint32_t inst = (uint32_t)half | ((uint32_t)half_high << 16);
                execute_instruction(cpu, inst);
            } else {
                execute_compressed_instruction(cpu, half);
            }
        }
        inst_count++;
    }

    printf("[Core 1] CPU halted after executing %llu instructions.\n", inst_count);
    while (1) { tight_loop_contents(); }
}