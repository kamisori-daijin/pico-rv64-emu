cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM rp2350-riscv)
set(PICO_BOARD pico2_w)
set(PICO_GCC_TRIPLE "riscv64-unknown-elf")

# import sdk
include(pico_sdk_import.cmake)

project(rv64_emu C CXX ASM)
pico_sdk_init()

# Load Wi-Fi Encryption
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wifi_credentials.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/wifi_credentials.cmake")
endif()

if(NOT DEFINED WIFI_SSID)
    set(WIFI_SSID $ENV{WIFI_SSID})
endif()
if(NOT DEFINED WIFI_PASSWORD)
    set(WIFI_PASSWORD $ENV{WIFI_PASSWORD})
endif()

if(NOT WIFI_SSID)
    set(WIFI_SSID "DEFAULT_SSID")
    set(WIFI_PASSWORD "DEFAULT_PASS")
endif()

add_executable(rv64_emu
    main.c
    memory.c 
    multicore_sub.c
    cpu/decode_i.c
    cpu/decode_c.c
    devices/uart.c
    devices/clint.c   
    devices/fs_img.c
    devices/virtio.c
    mmu/mmu.c
    devices/plic.c
    mmu/tlb.c
    net/net.c        
    ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin.inc
)

target_compile_definitions(rv64_emu PRIVATE
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
)

target_link_libraries(rv64_emu 
    pico_stdlib
    pico_multicore
    pico_bootsel_via_double_reset
    pico_cyw43_arch_lwip_poll
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_add_extra_outputs(rv64_emu)

# UART and USB
pico_enable_stdio_uart(rv64_emu 1)
pico_enable_stdio_usb(rv64_emu 1)

add_custom_target(flash
    COMMAND /opt/homebrew/bin/picotool load -f -x rv64_emu.uf2
    DEPENDS rv64_emu
)

include_directories(${CMAKE_CURRENT_BINARY_DIR} .)