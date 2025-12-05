#ifndef MEMCHIP_H
#define MEMCHIP_H
#include <stdint.h>
#include "pico/types.h"
#include "hardware/pio.h"
#include "pio_patcher.h"

#define MEMCHIP_MAX_VARIANTS 8

typedef struct {
    uint8_t num_variants;
    const char *variant_names[MEMCHIP_MAX_VARIANTS];
    const int (*ram_reads[MEMCHIP_MAX_VARIANTS])(int);
    const void (*ram_writes[MEMCHIP_MAX_VARIANTS])(int, int);
} mem_chip_variants_t;

typedef struct {
    void (*setup_pio)(uint speed_grade, uint variant);
    void (*teardown_pio)();
    int (*ram_read)(int addr);
    void (*ram_write)(int addr, int data);
    uint32_t mem_size;
    uint32_t bits;
    const mem_chip_variants_t *variants;
    uint8_t speed_grades;
    const char *chip_name;
    const char *speed_names[];
} mem_chip_t;

extern PIO pio;
extern uint sm;
extern uint offset; // Returns offset of starting instruction

extern int read_ram1b1r_7p(int addr);
extern void write_ram1b1r_7p(int addr, int data);

extern int read_ram1b1r_8p(int addr);
extern int read_ram1b1r_8p_half_lr(int addr);
extern int read_ram1b1r_8p_half_hr(int addr);
extern int read_ram1b1r_8p_half_lc(int addr);
extern int read_ram1b1r_8p_half_hc(int addr);

extern void write_ram1b1r_8p(int addr, int data);
extern void write_ram1b1r_8p_half_lr(int addr, int data);
extern void write_ram1b1r_8p_half_hr(int addr, int data);
extern void write_ram1b1r_8p_half_lc(int addr, int data);
extern void write_ram1b1r_8p_half_hc(int addr, int data);

extern void ram1b1r_setup_pio(const uint8_t *delay_set, uint variant);
extern void ram1b1r_teardown_pio();
#endif
