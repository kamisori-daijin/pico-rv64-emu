cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-riscv)
set(PICO_BOARD pico2)
set(PICO_GCC_TRIPLE "riscv64-unknown-elf")

# import sdk
include(pico_sdk_import.cmake)

project(rv64_emu C CXX ASM)
pico_sdk_init()


add_executable(rv64_emu
    main.c
    decode_i.c
    decode_c.c
    uart.c
    clint.c   
    mmu.c
    plic.c
    ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
)


target_link_libraries(rv64_emu 
    pico_stdlib
    pico_bootsel_via_double_reset
)


pico_add_extra_outputs(rv64_emu)

# 6.UART and USB
pico_enable_stdio_uart(rv64_emu 1)
pico_enable_stdio_usb(rv64_emu 1)


add_custom_target(flash
    COMMAND /opt/homebrew/bin/picotool load -f -x rv64_emu.uf2
    DEPENDS rv64_emu
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
    COMMAND xxd -i ${CMAKE_CURRENT_SOURCE_DIR}/kernel.bin | sed 's/^unsigned char/const unsigned char/' > ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kernel.bin
)


include_directories(${CMAKE_CURRENT_BINARY_DIR})
