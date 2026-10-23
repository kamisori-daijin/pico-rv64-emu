#include <stdio.h>
#include <pico/stdlib.h>

// 【超重要】Mac側で作った「kernel.bin.inc」をここでC言語の配列として吸い込む
// これにより、Pico SDKが自動的にRP2350の物理Flash内の安全な場所に焼き込んでくれます！
__attribute__((section(".rodata"))) 
const uint8_t xv6_kernel_rom[] = {
    #include "kernel.bin.inc" 
};

// カーネルの実際のバイト数
#define K_BIN_SIZE              30816
#define VIRT_RAM_START          0x80000000ULL

typedef struct {
    uint64_t pc;
    uint64_t x[32];
} RV64_CPU_t;

__attribute__((aligned(4096)))
uint8_t virtual_ram[256 * 1024];

int main() {
    stdio_init_all();
    
    // USBシリアルがMac側で認識されるまで少しだけ待つ（ログの頭切れ防止）
    sleep_ms(2000); 
    printf("Starting RV64 Emulator on Hazard3...\n");

    RV64_CPU_t cpu = {0};
    cpu.pc = 0x80000000ULL;

    while (1) {
        uint32_t insn = 0;
        uint64_t offset = cpu.pc - VIRT_RAM_START;
        
        if (offset < K_BIN_SIZE) {
            // 【大激変】面倒な物理アドレス指定を撤廃！
            // C言語の配列ポインタ「xv6_kernel_rom」から読むだけで、SDKの力で自動的にFlashから爆速フェッチされます
            insn = *(const uint32_t*)(xv6_kernel_rom + offset);
        } else {
            insn = *(uint32_t*)&virtual_ram[offset];
        }

        printf("PC: 0x%016llx, INSN: 0x%08x\n", cpu.pc, insn);

        cpu.pc += 4;
        sleep_ms(100);
    }
}
