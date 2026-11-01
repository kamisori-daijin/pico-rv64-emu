#include "decode_c.h"
#include "../mmu/mmu.h"
#include <stdio.h>

extern uint8_t guest_ram[];

extern uint8_t  mem_read8(uint32_t paddr);
extern uint16_t mem_read16(uint32_t paddr);
extern uint32_t mem_read32(uint32_t paddr);
extern reg64_t  mem_read64(uint32_t paddr);

extern void mem_write8(uint32_t paddr, uint8_t val);
extern void mem_write16(uint32_t paddr, uint16_t val);
extern void mem_write32(uint32_t paddr, uint32_t val);
extern void mem_write64(uint32_t paddr, reg64_t val);

static inline uint32_t c_reg(uint32_t r3) { return 8 + r3; }

static inline void set_reg64_u64(RV64CPU *cpu, int regno, uint64_t val) {
    if (regno == 0) return;
    cpu->x[regno].lo = (uint32_t)val;
    cpu->x[regno].hi = (uint32_t)(val >> 32);
}

static inline uint64_t get_reg64(RV64CPU *cpu, int regno) {
    if (regno == 0) return 0;
    return ((uint64_t)cpu->x[regno].hi << 32) | cpu->x[regno].lo;
}

static inline void advance_pc2(RV64CPU *cpu) {
    if (!cpu->trap_raised) {
        cpu->pc.lo += 2;
        cpu->pc.hi = 0;
    }
}

void __not_in_flash_func(execute_compressed_instruction)(RV64CPU *cpu, uint16_t inst) {
    uint32_t key = ((inst & 0x3) << 3) | ((inst >> 13) & 0x7);

    static const void *dispatch_table[32] = {
        // Quadrant 0
        [0x00] = &&op_c_addi4spn, [0x02] = &&op_c_lw, [0x03] = &&op_c_ld,
        [0x06] = &&op_c_sw,       [0x07] = &&op_c_sd,

        // Quadrant 1
        [0x08] = &&op_c_addi,     [0x09] = &&op_c_addiw, [0x0A] = &&op_c_li,
        [0x0B] = &&op_c_lui_sp,   [0x0C] = &&op_c_misc_alu, [0x0D] = &&op_c_j,
        [0x0E] = &&op_c_beqz,     [0x0F] = &&op_c_bnez,

        // Quadrant 2
        [0x10] = &&op_c_slli,     [0x12] = &&op_c_lwsp,  [0x13] = &&op_c_ldsp,
        [0x14] = &&op_c_jalr_mv_add, [0x16] = &&op_c_swsp, [0x17] = &&op_c_sdsp,
    };

    const void *target = dispatch_table[key];
    if (target) goto *target;
    goto op_c_unknown;

op_c_addi4spn: {
    uint32_t nzuimm = (((inst >> 11) & 0x3) << 4) | (((inst >> 7) & 0xF) << 6) | (((inst >> 6) & 0x1) << 2) | (((inst >> 5) & 0x1) << 3);
    uint32_t rd = c_reg((inst >> 2) & 0x7);
    if (nzuimm == 0) { cpu->halted = true; return; }
    set_reg64_u64(cpu, rd, get_reg64(cpu, 2) + nzuimm); advance_pc2(cpu); return;
}
op_c_lw: {
    uint32_t uimm = (((inst >> 10) & 0x7) << 3) | (((inst >> 6) & 0x1) << 2) | (((inst >> 5) & 0x1) << 6);
    set_reg64_u64(cpu, c_reg((inst >> 2) & 0x7), (int64_t)(int32_t)mem_read32((uint32_t)get_reg64(cpu, c_reg((inst >> 7) & 0x7)) + uimm));
    advance_pc2(cpu); return;
}
op_c_ld: {
    uint32_t uimm = (((inst >> 10) & 0x7) << 3) | (((inst >> 5) & 0x3) << 6);
    uint32_t rd = c_reg((inst >> 2) & 0x7);
    if (rd != 0) cpu->x[rd] = mem_read64((uint32_t)get_reg64(cpu, c_reg((inst >> 7) & 0x7)) + uimm);
    advance_pc2(cpu); return;
}
op_c_sw: {
    uint32_t uimm = (((inst >> 10) & 0x7) << 3) | (((inst >> 6) & 0x1) << 2) | (((inst >> 5) & 0x1) << 6);
    mem_write32((uint32_t)get_reg64(cpu, c_reg((inst >> 7) & 0x7)) + uimm, cpu->x[c_reg((inst >> 2) & 0x7)].lo);
    advance_pc2(cpu); return;
}
op_c_sd: {
    uint32_t uimm = (((inst >> 10) & 0x7) << 3) | (((inst >> 5) & 0x3) << 6);
    mem_write64((uint32_t)get_reg64(cpu, c_reg((inst >> 7) & 0x7)) + uimm, cpu->x[c_reg((inst >> 2) & 0x7)]);
    advance_pc2(cpu); return;
}
op_c_addi: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F;
    int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1F) | 0xFFFFFFE0) : (int32_t)((inst >> 2) & 0x1F);
    if (rd_rs1 != 0) set_reg64_u64(cpu, rd_rs1, get_reg64(cpu, rd_rs1) + imm);
    advance_pc2(cpu); return;
}
op_c_addiw: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F;
    int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1F) | 0xFFFFFFE0) : (int32_t)((inst >> 2) & 0x1F);
    if (rd_rs1 != 0) set_reg64_u64(cpu, rd_rs1, (int64_t)(int32_t)(cpu->x[rd_rs1].lo + imm));
    advance_pc2(cpu); return;
}
op_c_li: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F;
    int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1F) | 0xFFFFFFE0) : (int32_t)((inst >> 2) & 0x1F);
    set_reg64_u64(cpu, rd_rs1, (int64_t)imm); advance_pc2(cpu); return;
}
op_c_lui_sp: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F;
    if (rd_rs1 == 2) {
        int32_t nzimm = (((inst >> 12) & 0x1) << 9) | (((inst >> 3) & 0x3) << 7) | (((inst >> 5) & 0x1) << 6) | (((inst >> 2) & 0x1) << 5) | (((inst >> 6) & 0x1) << 4);
        if ((inst >> 12) & 0x1) nzimm |= 0xFFFFFC00;
        set_reg64_u64(cpu, 2, get_reg64(cpu, 2) + (int64_t)nzimm);
    } else if (rd_rs1 != 0) {
        int32_t imm = (((inst >> 12) & 0x1) << 17) | (((inst >> 2) & 0x1F) << 12);
        if ((inst >> 12) & 0x1) imm |= 0xFFFC0000;
        set_reg64_u64(cpu, rd_rs1, (int64_t)imm);
    }
    advance_pc2(cpu); return;
}
op_c_misc_alu: {
    uint32_t funct2 = (inst >> 10) & 0x3, shamt = ((inst >> 2) & 0x1F) | (((inst >> 12) & 0x1) << 5), rd = ((inst >> 7) & 0x7) | 8;
    if (funct2 == 0x0) set_reg64_u64(cpu, rd, get_reg64(cpu, rd) >> shamt);
    else if (funct2 == 0x1) set_reg64_u64(cpu, rd, (uint64_t)((int64_t)get_reg64(cpu, rd) >> shamt));
    else if (funct2 == 0x2) {
        int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1F) | 0xFFFFFFE0) : (int32_t)((inst >> 2) & 0x1F);
        set_reg64_u64(cpu, rd, get_reg64(cpu, rd) & (uint64_t)(int64_t)imm);
    } else if (funct2 == 0x3) {
        uint32_t bit12 = (inst >> 12) & 0x1, sub_funct = (inst >> 5) & 0x3, rs2 = ((inst >> 2) & 0x7) | 8;
        uint64_t v1 = get_reg64(cpu, rd), v2 = get_reg64(cpu, rs2);
        if (bit12 == 0) {
            if (sub_funct == 0x0) set_reg64_u64(cpu, rd, v1 - v2); else if (sub_funct == 0x1) set_reg64_u64(cpu, rd, v1 ^ v2);
            else if (sub_funct == 0x2) set_reg64_u64(cpu, rd, v1 | v2); else if (sub_funct == 0x3) set_reg64_u64(cpu, rd, v1 & v2);
        } else {
            if (sub_funct == 0x0) set_reg64_u64(cpu, rd, (int64_t)(int32_t)(v1 - v2));
            else if (sub_funct == 0x1) set_reg64_u64(cpu, rd, (int64_t)(int32_t)(v1 + v2));
        }
    }
    advance_pc2(cpu); return;
}
op_c_j: {
    int32_t offset = (((inst >> 12) & 1) << 11) | (((inst >> 8) & 1) << 10) | (((inst >> 9) & 3) << 8) | (((inst >> 6) & 1) << 7) | (((inst >> 7) & 1) << 6) | (((inst >> 2) & 1) << 5) | (((inst >> 11) & 1) << 4) | (((inst >> 3) & 7) << 1);
    if ((inst >> 12) & 1) offset |= 0xFFFFF800;
    cpu->pc.lo += offset; cpu->pc.hi = 0; return;
}
op_c_beqz: {
    int32_t offset = (((inst >> 12) & 1) << 8) | (((inst >> 5) & 3) << 6) | (((inst >> 2) & 1) << 5) | (((inst >> 10) & 3) << 3) | (((inst >> 3) & 3) << 1);
    if ((inst >> 12) & 1) offset |= 0xFFFFFF00;
    if (get_reg64(cpu, ((inst >> 7) & 0x7) | 8) == 0) { cpu->pc.lo += offset; cpu->pc.hi = 0; return; }
    advance_pc2(cpu); return;
}
op_c_bnez: {
    int32_t offset = (((inst >> 12) & 1) << 8) | (((inst >> 5) & 3) << 6) | (((inst >> 2) & 1) << 5) | (((inst >> 10) & 3) << 3) | (((inst >> 3) & 3) << 1);
    if ((inst >> 12) & 1) offset |= 0xFFFFFF00;
    if (get_reg64(cpu, ((inst >> 7) & 0x7) | 8) != 0) { cpu->pc.lo += offset; cpu->pc.hi = 0; return; }
    advance_pc2(cpu); return;
}
op_c_slli: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F, shamt = ((inst >> 2) & 0x1F) | (((inst >> 12) & 0x1) << 5);
    if (rd_rs1 != 0) set_reg64_u64(cpu, rd_rs1, get_reg64(cpu, rd_rs1) << shamt);
    advance_pc2(cpu); return;
}
op_c_lwsp: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F, uimm = (((inst >> 4) & 0x7) << 2) | (((inst >> 12) & 0x1) << 5) | (((inst >> 2) & 0x3) << 6);
    if (rd_rs1 != 0) set_reg64_u64(cpu, rd_rs1, (int64_t)(int32_t)mem_read32((uint32_t)get_reg64(cpu, 2) + uimm));
    advance_pc2(cpu); return;
}
op_c_ldsp: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F, uimm = (((inst >> 12) & 0x1) << 5) | (((inst >> 5) & 0x3) << 3) | (((inst >> 2) & 0x7) << 6);
    if (rd_rs1 != 0) cpu->x[rd_rs1] = mem_read64((uint32_t)get_reg64(cpu, 2) + uimm);
    advance_pc2(cpu); return;
}
op_c_jalr_mv_add: {
    uint32_t rd_rs1 = (inst >> 7) & 0x1F, rs2 = (inst >> 2) & 0x1F, bit12 = (inst >> 12) & 0x1;
    if (bit12 == 0) {
        if (rs2 == 0) { cpu->pc.lo = (uint32_t)get_reg64(cpu, rd_rs1) & ~1U; cpu->pc.hi = 0; return; }
        else { set_reg64_u64(cpu, rd_rs1, get_reg64(cpu, rs2)); advance_pc2(cpu); return; }
    } else {
        if (rs2 == 0) {
            if (rd_rs1 == 0) { // c.ebreak
                cpu->halted = true;
                return;
            }
            uint32_t target = (uint32_t)get_reg64(cpu, rd_rs1) & ~1U;
            set_reg64_u64(cpu, 1, (uint64_t)(cpu->pc.lo + 2));
            cpu->pc.lo = target; cpu->pc.hi = 0; return;
        } else { set_reg64_u64(cpu, rd_rs1, get_reg64(cpu, rd_rs1) + get_reg64(cpu, rs2)); advance_pc2(cpu); return; }
    }
}
op_c_swsp: {
    uint32_t uimm = (((inst >> 9) & 0xF) << 2) | (((inst >> 7) & 0x3) << 6);
    mem_write32((uint32_t)get_reg64(cpu, 2) + uimm, cpu->x[(inst >> 2) & 0x1F].lo);
    advance_pc2(cpu); return;
}
op_c_sdsp: {
    uint32_t uimm = (((inst >> 10) & 0x7) << 3) | (((inst >> 7) & 0x7) << 6);
    mem_write64((uint32_t)get_reg64(cpu, 2) + uimm, cpu->x[(inst >> 2) & 0x1F]);
    advance_pc2(cpu); return;
}

op_c_unknown: printf("[UNKNOWN RVC] inst=0x%04X at PC=0x%08X\n", inst, cpu->pc.lo); cpu->halted = true;
}