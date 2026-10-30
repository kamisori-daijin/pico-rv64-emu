#include "cpu.h"
#include <stdio.h>
#include <stdbool.h>
#include "pico.h" 

extern uint8_t  mem_read8(uint32_t paddr);
extern uint16_t mem_read16(uint32_t paddr);
extern uint32_t mem_read32(uint32_t paddr);
extern reg64_t  mem_read64(uint32_t paddr);

extern void mem_write8(uint32_t paddr, uint8_t val);
extern void mem_write16(uint32_t paddr, uint16_t val);
extern void mem_write32(uint32_t paddr, uint32_t val);
extern void mem_write64(uint32_t paddr, reg64_t val);

static inline void set_reg64_imm(RV64CPU *cpu, uint32_t rd, int32_t imm) {
    if (rd == 0) return;
    cpu->x[rd].lo = (uint32_t)imm;
    cpu->x[rd].hi = (imm < 0) ? 0xFFFFFFFF : 0x00000000;
}

static inline void set_reg64_u32(RV64CPU *cpu, uint32_t rd, uint32_t val32) {
    if (rd == 0) return;
    cpu->x[rd].lo = val32;
    cpu->x[rd].hi = 0x00000000;
}

static inline void set_reg64_sext32(RV64CPU *cpu, uint32_t rd, uint32_t val32) {
    if (rd == 0) return;
    int32_t signed_val = (int32_t)val32;
    cpu->x[rd].lo = (uint32_t)signed_val;
    cpu->x[rd].hi = (signed_val < 0) ? 0xFFFFFFFF : 0x00000000;
}

static inline void reg64_add(reg64_t *d, reg64_t a, reg64_t b) {
    uint32_t lo = a.lo + b.lo;
    uint32_t carry = (lo < a.lo) ? 1 : 0;
    d->lo = lo;
    d->hi = a.hi + b.hi + carry;
}

static inline void reg64_sub(reg64_t *d, reg64_t a, reg64_t b) {
    uint32_t lo = a.lo - b.lo;
    uint32_t borrow = (a.lo < b.lo) ? 1 : 0;
    d->lo = lo;
    d->hi = a.hi - b.hi - borrow;
}

static inline void reg64_mul(reg64_t *d, reg64_t a, reg64_t b) {
    uint64_t va = ((uint64_t)a.hi << 32) | a.lo;
    uint64_t vb = ((uint64_t)b.hi << 32) | b.lo;
    uint64_t res = va * vb;
    d->lo = (uint32_t)res;
    d->hi = (uint32_t)(res >> 32);
}

static inline void reg64_divs(reg64_t *d, reg64_t a, reg64_t b) {
    int64_t va = (int64_t)(((uint64_t)a.hi << 32) | a.lo);
    int64_t vb = (int64_t)(((uint64_t)b.hi << 32) | b.lo);
    if (vb == 0) {
        d->lo = 0xFFFFFFFF; d->hi = 0xFFFFFFFF;
    } else {
        int64_t res = va / vb;
        d->lo = (uint32_t)res; d->hi = (uint32_t)((uint64_t)res >> 32);
    }
}

static inline void reg64_divu(reg64_t *d, reg64_t a, reg64_t b) {
    uint64_t va = ((uint64_t)a.hi << 32) | a.lo;
    uint64_t vb = ((uint64_t)b.hi << 32) | b.lo;
    if (vb == 0) {
        d->lo = 0xFFFFFFFF; d->hi = 0xFFFFFFFF;
    } else {
        uint64_t res = va / vb;
        d->lo = (uint32_t)res; d->hi = (uint32_t)(res >> 32);
    }
}

static inline void reg64_rems(reg64_t *d, reg64_t a, reg64_t b) {
    int64_t va = (int64_t)(((uint64_t)a.hi << 32) | a.lo);
    int64_t vb = (int64_t)(((uint64_t)b.hi << 32) | b.lo);
    if (vb == 0) {
        *d = a;
    } else {
        int64_t res = va % vb;
        d->lo = (uint32_t)res; d->hi = (uint32_t)((uint64_t)res >> 32);
    }
}

static inline void reg64_remu(reg64_t *d, reg64_t a, reg64_t b) {
    uint64_t va = ((uint64_t)a.hi << 32) | a.lo;
    uint64_t vb = ((uint64_t)b.hi << 32) | b.lo;
    if (vb == 0) {
        *d = a;
    } else {
        uint64_t res = va % vb;
        d->lo = (uint32_t)res; d->hi = (uint32_t)(res >> 32);
    }
}

static inline void reg64_sll(reg64_t *d, reg64_t a, int shift) {
    shift &= 0x3F;
    uint64_t v = ((uint64_t)a.hi << 32) | a.lo;
    v <<= shift;
    d->lo = (uint32_t)v; d->hi = (uint32_t)(v >> 32);
}

static inline void reg64_srl(reg64_t *d, reg64_t a, int shift) {
    shift &= 0x3F;
    uint64_t v = ((uint64_t)a.hi << 32) | a.lo;
    v >>= shift;
    d->lo = (uint32_t)v; d->hi = (uint32_t)(v >> 32);
}

static inline void reg64_sra(reg64_t *d, reg64_t a, int shift) {
    shift &= 0x3F;
    int64_t v = (int64_t)(((uint64_t)a.hi << 32) | a.lo);
    v >>= shift;
    d->lo = (uint32_t)v; d->hi = (uint32_t)((uint64_t)v >> 32);
}

static inline bool reg64_slt(reg64_t a, reg64_t b) {
    int64_t va = (int64_t)(((uint64_t)a.hi << 32) | a.lo);
    int64_t vb = (int64_t)(((uint64_t)b.hi << 32) | b.lo);
    return va < vb;
}

static inline bool reg64_sltu(reg64_t a, reg64_t b) {
    uint64_t va = ((uint64_t)a.hi << 32) | a.lo;
    uint64_t vb = ((uint64_t)b.hi << 32) | b.lo;
    return va < vb;
}

static inline bool reg64_eq(reg64_t a, reg64_t b) {
    return (a.lo == b.lo) && (a.hi == b.hi);
}

static inline int32_t get_u_imm(uint32_t inst) {
    return (int32_t)(inst & 0xFFFFF000);
}

void __not_in_flash_func(execute_instruction)(RV64CPU *cpu, uint32_t inst) {
    uint32_t op5    = (inst >> 2) & 0x1F;
    uint32_t funct3 = (inst >> 12) & 0x07;
    uint32_t key    = (op5 << 3) | funct3;

    static const void *dispatch_table[256] = {
        // --- LOAD ---
        [0x00] = &&op_lb,  [0x01] = &&op_lh,  [0x02] = &&op_lw,  [0x03] = &&op_ld,
        [0x04] = &&op_lbu, [0x05] = &&op_lhu, [0x06] = &&op_lwu,

        // --- FENCE ---
        [0x18 ... 0x1F] = &&op_fence,

        // --- OP-IMM ---
        [0x20] = &&op_addi,  [0x21] = &&op_slli,  [0x22] = &&op_slti, [0x23] = &&op_sltiu,
        [0x24] = &&op_xori,  [0x25] = &&op_srli_srai, [0x26] = &&op_ori, [0x27] = &&op_andi,

        // --- AUIPC ---
        [0x28 ... 0x2F] = &&op_auipc,

        // --- OP-IMM-32 ---
        [0x30] = &&op_addiw, [0x31] = &&op_slliw, [0x35] = &&op_srliw_sraiw,

        // --- STORE ---
        [0x40] = &&op_sb, [0x41] = &&op_sh, [0x42] = &&op_sw, [0x43] = &&op_sd,

        // --- AMO ---
        [0x58 ... 0x5F] = &&op_amo,

        // --- OP ---
        [0x60] = &&op_add_sub_mul, [0x61] = &&op_sll, [0x62] = &&op_slt, [0x63] = &&op_sltu,
        [0x64] = &&op_xor, [0x65] = &&op_srl_sra_div, [0x66] = &&op_or_rem, [0x67] = &&op_and_rem,

        // --- LUI ---
        [0x68 ... 0x6F] = &&op_lui,

        // --- OP-32 ---
        [0x70] = &&op_addw_subw_mulw, [0x71] = &&op_sllw, [0x74] = &&op_divw,
        [0x75] = &&op_srlw_sraw_divuw, [0x76] = &&op_remw, [0x77] = &&op_remuw,

        // --- BRANCH ---
        [0xC0] = &&op_beq,  [0xC1] = &&op_bne,  [0xC4] = &&op_blt,
        [0xC5] = &&op_bge,  [0xC6] = &&op_bltu, [0xC7] = &&op_bgeu,

        // --- JALR ---
        [0xC8 ... 0xCF] = &&op_jalr,

        // --- JAL ---
        [0xD8 ... 0xDF] = &&op_jal,

        // --- SYSTEM ---
        [0xE0 ... 0xE7] = &&op_system,
    };

    cpu->x[0].lo = 0;
    cpu->x[0].hi = 0;

    const void *target = dispatch_table[key];
    if (target) goto *target;
    goto op_unknown;

op_lb: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    set_reg64_imm(cpu, rd, (int32_t)(int8_t)mem_read8(cpu->x[rs1].lo + imm));
    goto end_inst;
}
op_lh: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    set_reg64_imm(cpu, rd, (int32_t)(int16_t)mem_read16(cpu->x[rs1].lo + imm));
    goto end_inst;
}
op_lw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    set_reg64_imm(cpu, rd, (int32_t)mem_read32(cpu->x[rs1].lo + imm));
    goto end_inst;
}
op_ld: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    if (rd != 0) cpu->x[rd] = mem_read64(cpu->x[rs1].lo + imm);
    goto end_inst;
}
op_lbu: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    set_reg64_u32(cpu, rd, (uint32_t)mem_read8(cpu->x[rs1].lo + imm));
    goto end_inst;
}
op_lhu: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    set_reg64_u32(cpu, rd, (uint32_t)mem_read16(cpu->x[rs1].lo + imm));
    goto end_inst;
}
op_lwu: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm = (int32_t)inst >> 20;
    set_reg64_u32(cpu, rd, mem_read32(cpu->x[rs1].lo + imm));
    goto end_inst;
}

op_sb: {
    uint32_t rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    int32_t imm_s = (int32_t)(((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
    if (imm_s & 0x800) imm_s |= 0xFFFFF000;
    mem_write8(cpu->x[rs1].lo + imm_s, (uint8_t)cpu->x[rs2].lo);
    goto end_inst;
}
op_sh: {
    uint32_t rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    int32_t imm_s = (int32_t)(((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
    if (imm_s & 0x800) imm_s |= 0xFFFFF000;
    mem_write16(cpu->x[rs1].lo + imm_s, (uint16_t)cpu->x[rs2].lo);
    goto end_inst;
}
op_sw: {
    uint32_t rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    int32_t imm_s = (int32_t)(((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
    if (imm_s & 0x800) imm_s |= 0xFFFFF000;
    mem_write32(cpu->x[rs1].lo + imm_s, cpu->x[rs2].lo);
    goto end_inst;
}
op_sd: {
    uint32_t rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    int32_t imm_s = (int32_t)(((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
    if (imm_s & 0x800) imm_s |= 0xFFFFF000;
    mem_write64(cpu->x[rs1].lo + imm_s, cpu->x[rs2]);
    goto end_inst;
}

op_addi: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    if (rd != 0) {
        reg64_t imm_val;
        set_reg64_imm(cpu, rd, (int32_t)inst >> 20);
        imm_val = cpu->x[rd];
        reg64_add(&cpu->x[rd], cpu->x[rs1], imm_val);
    }
    goto end_inst;
}
op_slli: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    if (rd != 0) reg64_sll(&cpu->x[rd], cpu->x[rs1], (inst >> 20) & 0x3F);
    goto end_inst;
}
op_slti: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    reg64_t imm_val;
    set_reg64_imm(cpu, 31, (int32_t)inst >> 20); // 共通バッファ
    imm_val = cpu->x[31];
    set_reg64_imm(cpu, rd, reg64_slt(cpu->x[rs1], imm_val) ? 1 : 0);
    goto end_inst;
}
op_sltiu: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    reg64_t imm_val;
    set_reg64_imm(cpu, 31, (int32_t)inst >> 20);
    imm_val = cpu->x[31];
    set_reg64_imm(cpu, rd, reg64_sltu(cpu->x[rs1], imm_val) ? 1 : 0);
    goto end_inst;
}
op_xori: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    if (rd != 0) {
        int32_t imm = (int32_t)inst >> 20;
        uint32_t imm_hi = (imm < 0) ? 0xFFFFFFFF : 0;
        cpu->x[rd].lo = cpu->x[rs1].lo ^ (uint32_t)imm;
        cpu->x[rd].hi = cpu->x[rs1].hi ^ imm_hi;
    }
    goto end_inst;
}
op_srli_srai: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    uint32_t shamt6 = (inst >> 20) & 0x3F;
    if (rd != 0) {
        if ((inst & 0x40000000) == 0) reg64_srl(&cpu->x[rd], cpu->x[rs1], shamt6);
        else reg64_sra(&cpu->x[rd], cpu->x[rs1], shamt6);
    }
    goto end_inst;
}
op_ori: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    if (rd != 0) {
        int32_t imm = (int32_t)inst >> 20;
        uint32_t imm_hi = (imm < 0) ? 0xFFFFFFFF : 0;
        cpu->x[rd].lo = cpu->x[rs1].lo | (uint32_t)imm;
        cpu->x[rd].hi = cpu->x[rs1].hi | imm_hi;
    }
    goto end_inst;
}
op_andi: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    if (rd != 0) {
        int32_t imm = (int32_t)inst >> 20;
        uint32_t imm_hi = (imm < 0) ? 0xFFFFFFFF : 0;
        cpu->x[rd].lo = cpu->x[rs1].lo & (uint32_t)imm;
        cpu->x[rd].hi = cpu->x[rs1].hi & imm_hi;
    }
    goto end_inst;
}

op_addiw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm_i = (int32_t)inst >> 20;
    set_reg64_sext32(cpu, rd, cpu->x[rs1].lo + imm_i);
    goto end_inst;
}
op_slliw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    set_reg64_sext32(cpu, rd, cpu->x[rs1].lo << (rs2 & 0x1F));
    goto end_inst;
}
op_srliw_sraiw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    uint32_t v = cpu->x[rs1].lo;
    uint32_t res32 = ((inst & 0x40000000) == 0) ? (v >> (rs2 & 0x1F)) : (uint32_t)((int32_t)v >> (rs2 & 0x1F));
    set_reg64_sext32(cpu, rd, res32);
    goto end_inst;
}

op_add_sub_mul: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    if (rd != 0) {
        uint32_t funct7 = (inst >> 25) & 0x7F;
        if (funct7 == 0x00) reg64_add(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]);
        else if (funct7 == 0x20) reg64_sub(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]);
        else if (funct7 == 0x01) reg64_mul(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]);
    }
    goto end_inst;
}
op_sll: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    if (rd != 0) reg64_sll(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2].lo);
    goto end_inst;
}
op_slt: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    set_reg64_imm(cpu, rd, reg64_slt(cpu->x[rs1], cpu->x[rs2]) ? 1 : 0);
    goto end_inst;
}
op_sltu: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    set_reg64_imm(cpu, rd, reg64_sltu(cpu->x[rs1], cpu->x[rs2]) ? 1 : 0);
    goto end_inst;
}
op_xor: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    if (rd != 0) {
        cpu->x[rd].lo = cpu->x[rs1].lo ^ cpu->x[rs2].lo;
        cpu->x[rd].hi = cpu->x[rs1].hi ^ cpu->x[rs2].hi;
    }
    goto end_inst;
}
op_srl_sra_div: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    if (rd != 0) {
        uint32_t funct7 = (inst >> 25) & 0x7F;
        if (funct7 == 0x00) reg64_srl(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2].lo);
        else if (funct7 == 0x20) reg64_sra(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2].lo);
        else if (funct7 == 0x01) reg64_divs(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]);
    }
    goto end_inst;
}
op_or_rem: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    if (rd != 0) {
        if (((inst >> 25) & 0x7F) == 0x01) reg64_rems(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]);
        else { cpu->x[rd].lo = cpu->x[rs1].lo | cpu->x[rs2].lo; cpu->x[rd].hi = cpu->x[rs1].hi | cpu->x[rs2].hi; }
    }
    goto end_inst;
}
op_and_rem: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    if (rd != 0) {
        if (((inst >> 25) & 0x7F) == 0x01) reg64_remu(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]);
        else { cpu->x[rd].lo = cpu->x[rs1].lo & cpu->x[rs2].lo; cpu->x[rd].hi = cpu->x[rs1].hi & cpu->x[rs2].hi; }
    }
    goto end_inst;
}

op_addw_subw_mulw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    uint32_t funct7 = (inst >> 25) & 0x7F, v1 = cpu->x[rs1].lo, v2 = cpu->x[rs2].lo, res32 = 0;
    if (funct7 == 0x00) res32 = v1 + v2;
    else if (funct7 == 0x20) res32 = v1 - v2;
    else if (funct7 == 0x01) res32 = (uint32_t)((int32_t)v1 * (int32_t)v2);
    set_reg64_sext32(cpu, rd, res32);
    goto end_inst;
}
op_sllw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    set_reg64_sext32(cpu, rd, cpu->x[rs1].lo << (cpu->x[rs2].lo & 0x1F));
    goto end_inst;
}
op_divw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    uint32_t v1 = cpu->x[rs1].lo, v2 = cpu->x[rs2].lo;
    set_reg64_sext32(cpu, rd, (v2 == 0) ? 0xFFFFFFFF : (uint32_t)((int32_t)v1 / (int32_t)v2));
    goto end_inst;
}
op_srlw_sraw_divuw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    uint32_t funct7 = (inst >> 25) & 0x7F, v1 = cpu->x[rs1].lo, v2 = cpu->x[rs2].lo, res32 = 0;
    if (funct7 == 0x00) res32 = v1 >> (v2 & 0x1F);
    else if (funct7 == 0x20) res32 = (uint32_t)((int32_t)v1 >> (v2 & 0x1F));
    else if (funct7 == 0x01) res32 = (v2 == 0) ? 0xFFFFFFFF : (v1 / v2);
    set_reg64_sext32(cpu, rd, res32);
    goto end_inst;
}
op_remw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    uint32_t v1 = cpu->x[rs1].lo, v2 = cpu->x[rs2].lo;
    set_reg64_sext32(cpu, rd, (v2 == 0) ? v1 : (uint32_t)((int32_t)v1 % (int32_t)v2));
    goto end_inst;
}
op_remuw: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F;
    uint32_t v1 = cpu->x[rs1].lo, v2 = cpu->x[rs2].lo;
    set_reg64_sext32(cpu, rd, (v2 == 0) ? v1 : (v1 % v2));
    goto end_inst;
}

op_lui: {
    uint32_t rd = (inst >> 7) & 0x1F;
    set_reg64_imm(cpu, rd, get_u_imm(inst));
    goto end_inst;
}
op_auipc: {
    uint32_t rd = (inst >> 7) & 0x1F;
    if (rd != 0) {
        reg64_t offset;
        set_reg64_imm(cpu, 31, get_u_imm(inst));
        offset = cpu->x[31];
        reg64_add(&cpu->x[rd], cpu->pc, offset);
    }
    goto end_inst;
}
op_jal: {
    uint32_t rd = (inst >> 7) & 0x1F;
    int32_t offset = ((inst >> 31) & 0x1) << 20 | ((inst >> 21) & 0x3FF) << 1 |
                     ((inst >> 20) & 0x1) << 11 | ((inst >> 12) & 0xFF) << 12;
    if (offset & 0x100000) offset |= 0xFFE00000;
    
    if (rd != 0) {
        cpu->x[rd].lo = cpu->pc.lo + 4;
        cpu->x[rd].hi = 0;
    }
    cpu->pc.lo += (uint32_t)offset;
    cpu->pc.hi = 0; // ★ pc.hi をクリア
    return;
}
op_jalr: {
    uint32_t rd = (inst >> 7) & 0x1F, rs1 = (inst >> 15) & 0x1F;
    int32_t imm_i = (int32_t)inst >> 20;
    uint32_t target = (cpu->x[rs1].lo + (uint32_t)imm_i) & ~1U;
    
    if (rd != 0) {
        cpu->x[rd].lo = cpu->pc.lo + 4;
        cpu->x[rd].hi = 0;
    }
    cpu->pc.lo = target;
    cpu->pc.hi = 0; // ★ pc.hi をクリア
    return;
}

op_beq:  if (reg64_eq(cpu->x[(inst >> 15) & 0x1F], cpu->x[(inst >> 20) & 0x1F])) goto do_branch; goto end_inst;
op_bne:  if (!reg64_eq(cpu->x[(inst >> 15) & 0x1F], cpu->x[(inst >> 20) & 0x1F])) goto do_branch; goto end_inst;
op_blt:  if (reg64_slt(cpu->x[(inst >> 15) & 0x1F], cpu->x[(inst >> 20) & 0x1F])) goto do_branch; goto end_inst;
op_bge:  if (!reg64_slt(cpu->x[(inst >> 15) & 0x1F], cpu->x[(inst >> 20) & 0x1F])) goto do_branch; goto end_inst;
op_bltu: if (reg64_sltu(cpu->x[(inst >> 15) & 0x1F], cpu->x[(inst >> 20) & 0x1F])) goto do_branch; goto end_inst;
op_bgeu: if (!reg64_sltu(cpu->x[(inst >> 15) & 0x1F], cpu->x[(inst >> 20) & 0x1F])) goto do_branch; goto end_inst;

do_branch: {
    int32_t imm_b = ((inst >> 31) & 0x1) << 12 | ((inst >> 7) & 0x1) << 11 |
                    ((inst >> 25) & 0x3F) << 5 | ((inst >> 8) & 0xF) << 1;
    if (imm_b & 0x1000) imm_b |= 0xFFFFE000;
    cpu->pc.lo += (uint32_t)imm_b;
    cpu->pc.hi = 0; // ★ pc.hi をクリア
    return;
}

op_system: {
    uint32_t rd = (inst >> 7) & 0x1F, funct3 = (inst >> 12) & 0x07, rs1 = (inst >> 15) & 0x1F, csr_addr = inst >> 20;
    if (funct3 == 0x0) {
        if (inst == 0x00000073) { // ecall
            cpu->sepc = cpu->pc;
            uint64_t sstatus_val = ((uint64_t)cpu->sstatus.hi << 32) | cpu->sstatus.lo;
            uint64_t sie = (sstatus_val >> 1) & 1;
            
            cpu->scause.hi = 0; // ★ scause.hi を必ずクリア！
            if (cpu->priv_mode == 0) {
                cpu->scause.lo = 8;
            } else {
                cpu->scause.lo = 9;
                sstatus_val |= (1ULL << 8);
            }
            sstatus_val = (sstatus_val & ~(1ULL << 5)) | (sie << 5);
            sstatus_val &= ~(1ULL << 1);
            cpu->sstatus.lo = (uint32_t)sstatus_val;
            cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

            cpu->priv_mode = 1;
            cpu->pc.lo = cpu->stvec.lo & ~0x3U;
            cpu->pc.hi = 0;
            return;
        } else if (inst == 0x30200073) { // mret
            uint64_t mstatus_val = ((uint64_t)cpu->mstatus.hi << 32) | cpu->mstatus.lo;
            cpu->priv_mode = (mstatus_val >> 11) & 0x3;
            uint64_t mpie = (mstatus_val >> 7) & 1;
            mstatus_val = (mstatus_val & ~(1ULL << 3)) | (mpie << 3);
            mstatus_val |= (1ULL << 7);
            mstatus_val &= ~(3ULL << 11);
            cpu->mstatus.lo = (uint32_t)mstatus_val;
            cpu->mstatus.hi = (uint32_t)(mstatus_val >> 32);

            cpu->pc = cpu->mepc;
            cpu->pc.hi = 0;
            return;
        } else if (inst == 0x10200073) { // sret
            uint64_t sstatus_val = ((uint64_t)cpu->sstatus.hi << 32) | cpu->sstatus.lo;
            cpu->priv_mode = (sstatus_val >> 8) & 0x1;
            uint64_t spie = (sstatus_val >> 5) & 1;
            sstatus_val = (sstatus_val & ~(1ULL << 1)) | (spie << 1);
            sstatus_val |= (1ULL << 5);
            sstatus_val &= ~(1ULL << 8);
            cpu->sstatus.lo = (uint32_t)sstatus_val;
            cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

            cpu->pc = cpu->sepc;
            cpu->pc.hi = 0;
            return;
        } else if (inst == 0x10500073) { // wfi
            cpu->pc.lo += 4;
            cpu->pc.hi = 0;
            return;
        }
    }
    reg64_t *target_csr = NULL;
    bool read_only = false;
    if (csr_addr == 0xF14 || csr_addr == 0xC00 || csr_addr == 0xC01) read_only = true;
    else if (csr_addr == 0x100) target_csr = &cpu->sstatus;
    else if (csr_addr == 0x104) target_csr = &cpu->sie;
    else if (csr_addr == 0x105) target_csr = &cpu->stvec;
    else if (csr_addr == 0x140) target_csr = &cpu->sscratch;
    else if (csr_addr == 0x141) target_csr = &cpu->sepc;
    else if (csr_addr == 0x142) target_csr = &cpu->scause;
    else if (csr_addr == 0x143) target_csr = &cpu->stval;
    else if (csr_addr == 0x180) target_csr = &cpu->satp;
    else if (csr_addr == 0x300) target_csr = &cpu->mstatus;
    else if (csr_addr == 0x302) target_csr = &cpu->medeleg;
    else if (csr_addr == 0x303) target_csr = &cpu->mideleg;
    else if (csr_addr == 0x306) target_csr = &cpu->mcounteren;
    else if (csr_addr == 0x30A) target_csr = &cpu->menvcfg;
    else if (csr_addr == 0x341) target_csr = &cpu->mepc;
    else if (csr_addr == 0x14D) target_csr = &cpu->stimecmp;

    reg64_t old_val = { .lo = 0, .hi = 0 };
    if (target_csr != NULL) old_val = *target_csr;
    uint32_t wr_type = funct3 & 0x3;
    if (wr_type != 0 && !read_only) {
        uint64_t rs1_val = (funct3 & 0x4) ? rs1 : (((uint64_t)cpu->x[rs1].hi << 32) | cpu->x[rs1].lo);
        uint64_t old_u64 = ((uint64_t)old_val.hi << 32) | old_val.lo, new_u64 = old_u64;
        if (wr_type == 1) new_u64 = rs1_val;
        else if (wr_type == 2) new_u64 = old_u64 | rs1_val;
        else if (wr_type == 3) new_u64 = old_u64 & ~rs1_val;
        
        reg64_t new_val = { .lo = (uint32_t)new_u64, .hi = (uint32_t)(new_u64 >> 32) };
        if (target_csr != NULL) *target_csr = new_val;
        if (csr_addr == 0x180) cpu->satp = new_val;
    }
    if (rd != 0) cpu->x[rd] = old_val;
    goto end_inst;
}

op_amo: {
    uint32_t rd = (inst >> 7) & 0x1F, funct3 = (inst >> 12) & 0x07, rs1 = (inst >> 15) & 0x1F, rs2 = (inst >> 20) & 0x1F, funct7 = (inst >> 25) & 0x7F;
    uint32_t funct5 = (funct7 >> 2) & 0x1F, vaddr = cpu->x[rs1].lo;
    if (funct3 == 3) {
        reg64_t old_val = mem_read64(vaddr);
        uint64_t old_u64 = ((uint64_t)old_val.hi << 32) | old_val.lo, src_u64 = ((uint64_t)cpu->x[rs2].hi << 32) | cpu->x[rs2].lo, new_u64 = src_u64;
        if (funct5 == 0x00) new_u64 = old_u64 + src_u64;
        else if (funct5 == 0x08) new_u64 = old_u64 | src_u64;
        else if (funct5 == 0x0C) new_u64 = old_u64 & src_u64;
        else if (funct5 == 0x04) new_u64 = old_u64 ^ src_u64;
        else if (funct5 == 0x02) new_u64 = old_u64;
        
        if (funct5 != 0x02) {
            reg64_t write_val = { .lo = (uint32_t)new_u64, .hi = (uint32_t)(new_u64 >> 32) };
            mem_write64(vaddr, write_val);
        }
        if (rd != 0) {
            if (funct5 == 0x03) { cpu->x[rd].lo = 0; cpu->x[rd].hi = 0; }
            else cpu->x[rd] = old_val;
        }
    } else {
        uint32_t old_val = mem_read32(vaddr), src_val = cpu->x[rs2].lo, new_val = src_val;
        if (funct5 == 0x00) new_val = old_val + src_val;
        else if (funct5 == 0x08) new_val = old_val | src_val;
        else if (funct5 == 0x0C) new_val = old_val & src_val;
        else if (funct5 == 0x04) new_val = old_val ^ src_val;
        else if (funct5 == 0x02) new_val = old_val;
        
        if (funct5 != 0x02) mem_write32(vaddr, new_val);
        set_reg64_sext32(cpu, rd, (funct5 == 0x03) ? 0 : old_val);
    }
    goto end_inst;
}

op_fence: goto end_inst;
op_unknown: printf("[UNKNOWN 32-bit INST] inst=0x%08X at PC=0x%08X\n", inst, cpu->pc.lo); cpu->halted = true; return;

end_inst:
    if (!cpu->trap_raised) {
        cpu->pc.lo += 4;
        cpu->pc.hi = 0;
    }
}