#include "cpu.h"
#include <stdio.h>
#include <stdbool.h>

extern uint8_t  mem_read8(uint32_t paddr);
extern uint16_t mem_read16(uint32_t paddr);
extern uint32_t mem_read32(uint32_t paddr);
extern reg64_t  mem_read64(uint32_t paddr);

extern void mem_write8(uint32_t paddr, uint8_t val);
extern void mem_write16(uint32_t paddr, uint16_t val);
extern void mem_write32(uint32_t paddr, uint32_t val);
extern void mem_write64(uint32_t paddr, reg64_t val);

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
        d->lo = 0xFFFFFFFF;
        d->hi = 0xFFFFFFFF;
    } else {
        int64_t res = va / vb;
        d->lo = (uint32_t)res;
        d->hi = (uint32_t)((uint64_t)res >> 32);
    }
}

static inline void reg64_divu(reg64_t *d, reg64_t a, reg64_t b) {
    uint64_t va = ((uint64_t)a.hi << 32) | a.lo;
    uint64_t vb = ((uint64_t)b.hi << 32) | b.lo;
    if (vb == 0) {
        d->lo = 0xFFFFFFFF;
        d->hi = 0xFFFFFFFF;
    } else {
        uint64_t res = va / vb;
        d->lo = (uint32_t)res;
        d->hi = (uint32_t)(res >> 32);
    }
}

static inline void reg64_rems(reg64_t *d, reg64_t a, reg64_t b) {
    int64_t va = (int64_t)(((uint64_t)a.hi << 32) | a.lo);
    int64_t vb = (int64_t)(((uint64_t)b.hi << 32) | b.lo);
    if (vb == 0) {
        *d = a;
    } else {
        int64_t res = va % vb;
        d->lo = (uint32_t)res;
        d->hi = (uint32_t)((uint64_t)res >> 32);
    }
}

static inline void reg64_remu(reg64_t *d, reg64_t a, reg64_t b) {
    uint64_t va = ((uint64_t)a.hi << 32) | a.lo;
    uint64_t vb = ((uint64_t)b.hi << 32) | b.lo;
    if (vb == 0) {
        *d = a;
    } else {
        uint64_t res = va % vb;
        d->lo = (uint32_t)res;
        d->hi = (uint32_t)(res >> 32);
    }
}

static inline void reg64_sll(reg64_t *d, reg64_t a, int shift) {
    shift &= 0x3F;
    uint64_t v = ((uint64_t)a.hi << 32) | a.lo;
    v <<= shift;
    d->lo = (uint32_t)v;
    d->hi = (uint32_t)(v >> 32);
}

static inline void reg64_srl(reg64_t *d, reg64_t a, int shift) {
    shift &= 0x3F;
    uint64_t v = ((uint64_t)a.hi << 32) | a.lo;
    v >>= shift;
    d->lo = (uint32_t)v;
    d->hi = (uint32_t)(v >> 32);
}

static inline void reg64_sra(reg64_t *d, reg64_t a, int shift) {
    shift &= 0x3F;
    int64_t v = (int64_t)(((uint64_t)a.hi << 32) | a.lo);
    v >>= shift;
    d->lo = (uint32_t)v;
    d->hi = (uint32_t)((uint64_t)v >> 32);
}

static inline void reg64_from_imm32(reg64_t *d, int32_t imm) {
    d->lo = (uint32_t)imm;
    d->hi = (imm < 0) ? 0xFFFFFFFF : 0x00000000;
}

static inline void reg64_from_u32(reg64_t *d, uint32_t val32) {
    d->lo = val32;
    d->hi = 0x00000000;
}

static inline void reg64_sext32(reg64_t *d, uint32_t val32) {
    int32_t signed_val = (int32_t)val32;
    d->lo = (uint32_t)signed_val;
    d->hi = (signed_val < 0) ? 0xFFFFFFFF : 0x00000000;
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
    int32_t imm = (int32_t)(inst & 0xFFFFF000);
    return imm;
}

void execute_instruction(RV64CPU *cpu, uint32_t inst) {
    uint32_t opcode = inst & 0x7F;
    uint32_t rd     = (inst >> 7) & 0x1F;
    uint32_t funct3 = (inst >> 12) & 0x07;
    uint32_t rs1    = (inst >> 15) & 0x1F;
    uint32_t rs2    = (inst >> 20) & 0x1F;
    uint32_t funct7 = (inst >> 25) & 0x7F;

    int32_t imm_i = (int32_t)inst >> 20;
    int32_t imm_s = (int32_t)(((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
    if (imm_s & 0x800) {
        imm_s |= 0xFFFFF000;
    }

    cpu->x[0].lo = 0;
    cpu->x[0].hi = 0;

    switch (opcode) {
        case 0x33:
            if (funct7 == 0x00) {
                switch (funct3) {
                    case 0x0: reg64_add(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x1: reg64_sll(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2].lo); break;
                    case 0x2: reg64_from_imm32(&cpu->x[rd], reg64_slt(cpu->x[rs1], cpu->x[rs2]) ? 1 : 0); break;
                    case 0x3: reg64_from_imm32(&cpu->x[rd], reg64_sltu(cpu->x[rs1], cpu->x[rs2]) ? 1 : 0); break;
                    case 0x4: cpu->x[rd].lo = cpu->x[rs1].lo ^ cpu->x[rs2].lo; cpu->x[rd].hi = cpu->x[rs1].hi ^ cpu->x[rs2].hi; break;
                    case 0x5: reg64_srl(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2].lo); break;
                    case 0x6: cpu->x[rd].lo = cpu->x[rs1].lo | cpu->x[rs2].lo; cpu->x[rd].hi = cpu->x[rs1].hi | cpu->x[rs2].hi; break;
                    case 0x7: cpu->x[rd].lo = cpu->x[rs1].lo & cpu->x[rs2].lo; cpu->x[rd].hi = cpu->x[rs1].hi & cpu->x[rs2].hi; break;
                }
            } else if (funct7 == 0x20) {
                switch (funct3) {
                    case 0x0: reg64_sub(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x5: reg64_sra(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2].lo); break;
                }
            } else if (funct7 == 0x01) { // M拡張 64-bit
                switch (funct3) {
                    case 0x0: reg64_mul(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x4: reg64_divs(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x5: reg64_divu(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x6: reg64_rems(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x7: reg64_remu(&cpu->x[rd], cpu->x[rs1], cpu->x[rs2]); break;
                }
            }
            break;

        case 0x3B: // OP-32
            {
                uint32_t v1 = cpu->x[rs1].lo;
                uint32_t v2 = cpu->x[rs2].lo;
                uint32_t res32 = 0;
                if (funct7 == 0x00) {
                    if (funct3 == 0x0) res32 = v1 + v2;
                    else if (funct3 == 0x1) res32 = v1 << (v2 & 0x1F);
                    else if (funct3 == 0x5) res32 = v1 >> (v2 & 0x1F);
                } else if (funct7 == 0x20) {
                    if (funct3 == 0x0) res32 = v1 - v2;
                    else if (funct3 == 0x5) res32 = (uint32_t)((int32_t)v1 >> (v2 & 0x1F));
                } else if (funct7 == 0x01) { // M拡張 32-bit (MULW, DIVW, DIVUW, REMW, REMUW)
                    switch (funct3) {
                        case 0x0: res32 = (uint32_t)((int32_t)v1 * (int32_t)v2); break;
                        case 0x4: res32 = (v2 == 0) ? 0xFFFFFFFF : (uint32_t)((int32_t)v1 / (int32_t)v2); break;
                        case 0x5: res32 = (v2 == 0) ? 0xFFFFFFFF : (v1 / v2); break;
                        case 0x6: res32 = (v2 == 0) ? v1 : (uint32_t)((int32_t)v1 % (int32_t)v2); break;
                        case 0x7: res32 = (v2 == 0) ? v1 : (v1 % v2); break;
                    }
                }
                reg64_sext32(&cpu->x[rd], res32);
            }
            break;

        case 0x13:
            {
                reg64_t imm_val;
                reg64_from_imm32(&imm_val, imm_i);
                uint32_t shamt6 = (inst >> 20) & 0x3F;
                switch (funct3) {
                    case 0x0: reg64_add(&cpu->x[rd], cpu->x[rs1], imm_val); break;
                    case 0x2: reg64_from_imm32(&cpu->x[rd], reg64_slt(cpu->x[rs1], imm_val) ? 1 : 0); break;
                    case 0x3: reg64_from_imm32(&cpu->x[rd], reg64_sltu(cpu->x[rs1], imm_val) ? 1 : 0); break;
                    case 0x4: cpu->x[rd].lo = cpu->x[rs1].lo ^ imm_val.lo; cpu->x[rd].hi = cpu->x[rs1].hi ^ imm_val.hi; break;
                    case 0x6: cpu->x[rd].lo = cpu->x[rs1].lo | imm_val.lo; cpu->x[rd].hi = cpu->x[rs1].hi | imm_val.hi; break;
                    case 0x7: cpu->x[rd].lo = cpu->x[rs1].lo & imm_val.lo; cpu->x[rd].hi = cpu->x[rs1].hi & imm_val.hi; break;
                    case 0x1: reg64_sll(&cpu->x[rd], cpu->x[rs1], shamt6); break;
                    case 0x5:
                        if ((inst & 0x40000000) == 0) reg64_srl(&cpu->x[rd], cpu->x[rs1], shamt6);
                        else reg64_sra(&cpu->x[rd], cpu->x[rs1], shamt6);
                        break;
                }
            }
            break;

        case 0x1B:
            {
                uint32_t v = cpu->x[rs1].lo;
                uint32_t res32 = 0;
                if (funct3 == 0x0) res32 = v + imm_i;
                else if (funct3 == 0x1) res32 = v << (rs2 & 0x1F);
                else if (funct3 == 0x5) {
                    if ((inst & 0x40000000) == 0) res32 = v >> (rs2 & 0x1F);
                    else res32 = (uint32_t)((int32_t)v >> (rs2 & 0x1F));
                }
                reg64_sext32(&cpu->x[rd], res32);
            }
            break;

        case 0x03:
            {
                // 修正点: 符号なし加算でアドレスを計算
                uint32_t vaddr = cpu->x[rs1].lo + (uint32_t)imm_i;
                switch (funct3) {
                    case 0x0: reg64_from_imm32(&cpu->x[rd], (int32_t)(int8_t)mem_read8(vaddr)); break;
                    case 0x1: reg64_from_imm32(&cpu->x[rd], (int32_t)(int16_t)mem_read16(vaddr)); break;
                    case 0x2: reg64_from_imm32(&cpu->x[rd], (int32_t)mem_read32(vaddr)); break;
                    case 0x3: cpu->x[rd] = mem_read64(vaddr); break;
                    case 0x4: reg64_from_u32(&cpu->x[rd], (uint32_t)mem_read8(vaddr)); break;
                    case 0x5: reg64_from_u32(&cpu->x[rd], (uint32_t)mem_read16(vaddr)); break;
                    case 0x6: reg64_from_u32(&cpu->x[rd], mem_read32(vaddr)); break;
                }
            }
            break;

        case 0x23:
            {
                // 修正点: 符号なし加算でアドレスを計算
                uint32_t vaddr = cpu->x[rs1].lo + (uint32_t)imm_s;
                switch (funct3) {
                    case 0x0: mem_write8(vaddr, (uint8_t)cpu->x[rs2].lo); break;
                    case 0x1: mem_write16(vaddr, (uint16_t)cpu->x[rs2].lo); break;
                    case 0x2: mem_write32(vaddr, cpu->x[rs2].lo); break;
                    case 0x3: mem_write64(vaddr, cpu->x[rs2]); break;
                }
            }
            break;

        case 0x37: // lui
            reg64_from_imm32(&cpu->x[rd], get_u_imm(inst));
            break;

        case 0x17: // auipc
            {
                int32_t imm_u = get_u_imm(inst);
                reg64_t offset;
                reg64_from_imm32(&offset, imm_u);
                reg64_add(&cpu->x[rd], cpu->pc, offset);
            }
            break;

        case 0x6F: // jal
            {
                int32_t offset = ((inst >> 31) & 0x1) << 20 |
                                 ((inst >> 21) & 0x3FF) << 1 |
                                 ((inst >> 20) & 0x1) << 11 |
                                 ((inst >> 12) & 0xFF) << 12;
                if (offset & 0x100000) offset |= 0xFFE00000;

                reg64_t next_pc;
                reg64_t pc_four = { .lo = 4, .hi = 0 };
                reg64_add(&next_pc, cpu->pc, pc_four);
                cpu->x[rd] = next_pc;

                reg64_t target_offset = { .lo = (uint32_t)offset, .hi = (offset < 0) ? 0xFFFFFFFF : 0 };
                reg64_add(&cpu->pc, cpu->pc, target_offset);
                return;
            }
            break;

        case 0x67: // jalr
            {
                reg64_t next_pc;
                reg64_t pc_four = { .lo = 4, .hi = 0 };
                reg64_add(&next_pc, cpu->pc, pc_four);
                cpu->x[rd] = next_pc;

                reg64_t imm_reg;
                reg64_from_imm32(&imm_reg, imm_i);
                reg64_t target;
                reg64_add(&target, cpu->x[rs1], imm_reg);
                target.lo &= ~1U;

                cpu->pc = target;
                return;
            }
            break;

        case 0x63: // branch
            {
                int32_t imm_b = ((inst >> 31) & 0x1) << 12 |
                                ((inst >> 7) & 0x1) << 11 |
                                ((inst >> 25) & 0x3F) << 5 |
                                ((inst >> 8) & 0xF) << 1;
                if (imm_b & 0x1000) imm_b |= 0xFFFFE000;

                bool take = false;
                switch (funct3) {
                    case 0x0: take = reg64_eq(cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x1: take = !reg64_eq(cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x4: take = reg64_slt(cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x5: take = !reg64_slt(cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x6: take = reg64_sltu(cpu->x[rs1], cpu->x[rs2]); break;
                    case 0x7: take = !reg64_sltu(cpu->x[rs1], cpu->x[rs2]); break;
                }
                if (take) {
                    reg64_t offset = { .lo = (uint32_t)imm_b, .hi = (imm_b < 0) ? 0xFFFFFFFF : 0 };
                    reg64_add(&cpu->pc, cpu->pc, offset);
                    return;
                }
            }
            break;

        case 0x73: // SYSTEM
            {
                uint32_t csr_addr = inst >> 20;
                
                if (funct3 == 0x0) {
                    if (inst == 0x00000073) { // ecall
                        cpu->sepc = cpu->pc;
                        if (cpu->priv_mode == 0) { // User Mode
                            cpu->scause.lo = 8;
                            cpu->scause.hi = 0;
                            
                            uint64_t sstatus_val = ((uint64_t)cpu->sstatus.hi << 32) | cpu->sstatus.lo;
                            uint64_t sie = (sstatus_val >> 1) & 1;
                            sstatus_val &= ~(1ULL << 8);                              // SPP = 0
                            sstatus_val = (sstatus_val & ~(1ULL << 5)) | (sie << 5); // SPIE = SIE
                            sstatus_val &= ~(1ULL << 1);                              // SIE = 0
                            cpu->sstatus.lo = (uint32_t)sstatus_val;
                            cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

                            cpu->priv_mode = 1; // Sモード
                            cpu->pc.lo = cpu->stvec.lo & ~0x3U;
                            cpu->pc.hi = cpu->stvec.hi;
                        } else { // Supervisor Mode
                            cpu->scause.lo = 9;
                            cpu->scause.hi = 0;
                            
                            uint64_t sstatus_val = ((uint64_t)cpu->sstatus.hi << 32) | cpu->sstatus.lo;
                            uint64_t sie = (sstatus_val >> 1) & 1;
                            sstatus_val |= (1ULL << 8);                               // SPP = 1
                            sstatus_val = (sstatus_val & ~(1ULL << 5)) | (sie << 5); // SPIE = SIE
                            sstatus_val &= ~(1ULL << 1);                              // SIE = 0
                            cpu->sstatus.lo = (uint32_t)sstatus_val;
                            cpu->sstatus.hi = (uint32_t)(sstatus_val >> 32);

                            cpu->priv_mode = 1;
                            cpu->pc.lo = cpu->stvec.lo & ~0x3U;
                            cpu->pc.hi = cpu->stvec.hi;
                        }
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
                        return;
                    } else if (inst == 0x10500073) { // wfi
                        reg64_t four = { .lo = 4, .hi = 0 };
                        reg64_add(&cpu->pc, cpu->pc, four);
                        return;
                    }
                }

                reg64_t *target_csr = NULL;
                bool read_only = false;

                switch (csr_addr) {
                    case 0xF14: 
                    case 0xC00: 
                    case 0xC01: 
                        read_only = true;
                        break;
                    case 0x100: target_csr = &cpu->sstatus; break;
                    case 0x104: target_csr = &cpu->sie; break;
                    case 0x105: target_csr = &cpu->stvec; break;
                    case 0x140: target_csr = &cpu->sscratch; break;
                    case 0x141: target_csr = &cpu->sepc; break;
                    case 0x142: target_csr = &cpu->scause; break;
                    case 0x143: target_csr = &cpu->stval; break;
                    case 0x180: target_csr = &cpu->satp; break;
                    case 0x300: target_csr = &cpu->mstatus; break;
                    case 0x302: target_csr = &cpu->medeleg; break;
                    case 0x303: target_csr = &cpu->mideleg; break;
                    case 0x306: target_csr = &cpu->mcounteren; break;
                    case 0x30A: target_csr = &cpu->menvcfg; break;
                    case 0x341: target_csr = &cpu->mepc; break;
                    case 0x14D: target_csr = &cpu->stimecmp; break;
                    default:
                        break;
                }

                reg64_t old_val = { .lo = 0, .hi = 0 };
                if (csr_addr == 0xF14 || csr_addr == 0xC00 || csr_addr == 0xC01) {
                    old_val.lo = 0;
                    old_val.hi = 0;
                } else if (target_csr != NULL) {
                    old_val = *target_csr;
                }

                uint32_t wr_type = funct3 & 0x3;
                if (wr_type != 0 && !read_only) {
                    uint64_t rs1_val = 0;
                    if ((funct3 & 0x4) != 0) {
                        rs1_val = rs1;
                    } else {
                        rs1_val = ((uint64_t)cpu->x[rs1].hi << 32) | cpu->x[rs1].lo;
                    }

                    uint64_t old_u64 = ((uint64_t)old_val.hi << 32) | old_val.lo;
                    uint64_t new_u64 = old_u64;

                    if (wr_type == 1) {
                        new_u64 = rs1_val;
                    } else if (wr_type == 2) {
                        new_u64 = old_u64 | rs1_val;
                    } else if (wr_type == 3) {
                        new_u64 = old_u64 & ~rs1_val;
                    }

                    reg64_t new_val = { .lo = (uint32_t)new_u64, .hi = (uint32_t)(new_u64 >> 32) };

                    if (target_csr != NULL) {
                        *target_csr = new_val;
                    }
                    if (csr_addr == 0x180) {
                        cpu->satp = new_val;
                    }
                }

                if (rd != 0) {
                    cpu->x[rd] = old_val;
                }
            }
            break;

        case 0x2F: // AMO
            {
                uint32_t funct5 = (funct7 >> 2) & 0x1F;
                uint32_t vaddr  = cpu->x[rs1].lo;
                bool is_64      = (funct3 == 3);

                if (is_64) {
                    reg64_t old_val = mem_read64(vaddr);
                    uint64_t old_u64 = ((uint64_t)old_val.hi << 32) | old_val.lo;
                    uint64_t src_u64 = ((uint64_t)cpu->x[rs2].hi << 32) | cpu->x[rs2].lo;
                    uint64_t new_u64 = 0;

                    switch (funct5) {
                        case 0x01: new_u64 = src_u64; break;             // amoswap.d
                        case 0x00: new_u64 = old_u64 + src_u64; break;   // amoadd.d
                        case 0x08: new_u64 = old_u64 | src_u64; break;   // amoor.d
                        case 0x0C: new_u64 = old_u64 & src_u64; break;   // amoand.d
                        case 0x04: new_u64 = old_u64 ^ src_u64; break;   // amoxor.d
                        case 0x02: new_u64 = old_u64; break;             // lr.d
                        case 0x03: new_u64 = src_u64; break;             // sc.d
                        default: new_u64 = src_u64; break;
                    }

                    if (funct5 != 0x02) {
                        reg64_t write_val = { .lo = (uint32_t)new_u64, .hi = (uint32_t)(new_u64 >> 32) };
                        mem_write64(vaddr, write_val);
                    }

                    if (rd != 0) {
                        if (funct5 == 0x03) {
                            cpu->x[rd].lo = 0;
                            cpu->x[rd].hi = 0;
                        } else {
                            cpu->x[rd] = old_val;
                        }
                    }
                } else {
                    uint32_t old_val = mem_read32(vaddr);
                    uint32_t src_val = cpu->x[rs2].lo;
                    uint32_t new_val = 0;

                    switch (funct5) {
                        case 0x01: new_val = src_val; break;             // amoswap.w
                        case 0x00: new_val = old_val + src_val; break;   // amoadd.w
                        case 0x08: new_val = old_val | src_val; break;   // amoor.w
                        case 0x0C: new_val = old_val & src_val; break;   // amoand.w
                        case 0x04: new_val = old_val ^ src_val; break;   // amoxor.w
                        case 0x02: new_val = old_val; break;             // lr.w
                        case 0x03: new_val = src_val; break;             // sc.w
                        default: new_val = src_val; break;
                    }

                    if (funct5 != 0x02) {
                        mem_write32(vaddr, new_val);
                    }

                    if (rd != 0) {
                        if (funct5 == 0x03) {
                            cpu->x[rd].lo = 0;
                            cpu->x[rd].hi = 0;
                        } else {
                            reg64_sext32(&cpu->x[rd], old_val);
                        }
                    }
                }
            }
            break;

        case 0x0F: // FENCE
            break;

        default:
            printf("[UNKNOWN 32-bit INST] inst=0x%08X at PC=0x%08X (opcode=0x%02X)\n", inst, cpu->pc.lo, opcode);
            cpu->halted = true;
            break;
    }

    reg64_t four = { .lo = 4, .hi = 0 };
    reg64_add(&cpu->pc, cpu->pc, four);
}