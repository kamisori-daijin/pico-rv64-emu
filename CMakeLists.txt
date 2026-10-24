cmake_minimum_required(VERSION 3.13)

# 1. ターゲットの指定 (Cortex-M33 / RP2350 ARMモード)
# ※ PICO_PLATFORMを rp2350 にし、riscv用のGCCトリプルは削除します
set(PICO_PLATFORM rp2350)
set(PICO_BOARD pico2)

# 2. SDKのインポート (必ず project() の前に置く)
include(pico_sdk_import.cmake)

project(rv64_emu C CXX ASM)
pico_sdk_init()

# 3. 実行ファイルの定義
add_executable(rv64_emu
    main.c
    decode_i.c
    #decode_c.c
)

# 4. 標準ライブラリとダブルリセット用ライブラリをリンク
target_link_libraries(rv64_emu 
    pico_stdlib
    pico_bootsel_via_double_reset
)

# 5. リンク設定の直後にUF2等の出力生成を追加
pico_add_extra_outputs(rv64_emu)

# 6. 標準入出力をUSBとUARTの両方で有効化
pico_enable_stdio_uart(rv64_emu 1)
pico_enable_stdio_usb(rv64_emu 1)

# 7. flashカスタムターゲット
add_custom_target(flash
    COMMAND /opt/homebrew/bin/picotool load -f -x rv64_emu.uf2
    DEPENDS rv64_emu
)