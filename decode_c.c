#include "decode_c.h"
#include <stdio.h>

// 外部メモリ読み書き関数
extern uint8_t  mem_read8(uint32_t paddr);
extern uint16_t mem_read16(uint32_t paddr);
extern uint32_t mem_read32(uint32_t paddr);
extern reg64_t   mem_read64(uint32_t paddr);

extern void mem_write8(uint32_t paddr, uint8_t val);
extern void mem_write16(uint32_t paddr, uint16_t val);
extern void mem_write32(uint32_t paddr, uint32_t val);
extern void mem_write64(uint32_t paddr, reg64_t val);

// 3bitの圧縮レジスタ指定 (0~7) を実レジスタ番号 (8~15, s0-s7/a0-a5) に変換
static inline uint32_t c_reg(uint32_t r3) {
    return 8 + r3;
}

static inline uint64_t get_reg64(RV64CPU *cpu, int regno) {
    if (regno == 0) return 0;
    return ((uint64_t)cpu->x[regno].hi << 32) | cpu->x[regno].lo;
}

static inline void set_reg64(RV64CPU *cpu, int regno, uint64_t val) {
    if (regno == 0) return;
    cpu->x[regno].lo = (uint32_t)val;
    cpu->x[regno].hi = (uint32_t)(val >> 32);
}

static inline uint32_t get_pc32(RV64CPU *cpu) {
    return cpu->pc.lo;
}

static inline void set_pc32(RV64CPU *cpu, uint32_t val) {
    cpu->pc.lo = val;
    cpu->pc.hi = 0;
}

void execute_compressed_instruction(RV64CPU *cpu, uint16_t inst) {
    uint32_t quadrant = inst & 0x3;

    switch (quadrant) {
        case 0: {
            uint32_t funct3 = (inst >> 13) & 0x7;
            if (funct3 == 0x0) {
                // c.addi4spn
                uint32_t nzuimm = ((inst >> 10) & 0x7) | ((inst >> 7) & 0x38) | ((inst & 0x40) >> 3) | ((inst & 0x20) >> 1);
                uint32_t rd = c_reg((inst >> 2) & 0x7);
                if (nzuimm == 0) {
                    cpu->halted = true;
                    return;
                }
                uint64_t sp = get_reg64(cpu, 2);
                set_reg64(cpu, rd, sp + nzuimm);
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x2) {
                // c.lw
                uint32_t uimm = ((inst >> 10) & 0x7) | ((inst >> 5) & 0x1) | ((inst >> 6) & 0x1) << 5;
                uint32_t rs1 = c_reg((inst >> 7) & 0x7);
                uint32_t rd = c_reg((inst >> 2) & 0x7);
                uint32_t addr = (uint32_t)get_reg64(cpu, rs1) + uimm;
                uint32_t val = mem_read32(addr);
                set_reg64(cpu, rd, (uint64_t)(int32_t)val);
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x3) {
                // c.ld
                uint32_t uimm = ((inst >> 10) & 0x7) | ((inst >> 5) & 0x3) << 3;
                uint32_t rs1 = c_reg((inst >> 7) & 0x7);
                uint32_t rd = c_reg((inst >> 2) & 0x7);
                uint32_t addr = (uint32_t)get_reg64(cpu, rs1) + uimm;
                reg64_t val = mem_read64(addr);
                cpu->x[rd] = val;
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x6) {
                // c.sw
                uint32_t uimm = ((inst >> 10) & 0x7) | ((inst >> 5) & 0x1) | ((inst >> 6) & 0x1) << 5;
                uint32_t rs1 = c_reg((inst >> 7) & 0x7);
                uint32_t rs2 = c_reg((inst >> 2) & 0x7);
                uint32_t addr = (uint32_t)get_reg64(cpu, rs1) + uimm;
                mem_write32(addr, cpu->x[rs2].lo);
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x7) {
                // c.sd
                uint32_t uimm = ((inst >> 10) & 0x7) | ((inst >> 5) & 0x3) << 3;
                uint32_t rs1 = c_reg((inst >> 7) & 0x7);
                uint32_t rs2 = c_reg((inst >> 2) & 0x7);
                uint32_t addr = (uint32_t)get_reg64(cpu, rs1) + uimm;
                mem_write64(addr, cpu->x[rs2]);
                cpu->pc.lo += 2;
                return;
            }
            break;
        }
        case 1: {
            uint32_t funct3 = (inst >> 13) & 0x7;
            uint32_t rd_rs1 = (inst >> 7) & 0x1f;

            if (funct3 == 0x0) {
                // c.addi / c.nop
                int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1f) | 0xffffffe0) : (int32_t)((inst >> 2) & 0x1f);
                if (rd_rs1 != 0) {
                    set_reg64(cpu, rd_rs1, get_reg64(cpu, rd_rs1) + imm);
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x1) {
                // c.addiw
                int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1f) | 0xffffffe0) : (int32_t)((inst >> 2) & 0x1f);
                if (rd_rs1 != 0) {
                    int32_t val = (int32_t)cpu->x[rd_rs1].lo + imm;
                    set_reg64(cpu, rd_rs1, (int64_t)val);
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x2) {
                // c.li
                int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1f) | 0xffffffe0) : (int32_t)((inst >> 2) & 0x1f);
                set_reg64(cpu, rd_rs1, (int64_t)imm);
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x3) {
                if (rd_rs1 == 2) {
                    // c.addi16sp
                    int32_t nzimm = (((inst >> 12) & 0x1) << 9) | (((inst >> 4) & 0xf) << 5) | (((inst >> 3) & 0x1) << 4) | (((inst >> 2) & 0x1) << 6) | (((inst >> 6) & 0x1) << 8);
                    if ((inst >> 12) & 0x1) nzimm |= 0xfffffc00;
                    set_reg64(cpu, 2, get_reg64(cpu, 2) + nzimm);
                } else if (rd_rs1 != 0) {
                    // c.lui
                    int32_t imm = (((inst >> 12) & 0x1) << 17) | (((inst >> 2) & 0x1f) << 12);
                    if ((inst >> 12) & 0x1) imm |= 0xfffc0000;
                    set_reg64(cpu, rd_rs1, (int64_t)imm);
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x4) {
                uint32_t funct2 = (inst >> 10) & 0x3;
                uint32_t shamt = ((inst >> 2) & 0x1f) | (((inst >> 12) & 0x1) << 5);
                uint32_t rd = ((inst >> 7) & 0x7) | 8;

                if (funct2 == 0x0 || funct2 == 0x1) {
                    uint64_t val = get_reg64(cpu, rd);
                    if (funct2 == 0x0) val >>= shamt;
                    else val = (uint64_t)((int64_t)val >> shamt);
                    set_reg64(cpu, rd, val);
                } else if (funct2 == 0x2) {
                    int32_t imm = ((inst >> 12) & 0x1) ? (int32_t)(((inst >> 2) & 0x1f) | 0xffffffe0) : (int32_t)((inst >> 2) & 0x1f);
                    set_reg64(cpu, rd, get_reg64(cpu, rd) & imm);
                } else if (funct2 == 0x3) {
                    uint32_t sub_funct = (inst >> 5) & 0x3;
                    uint32_t rs2 = ((inst >> 2) & 0x7) | 8;
                    uint64_t v1 = get_reg64(cpu, rd);
                    uint64_t v2 = get_reg64(cpu, rs2);
                    if (sub_funct == 0x0) set_reg64(cpu, rd, v1 - v2);
                    else if (sub_funct == 0x1) set_reg64(cpu, rd, v1 ^ v2);
                    else if (sub_funct == 0x2) set_reg64(cpu, rd, v1 | v2);
                    else if (sub_funct == 0x3) set_reg64(cpu, rd, v1 & v2);
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x5) {
                // c.j
                int32_t offset = (((inst >> 12) & 1) << 11) | (((inst >> 8) & 1) << 10) | (((inst >> 9) & 3) << 8) | (((inst >> 6) & 1) << 7) | (((inst >> 7) & 1) << 6) | (((inst >> 2) & 1) << 5) | (((inst >> 11) & 1) << 4) | (((inst >> 3) & 7) << 1);
                if ((inst >> 12) & 1) offset |= 0xfffff800;
                set_pc32(cpu, get_pc32(cpu) + offset);
                return;
            } else if (funct3 == 0x6) {
                // c.beqz
                int32_t offset = (((inst >> 12) & 1) << 8) | (((inst >> 5) & 3) << 6) | (((inst >> 10) & 3) << 4) | (((inst >> 3) & 3) << 1) | (((inst >> 6) & 1) << 5);
                if ((inst >> 12) & 1) offset |= 0xffffff00;
                uint32_t rs1 = ((inst >> 7) & 0x7) | 8;
                if (get_reg64(cpu, rs1) == 0) {
                    set_pc32(cpu, get_pc32(cpu) + offset);
                    return;
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x7) {
                // c.bnez
                int32_t offset = (((inst >> 12) & 1) << 8) | (((inst >> 5) & 3) << 6) | (((inst >> 10) & 3) << 4) | (((inst >> 3) & 3) << 1) | (((inst >> 6) & 1) << 5);
                if ((inst >> 12) & 1) offset |= 0xffffff00;
                uint32_t rs1 = ((inst >> 7) & 0x7) | 8;
                if (get_reg64(cpu, rs1) != 0) {
                    set_pc32(cpu, get_pc32(cpu) + offset);
                    return;
                }
                cpu->pc.lo += 2;
                return;
            }
            break;
        }
        case 2: {
            uint32_t funct3 = (inst >> 13) & 0x7;
            uint32_t rd_rs1 = (inst >> 7) & 0x1f;
            uint32_t rs2 = (inst >> 2) & 0x1f;
    

            if (funct3 == 0x0) {
                // c.slli
                uint32_t shamt = ((inst >> 2) & 0x1f) | (((inst >> 12) & 0x1) << 5);
                if (rd_rs1 != 0) {
                    set_reg64(cpu, rd_rs1, get_reg64(cpu, rd_rs1) << shamt);
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x2) {
                // c.lwsp
                uint32_t uimm = (((inst >> 4) & 0x7) << 2) | (((inst >> 12) & 0x1) << 5) | (((inst >> 2) & 0x3) << 6);
                uint32_t addr = (uint32_t)get_reg64(cpu, 2) + uimm;
                uint32_t val = mem_read32(addr);
                if (rd_rs1 != 0) set_reg64(cpu, rd_rs1, (int32_t)val);
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x3) {
                // c.ldsp (正確な offset[5|4:3|8:6] 抽出)
                uint32_t uimm = (((inst >> 4) & 0x7) << 6) | (((inst >> 12) & 0x1) << 5) | (((inst >> 2) & 0x3) << 3);
                uint32_t sp_val = (uint32_t)get_reg64(cpu, 2);
                uint32_t addr = sp_val + uimm;
                reg64_t val = mem_read64(addr);
                                
                printf("[DEBUG c.ldsp] sp=0x%08X uimm=%u addr=0x%08X val=0x%08X_%08X\n", sp_val, uimm, addr, val.hi, val.lo);
                fflush(stdout);
                if (rd_rs1 != 0) {
                    cpu->x[rd_rs1] = val;
                }
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x4) {
                uint32_t bit12 = (inst >> 12) & 0x1;
                if (bit12 == 0) {
                    if (rs2 == 0) {
                        // c.jr
                        uint32_t target = (uint32_t)get_reg64(cpu, rd_rs1);
                        printf("[DEBUG c.jr] PC=0x%08X jumping to reg x%u (target=0x%08X, val=0x%08X_%08X)\n", 
                               cpu->pc.lo, rd_rs1, target, cpu->x[rd_rs1].hi, cpu->x[rd_rs1].lo);
                        set_pc32(cpu, target);
                        return;
                    } else {
                        // c.mv
                        set_reg64(cpu, rd_rs1, get_reg64(cpu, rs2));
                        cpu->pc.lo += 2;
                        return;
                    }
                } else {
                    if (rs2 == 0) {
                        // c.jalr
                        uint32_t target = (uint32_t)get_reg64(cpu, rd_rs1);
                        printf("[DEBUG c.jalr] PC=0x%08X jumping to reg x%u (target=0x%08X)\n", cpu->pc.lo, rd_rs1, target);
                        set_reg64(cpu, 1, get_pc32(cpu) + 2);
                        set_pc32(cpu, target);
                        return;
                    } else {
                        // c.add
                        set_reg64(cpu, rd_rs1, get_reg64(cpu, rd_rs1) + get_reg64(cpu, rs2));
                        cpu->pc.lo += 2;
                        return;
                    }
                }
            } else if (funct3 == 0x6) {
                // c.swsp
                uint32_t uimm = (((inst >> 9) & 0xf) << 2) | (((inst >> 7) & 0x3) << 6);
                uint32_t addr = (uint32_t)get_reg64(cpu, 2) + uimm;
                mem_write32(addr, cpu->x[rs2].lo);
                cpu->pc.lo += 2;
                return;
            } else if (funct3 == 0x7) {
                // c.sdsp (★修正: 正しい offset[5:3|8:6] 抽出)
                uint32_t uimm = (((inst >> 10) & 0x7) << 6) | (((inst >> 7) & 0x7) << 3);
                uint32_t addr = (uint32_t)get_reg64(cpu, 2) + uimm;
                mem_write64(addr, cpu->x[rs2]);
                printf("[DEBUG c.sdsp] inst=0x%04X rs2=%u addr=0x%08X val=0x%08X_%08X\n", 
                                       inst, rs2, addr, cpu->x[rs2].hi, cpu->x[rs2].lo);
                                fflush(stdout);
                cpu->pc.lo += 2;
                return;
            }
            break;
        }
    }

    // 詳細なデバッグ情報を吐き出して停止する
    uint32_t f3 = (inst >> 13) & 0x7;
    printf("[UNKNOWN RVC] inst=0x%04X at PC=0x%08X (quadrant=%u, funct3=%u)\n", inst, cpu->pc.lo, quadrant, f3);
    cpu->halted = true;
}