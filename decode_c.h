#ifndef DECODE_C_H
#define DECODE_C_H

#include <stdint.h>
#include "cpu.h"

void execute_compressed_instruction(RV64CPU *cpu, uint16_t inst);

#endif