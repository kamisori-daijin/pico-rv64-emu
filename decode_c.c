#include <stdio.h>
#include "cpu.h"

void execute_instruction_16(RV64_CPU_t *cpu, uint16_t insn) {
    uint8_t op = insn & 0x3;
    uint8_t funct3 = (insn >> 13) & 0x7;

    if (op == 0x1 && funct3 == 0x3) { // C.LUI
        uint8_t rd = (insn >> 7) & 0x1F;
        int64_t imm = ((insn >> 12) & 0x1) << 17;
        imm |= ((insn >> 2) & 0x1F) << 12;
        if (imm & 0x20000) imm |= 0xFFFFFFFFFFFC0000ULL;
        cpu->x[rd] = imm;
    } else {
        printf("未対応の16bit圧縮命令: 0x%04x (PC: 0x%016llx)\n", insn, cpu->pc);
        while(1);
    }
}
