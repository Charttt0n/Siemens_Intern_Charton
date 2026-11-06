cmake_minimum_required(VERSION 3.20)
project(week1_cortex_m7 C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_executable(demo.elf src/main.c src/startup.S)
target_compile_options(demo.elf PRIVATE -mcpu=cortex-m7 -mthumb -mfloat-abi=soft -g3 -Og -ffreestanding -ffunction-sections -fdata-sections -Wall -Wextra)
target_link_options(demo.elf PRIVATE -mcpu=cortex-m7 -mthumb -mfloat-abi=soft -nostdlib -Wl,--gc-sections -Wl,-Map=${CMAKE_BINARY_DIR}/demo.map -T${CMAKE_SOURCE_DIR}/src/linker.ld)
set_property(TARGET demo.elf APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/src/linker.ld)

# Keep the Week-1 demo available as a regression target.
add_executable(bootloader.elf src/week2/startup.S src/week2/main.c
    src/week2/board.c src/week2/uart.c)
target_compile_options(bootloader.elf PRIVATE -mcpu=cortex-m7 -mthumb
    -mfloat-abi=soft -g3 -Og -ffreestanding -fno-builtin -ffunction-sections
    -fdata-sections -Wall -Wextra -Werror)
target_link_options(bootloader.elf PRIVATE -mcpu=cortex-m7 -mthumb
    -mfloat-abi=soft -nostdlib -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/bootloader.map
    -T${CMAKE_SOURCE_DIR}/src/week2/linker.ld)
set_property(TARGET bootloader.elf APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/week2/linker.ld)

# Week 3 is isolated from the preserved Week 1/2 teaching targets.
function(add_week3_target target)
    add_executable(${target} src/week3/startup.S src/week3/main.c
        src/week3/uart.c src/week3/flash.c src/week2/board.c ${ARGN})
    target_include_directories(${target} PRIVATE src/week3 src/week2)
    target_compile_options(${target} PRIVATE -mcpu=cortex-m7 -mthumb
        -mfloat-abi=soft -g3 -Og -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
    target_link_options(${target} PRIVATE -mcpu=cortex-m7 -mthumb
        -mfloat-abi=soft -nostdlib -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/${target}.map
        -T${CMAKE_SOURCE_DIR}/src/week3/linker.ld)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/week3/linker.ld)
endfunction()
add_week3_target(bootloader-week3.elf)
add_week3_target(week3-test.elf tests/test_flash.c)
target_compile_definitions(week3-test.elf PRIVATE WEEK3_TEST)

# Stage 4: one bootloader ELF and one independently linked application ELF.
function(configure_stage4 target script)
    target_compile_options(${target} PRIVATE -mcpu=cortex-m7 -mthumb
        -mfloat-abi=soft -g3 -Og -ffreestanding -fno-builtin
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
    target_link_options(${target} PRIVATE -mcpu=cortex-m7 -mthumb
        -mfloat-abi=soft -nostdlib -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/${target}.map -T${CMAKE_SOURCE_DIR}/${script})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/${script})
endfunction()
add_executable(bootloader-week4.elf src/week3/startup.S src/week3/uart.c
    src/week3/flash.c src/week2/board.c src/week4/main.c src/week4/image.c
    src/week4/crc32.c src/week4/handoff.S)
target_include_directories(bootloader-week4.elf PRIVATE src/week4 src/week3 src/week2)
configure_stage4(bootloader-week4.elf src/week4/linker.ld)
add_executable(application.elf src/week4/app/startup.S src/week4/app/main.c src/week2/uart.c)
target_include_directories(application.elf PRIVATE src/week2)
configure_stage4(application.elf src/week4/app/linker.ld)

# Stage 5: slot-aware bootloader plus three application variants. The variants
# are built from the same source and differ at run time: the version line is
# compiled in, so the payload and the UART output both change, not just the
# 32-byte image header. The Week-4 targets above stay byte-for-byte available.
add_executable(bootloader-week5.elf src/week3/startup.S src/week3/uart.c
    src/week3/flash.c src/week2/board.c src/week5/main.c src/week5/boot_meta.c
    src/week5/crc16.c src/week5/ymodem.c src/week5/update.c src/week5/timebase.c
    src/week4/image.c src/week4/crc32.c
    src/week4/handoff.S)
target_include_directories(bootloader-week5.elf PRIVATE src/week5 src/week4
    src/week3 src/week2)
configure_stage4(bootloader-week5.elf src/week4/linker.ld)

# READY-boundary harness: uses the same command window and upload path,
# but deliberately leaves active_slot unchanged after successful reception.
add_executable(bootloader-week5-upgrade.elf src/week3/startup.S src/week3/uart.c
    src/week3/flash.c src/week2/board.c src/week5/main.c src/week5/boot_meta.c
    src/week5/crc16.c src/week5/ymodem.c src/week5/update.c src/week5/timebase.c
    src/week4/image.c src/week4/crc32.c src/week4/handoff.S)
target_include_directories(bootloader-week5-upgrade.elf PRIVATE src/week5
    src/week4 src/week3 src/week2)
target_compile_definitions(bootloader-week5-upgrade.elf PRIVATE WEEK5_UPGRADE_ONLY)
configure_stage4(bootloader-week5-upgrade.elf src/week4/linker.ld)

function(add_stage5_app target version)
    add_executable(${target} src/week4/app/startup.S src/week4/app/main.c
        src/week2/uart.c)
    target_include_directories(${target} PRIVATE src/week2)
    target_compile_definitions(${target} PRIVATE APP_VERSION_STRING="v${version}")
    configure_stage4(${target} src/week4/app/linker.ld)
endfunction()
add_stage5_app(application-v1.elf 1)
add_stage5_app(application-v2.elf 2)

add_stage5_app(application-v3.elf 3)

# Test-only operation wrappers are absent from the production ELF.
add_executable(bootloader-week5-fault.elf src/week3/startup.S src/week3/uart.c
    src/week3/flash.c src/week2/board.c src/week5/main.c src/week5/boot_meta.c
    src/week5/crc16.c src/week5/ymodem.c src/week5/update.c src/week5/timebase.c
    src/week4/image.c src/week4/crc32.c src/week4/handoff.S tests/week5_fault.c)
target_include_directories(bootloader-week5-fault.elf PRIVATE src/week5
    src/week4 src/week3 src/week2)
configure_stage4(bootloader-week5-fault.elf src/week4/linker.ld)
target_link_options(bootloader-week5-fault.elf PRIVATE
    -Wl,--wrap=flash_program -Wl,--wrap=flash_erase)
