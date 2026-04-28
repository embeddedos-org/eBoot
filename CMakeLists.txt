cmake_minimum_required(VERSION 3.15)
project(eBootloader
    VERSION 0.1.0
    LANGUAGES C
    DESCRIPTION "Multi-platform modular bootloader with RTOS support"
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ====================================================================
# Options
# ====================================================================
set(EBLDR_BOARD "none" CACHE STRING
    "Target board to build (requires cross-compiler). Options: \
stm32f4 | stm32h7 | nrf52 | rpi4 | riscv64_virt | esp32 | x86_64_efi | \
imx8m | am64x | samd51 | sifive_u | cortex_r5 | none")

option(EBLDR_BUILD_TESTS "Build unit tests (native only)" OFF)
option(EBLDR_REQUIRE_SIGNATURES "Require Ed25519 signatures for boot" OFF)
option(EBLDR_RECOVERY_AUTH "Require authentication for recovery commands" OFF)
option(EBLDR_VERIFY_STAGE1 "Verify Stage-1 hash before jump" OFF)
option(EBLDR_HARDENING "Enable compiler hardening flags" ON)
option(EBLDR_SANITIZE "Enable ASAN/UBSAN for host builds" OFF)
option(EBLDR_BUILD_FUZZ "Build libFuzzer fuzz targets" OFF)

set(EBLDR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ====================================================================
# Compiler flags
# ====================================================================
if(CMAKE_CROSSCOMPILING)
    add_compile_options(-Wall -Wextra -Os -ffunction-sections -fdata-sections -fno-common)
    add_link_options(-Wl,--gc-sections)
elseif(MSVC)
    add_compile_options(/W3)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Security hardening flags
if(EBLDR_HARDENING AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
endif()

# Sanitizer flags for host builds
if(EBLDR_SANITIZE AND NOT CMAKE_CROSSCOMPILING AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Security compile definitions
if(EBLDR_REQUIRE_SIGNATURES)
    add_compile_definitions(EBLDR_REQUIRE_SIGNATURES)
endif()
if(EBLDR_RECOVERY_AUTH)
    add_compile_definitions(EBLDR_RECOVERY_AUTH)
endif()
if(EBLDR_VERIFY_STAGE1)
    add_compile_definitions(EBLDR_VERIFY_STAGE1)
endif()

# ====================================================================
# Core Libraries — platform-agnostic, always built
# ====================================================================
# These contain zero architecture-specific code (no inline asm,
# no memory-mapped I/O). They compile on any host: Windows, Linux, Mac.

# ---- HAL dispatch + board registry ----
add_library(eboot_hal STATIC
    hal/hal_core.c
    hal/board_registry.c
)
target_include_directories(eboot_hal PUBLIC ${EBLDR_INCLUDE_DIR})

# ---- Core boot logic ----
add_library(eboot_core STATIC
    core/bootctl.c
    core/image_verify.c
    core/slot_manager.c
    core/boot_policy.c
    core/recovery.c
    core/fw_services.c
    core/fw_update.c
    core/fw_transport_uart.c
    core/rtos_boot.c
    core/rtos_params.c
    core/boot_menu.c
    core/device_table.c
    core/runtime_services.c
    core/crypto_boot.c
    core/multicore.c
    core/board_config.c
    core/dram_init.c
    core/pci_enum.c
    core/storage.c
    core/power_init.c
    core/clock_init.c
    core/mpu_boot.c
    core/ecc_scrub.c
    core/bmc_handoff.c
    core/os_adapter.c
    core/ed25519_verify.c
    core/keystore.c
    core/debug_lock.c
    core/fw_decrypt.c
    core/image_tlv.c
)
target_include_directories(eboot_core PUBLIC ${EBLDR_INCLUDE_DIR})
target_link_libraries(eboot_core PUBLIC eboot_hal)

# ---- Stage-1 boot manager ----
add_library(eboot_stage1 STATIC
    stage1/main.c
    stage1/boot_scan.c
    stage1/boot_select.c
    stage1/boot_log.c
    stage1/jump_app.c
)
target_include_directories(eboot_stage1 PUBLIC ${EBLDR_INCLUDE_DIR})
target_link_libraries(eboot_stage1 PUBLIC eboot_core)

# ====================================================================
# Board Port — architecture-specific, cross-compilation only
# ====================================================================
# Board ports contain MMIO register access, inline assembly, and
# linker scripts specific to their target architecture. They are
# compiled ONLY when a matching cross-compiler is used.
#
# Usage:
#   cmake -B build -DEBLDR_BOARD=stm32f4 \
#     -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake
#
# Native builds (no EBLDR_BOARD or EBLDR_BOARD=none) build only
# the core libraries — sufficient for unit tests and host-side tools.

macro(eboot_add_board BOARD_NAME BOARD_SRC BOARD_DIR)
    add_library(board_${BOARD_NAME} STATIC ${BOARD_SRC})
    target_include_directories(board_${BOARD_NAME} PUBLIC
        ${EBLDR_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/${BOARD_DIR}
    )
    target_link_libraries(board_${BOARD_NAME} PUBLIC eboot_hal)
    message(STATUS "  Board port: ${BOARD_NAME}")
endmacro()

if(NOT EBLDR_BOARD STREQUAL "none")
    # Build the selected board port
    if(EBLDR_BOARD STREQUAL "stm32f4")
        eboot_add_board(stm32f4 boards/stm32f4/board_stm32f4.c boards/stm32f4)
    elseif(EBLDR_BOARD STREQUAL "stm32h7")
        eboot_add_board(stm32h7 boards/stm32h7/board_stm32h7.c boards/stm32h7)
    elseif(EBLDR_BOARD STREQUAL "nrf52")
        eboot_add_board(nrf52 boards/nrf52/board_nrf52.c boards/nrf52)
    elseif(EBLDR_BOARD STREQUAL "rpi4")
        eboot_add_board(rpi4 boards/rpi4/board_rpi4.c boards/rpi4)
    elseif(EBLDR_BOARD STREQUAL "riscv64_virt")
        eboot_add_board(riscv64_virt boards/riscv64_virt/board_riscv64_virt.c boards/riscv64_virt)
    elseif(EBLDR_BOARD STREQUAL "esp32")
        eboot_add_board(esp32 boards/esp32/board_esp32.c boards/esp32)
    elseif(EBLDR_BOARD STREQUAL "x86_64_efi")
        eboot_add_board(x86_64_efi boards/x86_64_efi/board_x86_64_efi.c boards/x86_64_efi)
    elseif(EBLDR_BOARD STREQUAL "imx8m")
        eboot_add_board(imx8m boards/imx8m/board_imx8m.c boards/imx8m)
    elseif(EBLDR_BOARD STREQUAL "am64x")
        eboot_add_board(am64x boards/am64x/board_am64x.c boards/am64x)
    elseif(EBLDR_BOARD STREQUAL "samd51")
        eboot_add_board(samd51 boards/samd51/board_samd51.c boards/samd51)
    elseif(EBLDR_BOARD STREQUAL "sifive_u")
        eboot_add_board(sifive_u boards/sifive_u/board_sifive_u.c boards/sifive_u)
    elseif(EBLDR_BOARD STREQUAL "strongarm")
        eboot_add_board(strongarm boards/strongarm/board_strongarm.c boards/strongarm)
    elseif(EBLDR_BOARD STREQUAL "xscale")
        eboot_add_board(xscale boards/xscale/board_xscale.c boards/xscale)
    elseif(EBLDR_BOARD STREQUAL "frv")
        eboot_add_board(frv boards/frv/board_frv.c boards/frv)
    elseif(EBLDR_BOARD STREQUAL "sh4")
        eboot_add_board(sh4 boards/sh4/board_sh4.c boards/sh4)
    elseif(EBLDR_BOARD STREQUAL "h8300")
        eboot_add_board(h8300 boards/h8300/board_h8300.c boards/h8300)
    elseif(EBLDR_BOARD STREQUAL "x86")
        eboot_add_board(x86 boards/x86/board_x86.c boards/x86)
    elseif(EBLDR_BOARD STREQUAL "mips")
        eboot_add_board(mips boards/mips/board_mips.c boards/mips)
    elseif(EBLDR_BOARD STREQUAL "mn103")
        eboot_add_board(mn103 boards/mn103/board_mn103.c boards/mn103)
    elseif(EBLDR_BOARD STREQUAL "powerpc")
        eboot_add_board(powerpc boards/powerpc/board_powerpc.c boards/powerpc)
    elseif(EBLDR_BOARD STREQUAL "m68k")
        eboot_add_board(m68k boards/m68k/board_m68k.c boards/m68k)
    elseif(EBLDR_BOARD STREQUAL "v850")
        eboot_add_board(v850 boards/v850/board_v850.c boards/v850)
    elseif(EBLDR_BOARD STREQUAL "sparc")
        eboot_add_board(sparc boards/sparc/board_sparc.c boards/sparc)
    elseif(EBLDR_BOARD STREQUAL "stm32mp1")
        eboot_add_board(stm32mp1 boards/stm32mp1/board_stm32mp1.c boards/stm32mp1)
    elseif(EBLDR_BOARD STREQUAL "qemu_arm64")
        eboot_add_board(qemu_arm64 boards/qemu_arm64/board_qemu_arm64.c boards/qemu_arm64)
    elseif(EBLDR_BOARD STREQUAL "cortex_r5")
        eboot_add_board(cortex_r5 boards/cortex_r5/board_cortex_r5.c boards/cortex_r5)
    elseif(EBLDR_BOARD STREQUAL "cortex_m0")
        eboot_add_board(cortex_m0 boards/cortex_m0/board_cortex_m0.c boards/cortex_m0)
    elseif(EBLDR_BOARD STREQUAL "cortex_m0plus")
        eboot_add_board(cortex_m0plus boards/cortex_m0plus/board_cortex_m0plus.c boards/cortex_m0plus)
    elseif(EBLDR_BOARD STREQUAL "cortex_m3")
        eboot_add_board(cortex_m3 boards/cortex_m3/board_cortex_m3.c boards/cortex_m3)
    elseif(EBLDR_BOARD STREQUAL "cortex_m23")
        eboot_add_board(cortex_m23 boards/cortex_m23/board_cortex_m23.c boards/cortex_m23)
    elseif(EBLDR_BOARD STREQUAL "cortex_m33")
        eboot_add_board(cortex_m33 boards/cortex_m33/board_cortex_m33.c boards/cortex_m33)
    elseif(EBLDR_BOARD STREQUAL "cortex_m55")
        eboot_add_board(cortex_m55 boards/cortex_m55/board_cortex_m55.c boards/cortex_m55)
    elseif(EBLDR_BOARD STREQUAL "cortex_m85")
        eboot_add_board(cortex_m85 boards/cortex_m85/board_cortex_m85.c boards/cortex_m85)
    elseif(EBLDR_BOARD STREQUAL "cortex_r4")
        eboot_add_board(cortex_r4 boards/cortex_r4/board_cortex_r4.c boards/cortex_r4)
    elseif(EBLDR_BOARD STREQUAL "cortex_r52")
        eboot_add_board(cortex_r52 boards/cortex_r52/board_cortex_r52.c boards/cortex_r52)
    elseif(EBLDR_BOARD STREQUAL "cortex_a5")
        eboot_add_board(cortex_a5 boards/cortex_a5/board_cortex_a5.c boards/cortex_a5)
    elseif(EBLDR_BOARD STREQUAL "cortex_a9")
        eboot_add_board(cortex_a9 boards/cortex_a9/board_cortex_a9.c boards/cortex_a9)
    elseif(EBLDR_BOARD STREQUAL "cortex_a15")
        eboot_add_board(cortex_a15 boards/cortex_a15/board_cortex_a15.c boards/cortex_a15)
    elseif(EBLDR_BOARD STREQUAL "cortex_a35")
        eboot_add_board(cortex_a35 boards/cortex_a35/board_cortex_a35.c boards/cortex_a35)
    elseif(EBLDR_BOARD STREQUAL "cortex_a55")
        eboot_add_board(cortex_a55 boards/cortex_a55/board_cortex_a55.c boards/cortex_a55)
    elseif(EBLDR_BOARD STREQUAL "cortex_a76")
        eboot_add_board(cortex_a76 boards/cortex_a76/board_cortex_a76.c boards/cortex_a76)
    elseif(EBLDR_BOARD STREQUAL "arm7tdmi")
        eboot_add_board(arm7tdmi boards/arm7tdmi/board_arm7tdmi.c boards/arm7tdmi)
    elseif(EBLDR_BOARD STREQUAL "arm9")
        eboot_add_board(arm9 boards/arm9/board_arm9.c boards/arm9)
    elseif(EBLDR_BOARD STREQUAL "arm11")
        eboot_add_board(arm11 boards/arm11/board_arm11.c boards/arm11)
    elseif(EBLDR_BOARD STREQUAL "avr")
        eboot_add_board(avr boards/avr/board_avr.c boards/avr)
    elseif(EBLDR_BOARD STREQUAL "avr32")
        eboot_add_board(avr32 boards/avr32/board_avr32.c boards/avr32)
    elseif(EBLDR_BOARD STREQUAL "pic16")
        eboot_add_board(pic16 boards/pic16/board_pic16.c boards/pic16)
    elseif(EBLDR_BOARD STREQUAL "pic18")
        eboot_add_board(pic18 boards/pic18/board_pic18.c boards/pic18)
    elseif(EBLDR_BOARD STREQUAL "pic24")
        eboot_add_board(pic24 boards/pic24/board_pic24.c boards/pic24)
    elseif(EBLDR_BOARD STREQUAL "dspic")
        eboot_add_board(dspic boards/dspic/board_dspic.c boards/dspic)
    elseif(EBLDR_BOARD STREQUAL "pic32")
        eboot_add_board(pic32 boards/pic32/board_pic32.c boards/pic32)
    elseif(EBLDR_BOARD STREQUAL "msp430")
        eboot_add_board(msp430 boards/msp430/board_msp430.c boards/msp430)
    elseif(EBLDR_BOARD STREQUAL "c28x")
        eboot_add_board(c28x boards/c28x/board_c28x.c boards/c28x)
    elseif(EBLDR_BOARD STREQUAL "c6000")
        eboot_add_board(c6000 boards/c6000/board_c6000.c boards/c6000)
    elseif(EBLDR_BOARD STREQUAL "pru")
        eboot_add_board(pru boards/pru/board_pru.c boards/pru)
    elseif(EBLDR_BOARD STREQUAL "rl78")
        eboot_add_board(rl78 boards/rl78/board_rl78.c boards/rl78)
    elseif(EBLDR_BOARD STREQUAL "rx")
        eboot_add_board(rx boards/rx/board_rx.c boards/rx)
    elseif(EBLDR_BOARD STREQUAL "tricore")
        eboot_add_board(tricore boards/tricore/board_tricore.c boards/tricore)
    elseif(EBLDR_BOARD STREQUAL "c166")
        eboot_add_board(c166 boards/c166/board_c166.c boards/c166)
    elseif(EBLDR_BOARD STREQUAL "microblaze")
        eboot_add_board(microblaze boards/microblaze/board_microblaze.c boards/microblaze)
    elseif(EBLDR_BOARD STREQUAL "nios2")
        eboot_add_board(nios2 boards/nios2/board_nios2.c boards/nios2)
    elseif(EBLDR_BOARD STREQUAL "openrisc")
        eboot_add_board(openrisc boards/openrisc/board_openrisc.c boards/openrisc)
    elseif(EBLDR_BOARD STREQUAL "lm32")
        eboot_add_board(lm32 boards/lm32/board_lm32.c boards/lm32)
    elseif(EBLDR_BOARD STREQUAL "blackfin")
        eboot_add_board(blackfin boards/blackfin/board_blackfin.c boards/blackfin)
    elseif(EBLDR_BOARD STREQUAL "sharc")
        eboot_add_board(sharc boards/sharc/board_sharc.c boards/sharc)
    elseif(EBLDR_BOARD STREQUAL "hexagon")
        eboot_add_board(hexagon boards/hexagon/board_hexagon.c boards/hexagon)
    elseif(EBLDR_BOARD STREQUAL "ceva")
        eboot_add_board(ceva boards/ceva/board_ceva.c boards/ceva)
    elseif(EBLDR_BOARD STREQUAL "xtensa_hifi")
        eboot_add_board(xtensa_hifi boards/xtensa_hifi/board_xtensa_hifi.c boards/xtensa_hifi)
    elseif(EBLDR_BOARD STREQUAL "arc")
        eboot_add_board(arc boards/arc/board_arc.c boards/arc)
    elseif(EBLDR_BOARD STREQUAL "8051")
        eboot_add_board(8051 boards/8051/board_8051.c boards/8051)
    elseif(EBLDR_BOARD STREQUAL "riscv32")
        eboot_add_board(riscv32 boards/riscv32/board_riscv32.c boards/riscv32)
    elseif(EBLDR_BOARD STREQUAL "esp32s3")
        eboot_add_board(esp32s3 boards/esp32s3/board_esp32s3.c boards/esp32s3)
    elseif(EBLDR_BOARD STREQUAL "esp32c3")
        eboot_add_board(esp32c3 boards/esp32c3/board_esp32c3.c boards/esp32c3)
    elseif(EBLDR_BOARD STREQUAL "mips64")
        eboot_add_board(mips64 boards/mips64/board_mips64.c boards/mips64)
    elseif(EBLDR_BOARD STREQUAL "sparc64")
        eboot_add_board(sparc64 boards/sparc64/board_sparc64.c boards/sparc64)
    elseif(EBLDR_BOARD STREQUAL "ppc64")
        eboot_add_board(ppc64 boards/ppc64/board_ppc64.c boards/ppc64)
    elseif(EBLDR_BOARD STREQUAL "loongarch")
        eboot_add_board(loongarch boards/loongarch/board_loongarch.c boards/loongarch)
    elseif(EBLDR_BOARD STREQUAL "parisc")
        eboot_add_board(parisc boards/parisc/board_parisc.c boards/parisc)
    elseif(EBLDR_BOARD STREQUAL "ia64")
        eboot_add_board(ia64 boards/ia64/board_ia64.c boards/ia64)
    elseif(EBLDR_BOARD STREQUAL "alpha")
        eboot_add_board(alpha boards/alpha/board_alpha.c boards/alpha)
    elseif(EBLDR_BOARD STREQUAL "s390")
        eboot_add_board(s390 boards/s390/board_s390.c boards/s390)
    elseif(EBLDR_BOARD STREQUAL "cris")
        eboot_add_board(cris boards/cris/board_cris.c boards/cris)
    elseif(EBLDR_BOARD STREQUAL "kalimba")
        eboot_add_board(kalimba boards/kalimba/board_kalimba.c boards/kalimba)
    # --- New ARM Cortex-M boards ---
        message(FATAL_ERROR "Unknown board: ${EBLDR_BOARD}. See boards/ directory for available ports.")
    elseif(EBLDR_BOARD STREQUAL "cortex_m3")
        eboot_add_board(cortex_m3 boards/cortex_m3/board_cortex_m3.c boards/cortex_m3)
    elseif(EBLDR_BOARD STREQUAL "cortex_m23")
        eboot_add_board(cortex_m23 boards/cortex_m23/board_cortex_m23.c boards/cortex_m23)
    elseif(EBLDR_BOARD STREQUAL "cortex_m33")
        eboot_add_board(cortex_m33 boards/cortex_m33/board_cortex_m33.c boards/cortex_m33)
    elseif(EBLDR_BOARD STREQUAL "cortex_m55")
        eboot_add_board(cortex_m55 boards/cortex_m55/board_cortex_m55.c boards/cortex_m55)
    elseif(EBLDR_BOARD STREQUAL "cortex_m85")
        eboot_add_board(cortex_m85 boards/cortex_m85/board_cortex_m85.c boards/cortex_m85)
    # --- New ARM Cortex-R boards ---
    elseif(EBLDR_BOARD STREQUAL "cortex_r4")
        eboot_add_board(cortex_r4 boards/cortex_r4/board_cortex_r4.c boards/cortex_r4)
    elseif(EBLDR_BOARD STREQUAL "cortex_r52")
        eboot_add_board(cortex_r52 boards/cortex_r52/board_cortex_r52.c boards/cortex_r52)
    # --- New ARM Cortex-A boards ---
    elseif(EBLDR_BOARD STREQUAL "cortex_a5")
        eboot_add_board(cortex_a5 boards/cortex_a5/board_cortex_a5.c boards/cortex_a5)
    elseif(EBLDR_BOARD STREQUAL "cortex_a9")
        eboot_add_board(cortex_a9 boards/cortex_a9/board_cortex_a9.c boards/cortex_a9)
    elseif(EBLDR_BOARD STREQUAL "cortex_a15")
        eboot_add_board(cortex_a15 boards/cortex_a15/board_cortex_a15.c boards/cortex_a15)
    elseif(EBLDR_BOARD STREQUAL "cortex_a35")
        eboot_add_board(cortex_a35 boards/cortex_a35/board_cortex_a35.c boards/cortex_a35)
    elseif(EBLDR_BOARD STREQUAL "cortex_a55")
        eboot_add_board(cortex_a55 boards/cortex_a55/board_cortex_a55.c boards/cortex_a55)
    elseif(EBLDR_BOARD STREQUAL "cortex_a76")
        eboot_add_board(cortex_a76 boards/cortex_a76/board_cortex_a76.c boards/cortex_a76)
    # --- Legacy ARM boards ---
    elseif(EBLDR_BOARD STREQUAL "arm7tdmi")
        eboot_add_board(arm7tdmi boards/arm7tdmi/board_arm7tdmi.c boards/arm7tdmi)
    elseif(EBLDR_BOARD STREQUAL "arm9")
        eboot_add_board(arm9 boards/arm9/board_arm9.c boards/arm9)
    elseif(EBLDR_BOARD STREQUAL "arm11")
        eboot_add_board(arm11 boards/arm11/board_arm11.c boards/arm11)
    # --- Microchip architectures ---
    elseif(EBLDR_BOARD STREQUAL "avr")
        eboot_add_board(avr boards/avr/board_avr.c boards/avr)
    elseif(EBLDR_BOARD STREQUAL "avr32")
        eboot_add_board(avr32 boards/avr32/board_avr32.c boards/avr32)
    elseif(EBLDR_BOARD STREQUAL "pic16")
        eboot_add_board(pic16 boards/pic16/board_pic16.c boards/pic16)
    elseif(EBLDR_BOARD STREQUAL "pic18")
        eboot_add_board(pic18 boards/pic18/board_pic18.c boards/pic18)
    elseif(EBLDR_BOARD STREQUAL "pic24")
        eboot_add_board(pic24 boards/pic24/board_pic24.c boards/pic24)
    elseif(EBLDR_BOARD STREQUAL "dspic")
        eboot_add_board(dspic boards/dspic/board_dspic.c boards/dspic)
    elseif(EBLDR_BOARD STREQUAL "pic32")
        eboot_add_board(pic32 boards/pic32/board_pic32.c boards/pic32)
    # --- TI architectures ---
    elseif(EBLDR_BOARD STREQUAL "msp430")
        eboot_add_board(msp430 boards/msp430/board_msp430.c boards/msp430)
    elseif(EBLDR_BOARD STREQUAL "c28x")
        eboot_add_board(c28x boards/c28x/board_c28x.c boards/c28x)
    elseif(EBLDR_BOARD STREQUAL "c6000")
        eboot_add_board(c6000 boards/c6000/board_c6000.c boards/c6000)
    elseif(EBLDR_BOARD STREQUAL "pru")
        eboot_add_board(pru boards/pru/board_pru.c boards/pru)
    # --- Renesas + Infineon ---
    elseif(EBLDR_BOARD STREQUAL "rl78")
        eboot_add_board(rl78 boards/rl78/board_rl78.c boards/rl78)
    elseif(EBLDR_BOARD STREQUAL "rx")
        eboot_add_board(rx boards/rx/board_rx.c boards/rx)
    elseif(EBLDR_BOARD STREQUAL "tricore")
        eboot_add_board(tricore boards/tricore/board_tricore.c boards/tricore)
    elseif(EBLDR_BOARD STREQUAL "c166")
        eboot_add_board(c166 boards/c166/board_c166.c boards/c166)
    # --- FPGA soft-cores ---
    elseif(EBLDR_BOARD STREQUAL "microblaze")
        eboot_add_board(microblaze boards/microblaze/board_microblaze.c boards/microblaze)
    elseif(EBLDR_BOARD STREQUAL "nios2")
        eboot_add_board(nios2 boards/nios2/board_nios2.c boards/nios2)
    elseif(EBLDR_BOARD STREQUAL "openrisc")
        eboot_add_board(openrisc boards/openrisc/board_openrisc.c boards/openrisc)
    elseif(EBLDR_BOARD STREQUAL "lm32")
        eboot_add_board(lm32 boards/lm32/board_lm32.c boards/lm32)
    # --- DSP architectures ---
    elseif(EBLDR_BOARD STREQUAL "blackfin")
        eboot_add_board(blackfin boards/blackfin/board_blackfin.c boards/blackfin)
    elseif(EBLDR_BOARD STREQUAL "sharc")
        eboot_add_board(sharc boards/sharc/board_sharc.c boards/sharc)
    elseif(EBLDR_BOARD STREQUAL "hexagon")
        eboot_add_board(hexagon boards/hexagon/board_hexagon.c boards/hexagon)
    elseif(EBLDR_BOARD STREQUAL "ceva")
        eboot_add_board(ceva boards/ceva/board_ceva.c boards/ceva)
    elseif(EBLDR_BOARD STREQUAL "xtensa_hifi")
        eboot_add_board(xtensa_hifi boards/xtensa_hifi/board_xtensa_hifi.c boards/xtensa_hifi)
    # --- Synopsys + 8-bit + Espressif ---
    elseif(EBLDR_BOARD STREQUAL "arc")
        eboot_add_board(arc boards/arc/board_arc.c boards/arc)
    elseif(EBLDR_BOARD STREQUAL "8051")
        eboot_add_board(8051 boards/8051/board_8051.c boards/8051)
    elseif(EBLDR_BOARD STREQUAL "riscv32")
        eboot_add_board(riscv32 boards/riscv32/board_riscv32.c boards/riscv32)
    elseif(EBLDR_BOARD STREQUAL "esp32s3")
        eboot_add_board(esp32s3 boards/esp32s3/board_esp32s3.c boards/esp32s3)
    elseif(EBLDR_BOARD STREQUAL "esp32c3")
        eboot_add_board(esp32c3 boards/esp32c3/board_esp32c3.c boards/esp32c3)
    # --- Server/legacy/exotic ---
    elseif(EBLDR_BOARD STREQUAL "mips64")
        eboot_add_board(mips64 boards/mips64/board_mips64.c boards/mips64)
    elseif(EBLDR_BOARD STREQUAL "sparc64")
        eboot_add_board(sparc64 boards/sparc64/board_sparc64.c boards/sparc64)
    elseif(EBLDR_BOARD STREQUAL "ppc64")
        eboot_add_board(ppc64 boards/ppc64/board_ppc64.c boards/ppc64)
    elseif(EBLDR_BOARD STREQUAL "loongarch")
        eboot_add_board(loongarch boards/loongarch/board_loongarch.c boards/loongarch)
    elseif(EBLDR_BOARD STREQUAL "parisc")
        eboot_add_board(parisc boards/parisc/board_parisc.c boards/parisc)
    elseif(EBLDR_BOARD STREQUAL "ia64")
        eboot_add_board(ia64 boards/ia64/board_ia64.c boards/ia64)
    elseif(EBLDR_BOARD STREQUAL "alpha")
        eboot_add_board(alpha boards/alpha/board_alpha.c boards/alpha)
    elseif(EBLDR_BOARD STREQUAL "s390")
        eboot_add_board(s390 boards/s390/board_s390.c boards/s390)
    elseif(EBLDR_BOARD STREQUAL "cris")
        eboot_add_board(cris boards/cris/board_cris.c boards/cris)
    elseif(EBLDR_BOARD STREQUAL "kalimba")
        eboot_add_board(kalimba boards/kalimba/board_kalimba.c boards/kalimba)
    else()
        message(FATAL_ERROR "Unknown board: ${EBLDR_BOARD}. See boards/ directory for available ports.")
    endif()

    # ---- Firmware executable (cross-compile with linker script) ----
    if(CMAKE_CROSSCOMPILING)
        set(_LD_STAGE0 "${CMAKE_CURRENT_SOURCE_DIR}/boards/${EBLDR_BOARD}/${EBLDR_BOARD}_stage0.ld")
        set(_LD_STAGE1 "${CMAKE_CURRENT_SOURCE_DIR}/boards/${EBLDR_BOARD}/${EBLDR_BOARD}_stage1.ld")

        if(EXISTS "${_LD_STAGE0}")
            add_executable(ebldr_stage0
                stage0/reset_entry.c
                stage0/hw_init_minimal.c
                stage0/watchdog.c
                stage0/recovery_entry.c
                stage0/jump_stage1.c
            )
            target_include_directories(ebldr_stage0 PRIVATE
                ${EBLDR_INCLUDE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/boards/${EBLDR_BOARD}
            )
            target_link_libraries(ebldr_stage0 PRIVATE eboot_core board_${EBLDR_BOARD})
            set_target_properties(ebldr_stage0 PROPERTIES
                LINK_FLAGS "-T${_LD_STAGE0} -nostartfiles"
                SUFFIX ".elf"
            )
            add_custom_command(TARGET ebldr_stage0 POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ebldr_stage0> ebldr_stage0.bin
                COMMENT "Generating stage0 binary for ${EBLDR_BOARD}"
            )
        endif()

        if(EXISTS "${_LD_STAGE1}")
            add_executable(eboot_firmware stage1/main.c)
            target_include_directories(eboot_firmware PRIVATE
                ${EBLDR_INCLUDE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/boards/${EBLDR_BOARD}
            )
            target_link_libraries(eboot_firmware PRIVATE eboot_stage1 board_${EBLDR_BOARD})
            set_target_properties(eboot_firmware PROPERTIES
                LINK_FLAGS "-T${_LD_STAGE1} -nostartfiles"
                SUFFIX ".elf"
            )
            add_custom_command(TARGET eboot_firmware POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:eboot_firmware> eboot_firmware.bin
                COMMENT "Generating firmware binary for ${EBLDR_BOARD}"
            )
        endif()
    endif()
endif()

# ====================================================================
# Flash targets — invokes eFlash tool (cross-compile only)
# ====================================================================
if(NOT EBLDR_BOARD STREQUAL "none" AND CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_custom_target(flash
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/eflash.py
                flash --board ${EBLDR_BOARD}
                --image ${CMAKE_CURRENT_BINARY_DIR}/eboot_firmware.bin
                --verify --reset
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Flashing ${EBLDR_BOARD} via eFlash"
            VERBATIM
        )
        add_custom_target(flash_stage0
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/eflash.py
                flash --board ${EBLDR_BOARD}
                --image ${CMAKE_CURRENT_BINARY_DIR}/ebldr_stage0.bin
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Flashing stage0 for ${EBLDR_BOARD} via eFlash"
            VERBATIM
        )
        add_custom_target(flash_info
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/eflash.py
                info --board ${EBLDR_BOARD}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Flash info for ${EBLDR_BOARD}"
            VERBATIM
        )
    endif()
endif()

# ====================================================================
# Unit Tests — native builds only
# ====================================================================
if(EBLDR_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ====================================================================
# Status
# ====================================================================
message(STATUS "----------------------------------------------------")
message(STATUS "eBootloader v${PROJECT_VERSION}")
message(STATUS "  Board       : ${EBLDR_BOARD}")
message(STATUS "  Tests       : ${EBLDR_BUILD_TESTS}")
message(STATUS "  Cross-compile: ${CMAKE_CROSSCOMPILING}")
if(EBLDR_BOARD STREQUAL "none")
    message(STATUS "  Mode        : Core libraries only (host build)")
    message(STATUS "  Outputs     : eboot_hal, eboot_core, eboot_stage1")
    message(STATUS "")
    message(STATUS "  To build for a target board:")
    message(STATUS "    cmake -B build -DEBLDR_BOARD=stm32f4 \\")
    message(STATUS "      -DCMAKE_TOOLCHAIN_FILE=toolchains/arm-none-eabi.cmake")
else()
    message(STATUS "  Mode        : Board port (${EBLDR_BOARD})")
endif()
message(STATUS "----------------------------------------------------")
