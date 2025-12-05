#include <stdint.h>
#include "ram4816.h"
#include "pico/types.h"
#include "ram1b1r.pio.h"
#include "mem_chip.h"

#define RAM4816_DELAY_SET_ROWS 3

static const uint8_t ram4816_delays[RAM4816_DELAY_SET_ROWS][RAM1B1R_DELAY_SET_COLS] = {
    {0,  0, 23, 3, 11,  4,  0,  0}, // 100ns
    {0,  0, 25, 4, 12,  6,  1,  0}, // 120ns
    {0,  0, 27, 3, 19,  9,  0,  0}  // 150ns
};

void ram4816_setup_pio(uint speed_grade, uint variant) {
    ram1b1r_setup_pio(ram4816_delays[speed_grade], variant);
}

// This RAM chip configuration
const mem_chip_t ram4816_chip = {
    .setup_pio = ram4816_setup_pio,
    .teardown_pio = ram1b1r_teardown_pio,
    .ram_read = read_ram1b1r_7p,
    .ram_write = write_ram1b1r_7p,
    .mem_size = 16384,
    .bits = 1,
    .variants = NULL,
    .speed_grades = RAM4816_DELAY_SET_ROWS,
    .chip_name = "4816 (16Kx1 use 4164 skt)",
    .speed_names = {"100ns", "120ns", "150ns"}
};