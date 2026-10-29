#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

// 32bit pair for 64bit processor
typedef struct {
    uint32_t lo; // up 32bit
    uint32_t hi; // down 32bit
} reg64_t;

typedef struct {
    reg64_t x[32];      // 64bit register(x0 is always 0)
    reg64_t pc;         // 64program counter
    
    // CSR
    reg64_t satp;
    reg64_t sstatus;
    reg64_t mstatus;
    reg64_t sepc;
    reg64_t mepc;
    reg64_t stvec;
    reg64_t scause;
    reg64_t sscratch;   
    reg64_t stval;     
    reg64_t medeleg;
    reg64_t mideleg;
    reg64_t sie;
    reg64_t mip;
    reg64_t mcounteren;
    reg64_t menvcfg;
    reg64_t stimecmp;
    bool trap_raised;
    uint32_t priv_mode;  // 0: User, 1: Supervisor, 3: Machine
    bool halted;         // wfi stop flag
} RV64CPU;

//memory primitives
uint8_t  mem_read8(uint32_t paddr);
uint16_t mem_read16(uint32_t paddr);
uint32_t mem_read32(uint32_t paddr);
reg64_t  mem_read64(uint32_t paddr);

void mem_write8(uint32_t paddr, uint8_t val);
void mem_write16(uint32_t paddr, uint16_t val);
void mem_write32(uint32_t paddr, uint32_t val);
void mem_write64(uint32_t paddr, reg64_t val);

// run main function
void execute_instruction(RV64CPU *cpu, uint32_t inst);

#endif