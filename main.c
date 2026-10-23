#include <stdio.h>
#include <pico/stdlib.h>

// RV64のCPU状態
typedef struct {
    uint64_t pc;
    uint64_t x[32];
} RV64_CPU_t;

// Flash上に配置した xv6 の kernel.bin（30,816バイト）の物理アドレス
#define PICO_FLASH_KERNEL_ADDR  ((const uint8_t*)(0x10100000))
#define K_BIN_SIZE              30816
#define VIRT_RAM_START          0x80000000ULL

// 内蔵SRAM上に確保する仮想RAM空間（256KB）
__attribute__((aligned(4096)))
uint8_t virtual_ram[256 * 1024];

int main() {
    // RP2350の初期化（クロックやUARTなど）
    stdio_init_all();
    printf("Starting RV64 Emulator on Hazard3...\n");

    RV64_CPU_t cpu = {0};
    cpu.pc = 0x80000000ULL; // xv6の開始アドレス

    while (1) {
        // 1. FETCH (命令の読み込み)
        uint32_t insn = 0;
        uint64_t offset = cpu.pc - VIRT_RAM_START;
        
        if (offset < K_BIN_SIZE) {
            // Flashからダイレクト読み込み
            insn = *(const uint32_t*)(PICO_FLASH_KERNEL_ADDR + offset);
        } else {
            // SRAM(仮想RAM)から読み込み
            insn = *(uint32_t*)&virtual_ram[offset];
        }

        // デバッグ表示：現在のPCとフェッチした命令コードをシリアル出力
        printf("PC: 0x%016llx, INSN: 0x%08x\n", cpu.pc, insn);

        // 2. DECODE & EXECUTE (解釈と実行)
        // ここに命令ごとの処理をガシガシ書いていく！
        
        // とりあえず1命令（4バイト）進める（仮実装）
        cpu.pc += 4;

        // あまりに速く回りすぎるとシリアル出力が溢れるので、開発初期は少しウェイトを入れる
        sleep_ms(100);
    }
}
