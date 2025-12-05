# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13...3.27)
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350 CACHE STRING "Platform")

# Initialize pico-sdk from submodule
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(pmemtest)

pico_sdk_init()

add_executable(pmemtest)

target_compile_definitions(pmemtest PRIVATE
	PLL_SYS_REFDIV=1
	PLL_SYS_VCO_FREQ_HZ=1500000000
	PLL_SYS_POSTDIV1=5
	PLL_SYS_POSTDIV2=1
)

pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram4116.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram4132.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram41128.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram41256.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram_4bit.pio)
pico_generate_pio_header(pmemtest ${CMAKE_CURRENT_LIST_DIR}/ram1b1r.pio)

set(PICO_SDK_PATH "${USERHOME}/.pico-sdk/sdk/${sdkVersion}")

target_include_directories(pmemtest
    PRIVATE ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source
	${CMAKE_CURRENT_LIST_DIR}/submodules/pico-usb-flash-drive
#	${CMAKE_CURRENT_LIST_DIR}
)

target_sources(pmemtest PRIVATE
    gui.c
    mem_chip.c
    ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ff.c
    ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ffsystem.c
    ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source/ffunicode.c
    pio_patcher.c
    pmemtest.c
    ram4164.c
    ram4816.c
    st7789.c
    submodules/pico-usb-flash-drive/flash.c
    submodules/pico-usb-flash-drive/fatfs_driver.c
	submodules/pico-usb-flash-drive/usb_msc_driver.c
	submodules/pico-usb-flash-drive/usb_descriptors.c
    xoroshiro64starstar.c
)

target_link_libraries(pmemtest PRIVATE
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_spi
    tinyusb_additions
    tinyusb_board
    tinyusb_device
    hardware_flash
    hardware_sync
)

pico_enable_stdio_usb(pmemtest 1)
pico_add_extra_outputs(pmemtest)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/submodules/pico-usb-flash-drive) 