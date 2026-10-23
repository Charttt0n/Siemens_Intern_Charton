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
