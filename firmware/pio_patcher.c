// Patches PIO program delay fields

#include <string.h>
#include "hardware/pio.h"


uint16_t current_pio_instructions[32];
struct pio_program current_pio_program;

// Copies a const pio program over to our internal buffer
void set_current_pio_program(const struct pio_program *prog)
{
    memcpy(current_pio_instructions, prog->instructions, prog->length * sizeof(uint16_t));
    current_pio_program.instructions = current_pio_instructions;
    current_pio_program.length = prog->length;
    current_pio_program.origin = prog->origin;
    current_pio_program.pio_version = prog->pio_version;
    current_pio_program.used_gpio_ranges = prog->used_gpio_ranges;
}

// Gets a pointer to the current PIO program
struct pio_program *get_current_pio_program()
{
    return &current_pio_program;
}

// Patches the delay/sideset field using the original delay value as an index
// into an array of delays
void pio_patch_delays(const uint8_t *delays, uint8_t length)
{
    uint8_t i;
    uint8_t field;
    uint16_t instr;

    for (i = 0; i < 31; i++) {
        field = (current_pio_instructions[i] >> 8) & 0x1f;

        // 0 is reserved for instructions that don't use this feature
        if ((field > 0) && (field < length)) {
            instr = current_pio_instructions[i] & 0xe0ff;
            instr |= ((delays[field] & 0x1f) << 8);
            current_pio_instructions[i] = instr;
        }
    }
}
