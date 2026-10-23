#include <stdio.h>
#include "cpu.h"

void execute_instruction_32(RV64_CPU_t *cpu, uint32_t insn) {
    uint8_t opcode = insn & 0x7F;
    uint8_t rd     = (insn >> 7) & 0x1F;
    uint8_t funct3 = (insn >> 12) & 0x7;
    uint8_t rs1    = (insn >> 15) & 0x1F;
    uint8_t rs2    = (insn >> 20) & 0x1F;
    uint8_t funct7 = (insn >> 25) & 0x7F;

    switch (opcode) {
        case 0x17: { // AUIPC
            int64_t imm = (int32_t)(insn & 0xFFFFF000); 
            cpu->x[rd] = cpu->pc + imm;
            break;
        }
        case 0x13: { // OP-IMM (ADDIなど)
            if (funct3 == 0x0) { // ADDI
                int64_t imm = (int32_t)insn >> 20; 
                cpu->x[rd] = cpu->x[rs1] + imm;
            }
            break;
        }
        // ★ここに今後の32bit命令（mulなど）を足していく
        default:
            printf("未対応の32bit命令: 0x%08x (PC: 0x%016llx)\n", insn, cpu->pc);
            while(1);
    }
}
