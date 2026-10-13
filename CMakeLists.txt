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
