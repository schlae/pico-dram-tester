cmake_minimum_required(VERSION 3.13...3.27)
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350 CACHE STRING "Platform")

# Initialize pico-sdk from submodule
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(pmemtest)

pico_sdk_init()

add_executable(pmemtest)

pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram4164.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram4116.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram4132.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram41128.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram41256.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram_4bit.pio)

target_sources(pmemtest PRIVATE pmemtest.c st7789.c gui.c pio_patcher.c xoroshiro64starstar.c)

target_link_libraries(pmemtest PRIVATE pico_stdlib pico_multicore hardware_pio hardware_spi)

pico_add_extra_outputs(pmemtest)
