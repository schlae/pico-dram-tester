// Main entry point

// TODO:
// Make the refresh test fancier
// Bug fix the 41128

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "pio_patcher.h"
#include "xoroshiro64starstar.h"
#include "mem_chip.h"
#include <bsp/board.h>
#include <tusb.h>
#include <ff.h>
#include <flash.h>

// Defined RAM pio programs
#include "ram4116.pio.h"
#include "ram4816.h"
#include "ram4132.pio.h"
#include "ram4164.h"
#include "ram41128.pio.h"
#include "ram41256.pio.h"
#include "ram_4bit.pio.h"

#include "st7789.h"

// Icons
#include "chip_icon.h"
#include "warn_icon.h"
#include "error_icon.h"
#include "check_icon.h"
#include "drum_icon0.h"
#include "drum_icon1.h"
#include "drum_icon2.h"
#include "drum_icon3.h"

#include "gui.h"


#define GPIO_POWER 4
#define GPIO_QUAD_A 22
#define GPIO_QUAD_B 26
#define GPIO_QUAD_BTN 27
#define GPIO_BACK_BTN 28
#define GPIO_LED 25

// Status shared variables between cores
// Not really thread safe but this
// status is unimportant.
volatile int stat_cur_addr;
volatile int stat_old_addr;
volatile int stat_cur_bit;
queue_t stat_cur_test;
volatile int stat_cur_subtest;

static uint ram_bit_mask;

gui_listbox_t *cur_menu;

#define MAIN_MENU_ITEMS 16
char *main_menu_items[MAIN_MENU_ITEMS];
gui_listbox_t main_menu = {7, 40, 220, MAIN_MENU_ITEMS, 4, 0, 0, main_menu_items};

#define NUM_CHIPS 12
const mem_chip_t *chip_list[] = {&ram4027_chip, &ram4116_half_chip, &ram4116_chip, &ram4816_chip,
                                 &ram4132_stk_chip, &ram4164_half_chip, &ram4164_chip,
                                 &ram41128_chip, &ram41256_chip, &ram4416_half_chip,
                                 &ram4416_chip, &ram4464_chip, &ram44256_chip};

gui_listbox_t variants_menu = {7, 40, 220, 0, 4, 0, 0, 0};
gui_listbox_t speed_menu = {7, 40, 220, 0, 4, 0, 0, 0};


typedef enum {
    MAIN_MENU,
    VARIANT_MENU,
    SPEED_MENU,
    DO_SOCKET,
    DO_TEST,
    TEST_RESULTS
} gui_state_t;

gui_state_t gui_state = MAIN_MENU;

void setup_main_menu()
{
    uint i;
    for (i = 0; i < NUM_CHIPS; i++) {
        main_menu_items[i] = (char *)chip_list[i]->chip_name;
    }
    main_menu.tot_lines = NUM_CHIPS;
}

// Function queue entry for dispatching worker functions
typedef struct
{
    uint32_t (*func)(uint32_t, uint32_t);
    uint32_t data;
    uint32_t data2;
} queue_entry_t;

queue_t call_queue;
queue_t results_queue;

// Entry point for second core. This is just a generic
// function dispatcher lifted from the Raspberry Pi example code.
void core1_entry() {
    while (1) {
        // Function pointer is passed to us via the queue_entry_t which also
        // contains the function parameter.
        // We provide an int32_t return value by simply pushing it back on the
        // return queue which also indicates the result is ready.

        queue_entry_t entry;

        queue_remove_blocking(&call_queue, &entry);

        int32_t result = entry.func(entry.data, entry.data2);

        queue_add_blocking(&results_queue, &result);
    }
}

// Routines for turning on-board power on and off
static inline void power_on()
{
    gpio_set_dir(GPIO_POWER, true);
    gpio_put(GPIO_POWER, false);
    sleep_ms(100);
}

static inline void power_off()
{
    gpio_set_dir(GPIO_POWER, false);
}

// Wrapper that just calls the read routine for the selected chip
static inline int ram_read(int addr)
{
    return chip_list[main_menu.sel_line]->ram_read(addr);
}

// Wrapper that just calls the write routine for the selected chip
static inline void ram_write(int addr, int data)
{
    chip_list[main_menu.sel_line]->ram_write(addr, data);
}

// Low level routines for march-b algorithm
static inline bool me_r0(int a)
{
    int bit = ram_read(a) & ram_bit_mask;
    return (bit == 0);
}

static inline bool me_r1(int a)
{
    int bit = ram_read(a) & ram_bit_mask;
    return (bit == ram_bit_mask);
}

static inline bool me_w0(int a)
{
    ram_write(a, ~ram_bit_mask);
    return true;
}

static inline bool me_w1(int a)
{
    ram_write(a, ram_bit_mask);
    return true;
}

static inline bool marchb_m0(int a)
{
    me_w0(a);
    return true;
}

static inline bool marchb_m1(int a)
{
    return me_r0(a) && me_w1(a) && me_r1(a) && me_w0(a) && me_r0(a) && me_w1(a);
}

static inline bool marchb_m2(int a)
{
    return me_r1(a) && me_w0(a) && me_w1(a);
}

static inline bool marchb_m3(int a)
{
    return me_r1(a) && me_w0(a) && me_w1(a) && me_w0(a);
}

static inline bool marchb_m4(int a)
{
    return me_r0(a) && me_w1(a) && me_w0(a);
}

static inline bool march_element(int addr_size, bool descending, int algorithm)
{
    int inc = descending ? -1 : 1;
    int start = descending ? (addr_size - 1) : 0;
    int end = descending ? -1 : addr_size;
    int a;
    bool ret;

    stat_cur_subtest = algorithm;

    for (stat_cur_addr = start; stat_cur_addr != end; stat_cur_addr += inc) {
        switch (algorithm) {
            case 0:
                ret = marchb_m0(stat_cur_addr);
                break;
            case 1:
                ret = marchb_m1(stat_cur_addr);
                break;
            case 2:
                ret = marchb_m2(stat_cur_addr);
                break;
            case 3:
                ret = marchb_m3(stat_cur_addr);
                break;
            case 4:
                ret = marchb_m4(stat_cur_addr);
                break;
            default:
                break;
        }
        if (!ret) return false;
    }
    return true;
}

uint32_t marchb_testbit(uint32_t addr_size)
{
    bool ret;
    ret = march_element(addr_size, false, 0);
    if (!ret) return false;
    ret = march_element(addr_size, false, 1);
    if (!ret) return false;
    ret = march_element(addr_size, false, 2);
    if (!ret) return false;
    ret = march_element(addr_size, true, 3);
    if (!ret) return false;
    ret = march_element(addr_size, true, 4);
    if (!ret) return false;
    return true;
}

// Runs the memory test on the 2nd core
uint32_t marchb_test(uint32_t addr_size, uint32_t bits)
{
    int failed = 0;
    int bit = 0;

    for (bit = 0; bit < bits; bit++) {
        stat_cur_bit = bit;
        ram_bit_mask = 1 << bit;
        if (!marchb_testbit(addr_size)) {
            failed |= 1 << bit; // fail flag
        }
    }

    return (uint32_t)failed;
}

#define PSEUDO_VALUES 64
#define ARTISANAL_NUMBER 42
static uint64_t random_seeds[PSEUDO_VALUES];

void psrand_init_seeds()
{
    int i;
    psrand_seed(ARTISANAL_NUMBER);
    for (i = 0; i < PSEUDO_VALUES; i++) {
        random_seeds[i] = psrand_next();
    }
}

uint32_t psrand_next_bits(uint32_t bits)
{
    static int bitcount = 0;
    static uint32_t cur_rand;
    uint32_t out;

    if (bitcount < bits) {
        cur_rand = psrand_next();
        bitcount = 32;
    }

    out = cur_rand & ((1 << (bits)) - 1);
    cur_rand = cur_rand >> bits;
    bitcount -= bits;
    return out;
}


// Pseudorandom test
uint32_t psrandom_test(uint32_t addr_size, uint32_t bits)
{
    uint i;
    uint32_t bitsout;
    uint32_t bitsin;
    uint32_t bitshift = addr_size / 4;

    // Write seeded pseudorandom data
    for (i = 0; i < PSEUDO_VALUES; i++) {
        stat_cur_subtest = i >> 2;
        stat_cur_bit = i & 3;
        psrand_seed(random_seeds[i]);
        for (stat_cur_addr = 0; stat_cur_addr < addr_size; stat_cur_addr++) {
            bitsout = psrand_next_bits(bits);
            ram_write(stat_cur_addr, bitsout);
        }

        // Reseed and then read the data back
        psrand_seed(random_seeds[i]);
        for (stat_cur_addr = 0; stat_cur_addr < addr_size; stat_cur_addr++) {
            bitsout = psrand_next_bits(bits);
            bitsin = ram_read(stat_cur_addr);
            if (bitsout != bitsin) {
                return 1;
            }
        }
    }

    return 0;
}

uint32_t refresh_subtest(uint32_t addr_size, uint32_t bits, uint32_t time_delay)
{
    uint32_t bitsout;
    uint32_t bitsin;

    psrand_seed(random_seeds[0]);
    for (stat_cur_addr = 0; stat_cur_addr < addr_size; stat_cur_addr++) {
        bitsout = psrand_next_bits(bits);
        ram_write(stat_cur_addr, bits);
    }

    sleep_us(time_delay);

    psrand_seed(random_seeds[0]);
    for (stat_cur_addr = 0; stat_cur_addr < addr_size; stat_cur_addr++) {
        bitsout = psrand_next_bits(bits);
        bitsin = ram_read(stat_cur_addr);
        if (bits != bitsin) {
            return 1;
        }
    }
    return 0;
}


uint32_t refresh_test(uint32_t addr_size, uint32_t bits)
{
    return refresh_subtest(addr_size, bits, 5000);
}


static const char *ram_test_names[] = {"March-B", "Pseudo", "Refresh"};

// Initial entry for the RAM test routines running
// on the second CPU core.
uint32_t all_ram_tests(uint32_t addr_size, uint32_t bits)
{
    int failed;
    int test = 0;
// Initialize RAM by performing n RAS cycles
    march_element(addr_size, false, 0);
// Now run actual tests
    queue_add_blocking(&stat_cur_test, &test);
    failed = marchb_test(addr_size, bits);
    if (failed) return failed;
    test = 1;
    queue_add_blocking(&stat_cur_test, &test);
    failed = psrandom_test(addr_size, bits);
    if (failed) return failed;
    test = 2;
    queue_add_blocking(&stat_cur_test, &test);
    failed = refresh_test(addr_size, bits);
    if (failed) return failed;
    return 0;
}

typedef struct {
    uint32_t pin;
    uint32_t hcount;
} pin_debounce_t;

#define ENC_DEBOUNCE_COUNT 1000
#define BUTTON_DEBOUNCE_COUNT 50000

// Debounces a pin
uint8_t do_debounce(pin_debounce_t *d)
{
    if (gpio_get(d->pin)) {
        d->hcount++;
        if (d->hcount > ENC_DEBOUNCE_COUNT) d->hcount = ENC_DEBOUNCE_COUNT;
    } else {
        d->hcount = 0;
    }
    return (d->hcount >= ENC_DEBOUNCE_COUNT) ? 1 : 0;
}

// Returns true only *once* when a button is pushed. No key repeat.
bool is_button_pushed(pin_debounce_t *pin_b)
{
    if (!gpio_get(pin_b->pin)) {
        if (pin_b->hcount == 0) {
            pin_b->hcount = BUTTON_DEBOUNCE_COUNT;
            return true;
        }
    } else {
        if (pin_b->hcount > 0) {
            pin_b->hcount--;
        }
    }
    return false;
}

// Setup and display the main menu
void show_main_menu()
{
    cur_menu = &main_menu;
    paint_dialog("Select Device");
    gui_listbox(cur_menu, LIST_ACTION_NONE);
}

void show_variant_menu()
{
    uint chip = main_menu.sel_line;
    cur_menu = &variants_menu;
    paint_dialog("Select Variant");
    variants_menu.items = (char **)chip_list[chip]->variants->variant_names;
    variants_menu.tot_lines = chip_list[chip]->variants->num_variants;
    gui_listbox(cur_menu, LIST_ACTION_NONE);
}

// With the selected chip, populate the speed grade menu and show it
void show_speed_menu()
{
    uint chip = main_menu.sel_line;
    cur_menu = &speed_menu;
    paint_dialog("Select Speed Grade");
    speed_menu.items = (char **)chip_list[chip]->speed_names;
    speed_menu.tot_lines = chip_list[chip]->speed_grades;
    gui_listbox(cur_menu, LIST_ACTION_NONE);
}


#define CELL_STAT_X 9
#define CELL_STAT_Y 33

// Used to update the RAM test GUI left pane
static inline void update_vis_dot(uint16_t cx, uint16_t cy, uint16_t col)
{
    st7789_fill(CELL_STAT_X + cx * 3, CELL_STAT_Y + cy * 3, 2, 2, col);
}

#define STATUS_ICON_X 155
#define STATUS_ICON_Y 65

struct repeating_timer drum_timer;

// Play the drums
bool drum_animation_cb(__unused struct repeating_timer *t)
{
    static uint8_t drum_st = 0;
    drum_st++;
    if (drum_st > 3) drum_st = 0;
    st7789_fill(STATUS_ICON_X, STATUS_ICON_Y, 32, 32, COLOR_LTGRAY);
    switch (drum_st) {
        case 0:
            draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &drum_icon0);
            break;
        case 1:
            draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &drum_icon1);
            break;
        case 2:
            draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &drum_icon2);
            break;
        case 3:
            draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &drum_icon3);
            break;
    }
    return true;
}

// Show the RAM test console GUI
void show_test_gui()
{
    uint16_t cx, cy;
    paint_dialog("Testing...");

    // Cell status area. 32x32 elements.
    fancy_rect(7, 31, 100, 100, B_SUNKEN_OUTER); // Usable size is 220x80.
    fancy_rect(8, 32, 98, 98, B_SUNKEN_INNER);
    st7789_fill(9, 33, 96, 96, COLOR_BLACK);
    for (cy = 0; cy < 32; cy++) {
        for (cx = 0; cx < 32; cx++) {
            update_vis_dot(cx, cy, COLOR_DKGRAY);
        }
    }
    stat_old_addr = 0;
    stat_cur_bit = 0;
    stat_cur_subtest = 0;

    // Current test indicator
    paint_status(120, 35, 110, "      ");
    draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &drum_icon0);
    add_repeating_timer_ms(-100, drum_animation_cb, NULL, &drum_timer);
}

// Begins the RAM test with the selected RAM chip
void start_the_ram_test()
{
    // Get the power turned on
    power_on();

    // Get the PIO going
    chip_list[main_menu.sel_line]->setup_pio(speed_menu.sel_line, variants_menu.sel_line);

    // Dispatch the second core
    // (The memory size is from our memory description data structure)
    queue_entry_t entry = {all_ram_tests,
                           chip_list[main_menu.sel_line]->mem_size,
                           chip_list[main_menu.sel_line]->bits};
    queue_add_blocking(&call_queue, &entry);
}

// Stops the RAM test
void stop_the_ram_test()
{
    chip_list[main_menu.sel_line]->teardown_pio();
    power_off();
}

// Figure out where visualization dot goes and map it
static inline void map_vis_dot(int addr, int ox, int oy, int bitsize, uint16_t col)
{
    int cx, cy;
    if (bitsize == 4) {
        cx = addr & 0xf;
        cy = (addr >> 4) & 0xf;
    } else {
        cx = addr & 0x1f;
        cy = (addr >> 5) & 0x1f;
    }
    update_vis_dot(cx + ox, cy + oy, col);
}

// Draw up visualization from current test state
void do_visualization()
{
    const uint16_t cmap[] = {COLOR_DKBLUE, COLOR_DKGREEN, COLOR_DKMAGENTA, COLOR_DKYELLOW, COLOR_GREEN};
    int bitsize = chip_list[main_menu.sel_line]->bits;
    int new_addr = stat_cur_addr * 1024 / chip_list[main_menu.sel_line]->mem_size / bitsize;
    int bit = stat_cur_bit;
    uint16_t col = cmap[stat_cur_subtest];
    int delta, i;
    int ox, oy = 0;

    if (bitsize == 4) {
        switch (bit) {
            case 1:
                oy = 0;
                ox = 16;
                break;
            case 2:
                oy = 16;
                ox = 0;
                break;
            case 3:
                ox = oy = 16;
                break;
            default:
                ox = oy = 0;
        }
    } else {
        ox = oy = 0;
    }

    if (new_addr > stat_old_addr) {
        delta = new_addr - stat_old_addr;
        for (i = 0; i < delta; i++) {
            map_vis_dot(stat_old_addr + i, ox, oy, bitsize, col);
        }
    } else {
        delta = stat_old_addr - new_addr;
        for (i = delta - 1; i >= 0; i--) {
            map_vis_dot(stat_old_addr + i, ox, oy, bitsize, col);
        }
    }
    stat_old_addr = new_addr;
}

// During a RAM test, updates the status window and checks for the end of the test
void do_status()
{
    uint32_t retval;
    char retstring[30];
    uint16_t v;
    static uint16_t v_prev = 0;
    int test;

    if (gui_state == DO_TEST) {
        do_visualization();

        // Update the status text
        if (queue_try_remove(&stat_cur_test, &test)) {
            paint_status(120, 35, 110, "      ");
            paint_status(120, 35, 110, (char *)ram_test_names[test]);
        }

        // Check official status
        if (!queue_is_empty(&results_queue)) {
            stop_the_ram_test();
            // The RAM test completed, so let's handle that
            sleep_ms(10);
            // No more drums
            cancel_repeating_timer(&drum_timer);
            queue_remove_blocking(&results_queue, &retval);
            // Show the completion status
            gui_state = TEST_RESULTS;
            st7789_fill(STATUS_ICON_X, STATUS_ICON_Y, 32, 32, COLOR_LTGRAY); // Erase icon
            if (retval == 0) {
                paint_status(120, 35, 110, "Passed!");
                draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &check_icon);
            } else {
                draw_icon(STATUS_ICON_X, STATUS_ICON_Y, &error_icon);
                if (chip_list[main_menu.sel_line]->bits == 4) {
                    sprintf(retstring, "Failed %d%d%d%d", (retval >> 3) & 1,
                                                           (retval >> 2) & 1,
                                                            (retval >> 1) & 1,
                                                            (retval & 1));
                    paint_status(120, 105, 110, retstring);
                } else {
                    paint_status(120, 105, 110, "Failed");
                }
            }
        }
    }
}

// Called when user presses the action button
void button_action()
{
    // Do something based on the current menu
    switch (gui_state) {
        case MAIN_MENU:
            // Check for variant
            if (chip_list[main_menu.sel_line]->variants == NULL) {
                gui_state = SPEED_MENU;
                show_speed_menu();
            } else {
                gui_state = VARIANT_MENU;
                show_variant_menu();
            }
            break;
        case VARIANT_MENU:
            // Set up variant
            gui_state = SPEED_MENU;
            show_speed_menu();
            break;
        case SPEED_MENU:
            gui_messagebox("Place Chip in Socket",
                           "Turn on external supply afterwards, if used.", &chip_icon);
            gui_state = DO_SOCKET;
            break;
        case DO_SOCKET:
            gui_state = DO_TEST;
            show_test_gui();
            start_the_ram_test();
            break;
        case DO_TEST:
            break;
        case TEST_RESULTS:
            // Quick retest to save time
            gui_state = DO_TEST;
            show_test_gui();
            start_the_ram_test();
            break;
        default:
            gui_state = MAIN_MENU;
            break;
    }
}

// Called when the user presses the back button
void button_back()
{
    switch (gui_state) {
        case MAIN_MENU:
            break;
        case VARIANT_MENU:
            gui_state = MAIN_MENU;
            show_main_menu();
            break;
        case SPEED_MENU:
            // Check if our selection has a variant
            if (chip_list[main_menu.sel_line]->variants == NULL) {
                gui_state = MAIN_MENU;
                show_main_menu();
            } else {
                gui_state = VARIANT_MENU;
                show_variant_menu();
            }
            break;
        case DO_SOCKET:
            gui_state = SPEED_MENU;
            show_speed_menu();
            break;
        case DO_TEST:
            break;
        case TEST_RESULTS:
            gui_state = SPEED_MENU;
            show_speed_menu();
            break;
        default:
            gui_state = MAIN_MENU;
            break;
    }
}

void do_buttons()
{
    static pin_debounce_t action_btn = {GPIO_QUAD_BTN, 0};
    static pin_debounce_t back_btn = {GPIO_BACK_BTN, 0};
    if (is_button_pushed(&action_btn)) button_action();
    if (is_button_pushed(&back_btn)) button_back();
}

void wheel_increment()
{
    if (gui_state == MAIN_MENU || gui_state == SPEED_MENU || gui_state == VARIANT_MENU) {
        gui_listbox(cur_menu, LIST_ACTION_DOWN);
    }
}

void wheel_decrement()
{
    if (gui_state == MAIN_MENU || gui_state == SPEED_MENU || gui_state == VARIANT_MENU) {
        gui_listbox(cur_menu, LIST_ACTION_UP);
    }
}

void do_encoder()
{
    static pin_debounce_t pin_a = {GPIO_QUAD_A, 0};
    static pin_debounce_t pin_b = {GPIO_QUAD_B, 0};
    static uint8_t wheel_state_old = 0;
    uint8_t st;
    uint8_t wheel_state;

    wheel_state = do_debounce(&pin_a) | (do_debounce(&pin_b) << 1);
    st = wheel_state | (wheel_state_old << 4);
    if (wheel_state != wheel_state_old) {
        // Present state, next state
        // 00 -> 01 clockwise
        // 10 -> 11 counterclockwise
        // 11 -> 10 clockwise
        // 01 -> 00 counterclockwise
        if ((st == 0x01) || (st == 0x32)) {
            wheel_increment();
        }
        if ((st == 0x23) || (st == 0x10)) {
            wheel_decrement();
        }
        wheel_state_old = wheel_state;
    }
}

void init_buttons_encoder()
{
    gpio_init(GPIO_QUAD_A);
    gpio_init(GPIO_QUAD_B);
    gpio_init(GPIO_QUAD_BTN);
    gpio_init(GPIO_BACK_BTN);
    gpio_set_dir(GPIO_QUAD_A, GPIO_IN);
    gpio_set_dir(GPIO_QUAD_B, GPIO_IN);
    gpio_set_dir(GPIO_QUAD_BTN, GPIO_IN);
    gpio_set_dir(GPIO_BACK_BTN, GPIO_IN);

}

int main() {
    uint offset;
    uint16_t addr;
    uint8_t db = 0;
    uint din = 0;
    int i, retval;

    // Increase core voltage slightly (default is 1.1V) to better handle overclock
    vreg_set_voltage(VREG_VOLTAGE_1_25);

    // USB Mass storage initialization. Untested.
    //board_init();
    //tud_init(BOARD_TUD_RHPORT);
    //stdio_init_all();

    // PLL->prim = 0x51000.

    // Setup uart comms
    //stdio_uart_init_full(uart0, 57600, 28, 29); // 28=tx, 29=rx actually runs at 115200 due to overclock
    //gpio_init(15);
    //gpio_set_dir(15, GPIO_OUT);
    //printf("Test.\n");

    psrand_init_seeds();

    gpio_init(GPIO_LED);
    gpio_set_dir(GPIO_LED, GPIO_OUT);
    gpio_put(GPIO_LED, 1);

    gpio_init(GPIO_POWER);
    power_off();

    // Set up second core
    queue_init(&call_queue, sizeof(queue_entry_t), 2);
    queue_init(&results_queue, sizeof(int32_t), 2);
    queue_init(&stat_cur_test, sizeof(int), 2);

    // Second core will wait for the call queue.
    multicore_launch_core1(core1_entry);

    // Init display
    st7789_init();

    setup_main_menu();
 //   gui_demo();
    show_main_menu();
    init_buttons_encoder();


// Testing
#if 0
    power_on();
    ram44256_setup_pio(5);
    sleep_ms(10);
    for (i=0; i < 100; i++) {
        ram44256_ram_read(i&7);
        ram44256_ram_write(i&7, 1);
        ram44256_ram_read(i&7);
        ram44256_ram_write(i&7, 0);
//gpio_put(GPIO_LED, marchb_test(8, 1));
    }
    while(1) {}
#endif

    while(1) {
        do_encoder();
        do_buttons();
        do_status();

        // usb mass storage handler. untested.
     //   tud_task();
    }

    while(1) {
//        printf("Begin march test.\n");
        retval = marchb_test(65536, 1);
//        printf("Rv: %d\n", retval);
    }

    return 0;
}
