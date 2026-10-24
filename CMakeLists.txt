cmake_minimum_required(VERSION 3.13)

# 1. ターゲットとコンパイラの強制指定
set(PICO_PLATFORM rp2350-riscv)
set(PICO_BOARD pico2)
set(PICO_GCC_TRIPLE "riscv64-unknown-elf")

# 2. SDKのインポート
include(pico_sdk_import.cmake)

project(rv64_emu C CXX ASM)
pico_sdk_init()

# 3. 実行ファイルの定義（decode_i.c を追加）
add_executable(rv64_emu
    main.c
    decode_i.c
    #decode_c.c
    ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
    uart.c
)

# 4. 標準ライブラリとダブルリセット用ライブラリをリンク
target_link_libraries(rv64_emu 
    pico_stdlib
    pico_bootsel_via_double_reset
)

# 5. 【超重要】リンク設定の直後にこれを持ってくる！
pico_add_extra_outputs(rv64_emu)

# 6. 標準入出力をUSBとUARTの両方で有効化
pico_enable_stdio_uart(rv64_emu 1)
pico_enable_stdio_usb(rv64_emu 1)

# 7. flashカスタムターゲット
add_custom_target(flash
    COMMAND /opt/homebrew/bin/picotool load -f -x rv64_emu.uf2
    DEPENDS rv64_emu
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
    COMMAND xxd -i ${CMAKE_CURRENT_SOURCE_DIR}/kernel.bin | sed 's/^unsigned char/const unsigned char/' > ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kernel.bin
)

# 実行ファイルが生成されたインクルードファイルを読めるようにする
include_directories(${CMAKE_CURRENT_BINARY_DIR})
