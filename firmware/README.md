# Glossary of Terms Related to Vintage RAM Operation and PIO Programming

[[toc]]

## Pico PIO Programming

### PIO - Programable IO

PIO is a hardware subsystem unique to the RP2040 microcontroller (used in the
Pico) that allows you to create custom digital interfaces using small,
programmable state machines.

### State Machine

The Pico 1 has eight PIO state machines. Think of a state machine as a
microprocessor with a small and very task-specific instruction set.
Each of these state machines can run independantly from its siblings
and also the Pico's main ARM proceessor. The Pico 2 added four additional
state machines.

Each state machine has its own output an input FIFOs which it uses to
comminicate with the main processor.

You can write state machine programs in raw assembly or - with the use of
libraries - higher level languages.

Each state machine can run  at a different clock cycle speed.

### FIFOs

Each PIO state machine has two First-In, First-Out (FIFO) buffers:

| FIFO        | Direction         | Purpose                                 |
|-------------|-------------------|-----------------------------------------|
| **TX FIFO** | CPU → PIO         | Send data from your program to the PIO  |
| **RX FIFO** | PIO → CPU         | Receive data from the PIO to your program |

These buffers are 4 words deep (each word is 32 bits), and they decouple the CPU
and PIO timing — letting each run independently.

In our assembly, we can grab data out of these (or push it in) in a few
different ways. The FIFO data is always transferred to/from the state
machine's respective shift registers:

| FIFO        | Role in PIO Program              | Interaction with Shift Registers          | CPU Interaction                                |
|-------------|----------------------------------|-------------------------------------------|------------------------------------------------|
| **TX FIFO** | Supplies data to the PIO         | Data is pulled into OSR via `PULL`        | CPU writes data using `sm.put()` or equivalent |
| **RX FIFO** | Receives data from the PIO       | Data is pushed from ISR via `PUSH`        | CPU reads data using `sm.get()` or equivalent  |
| **OSR**     | Output Shift Register            | Holds data to be shifted out via `OUT`    | Loaded from TX FIFO or via `MOV`               |
| **ISR**     | Input Shift Register             | Accumulates data via `IN` or `MOV`        | Pushed to RX FIFO when full or on command      |

### PIO Assembly

#### Command Format

The assembly language has 8 commands. Each command is 16 bits wide and takes
exactly one clock cycle to run in its most basic form. The 16 bit command is
broken down into the following sections:

    15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    [  OP  ] [       OPERANDS      ] [DELAY||SIDES]

Depending on how the state machine is configured, bits 04 to 00 can be used for
a split of delay and sideset directives from all delay (default) to all sides.

#### Commands

| Mnemonic | Opcode (bin) | Purpose                          | Operand Bits (12–5) Usage                                  |
|----------|--------------|----------------------------------|------------------------------------------------------------|
| `JMP`    | `000`        | Conditional jump                 | [Condition (3 bits)] + [Address (5 bits)]                  |
| `WAIT`   | `001`        | Wait for pin/IRQ/level           | [Polarity (1)] + [Source (2)] + [Index (5)]                |
| `IN`     | `010`        | Shift bits into ISR              | [Source (3)] + [Bit count (5)]                             |
| `OUT`    | `011`        | Shift bits out from OSR          | [Destination (3)] + [Bit count (5)]                        |
| `PUSH`   | `100`        | Push ISR to RX FIFO              | [If full (1)] + [Block (1)] + [Reserved (5)]               |
| `PULL`   | `101`        | Pull from TX FIFO to OSR         | [If empty (1)] + [Block (1)] + [Reserved (5)]              |
| `MOV`    | `110`        | Move between registers           | [Destination (3)] + [Source (3)]                           |
| `IRQ`    | `111`        | Trigger or wait on IRQ           | [Clear/Wait/Set (2)] + [IRQ index (6)]                     |

#### Delays

Delays are specified in assembly by a number in square brackets e.g.

    SET PINS, 1 [5]

Delay values, by default, represent an exact number of additional clock cycles.
So the line above would take six cycles complete. If you are using sidesets,
you will have a reduced number of delay values at your disposal - as delays and
sides share the same bit range. We can however patch each delay number so that
it is uesed as an index into a table of arbitraty values. This allows
us to use the same PIO program with different sets of timings. You will
see that the author of this project did just that! So we can use the same
program to test ram a chip family at all of its manufactured speed ratings!

#### Sidesets

Sidesets are optional bits in a PIO instruction that allow you to set GPIO pin
states in parallel with executing the instruction. You can toggle pins with
sidesets while performing other operations - like JMP, MOV, or PUSH.

The benefit of this feature is that you can save a precious command line or
unwanted clock cycle.

Sidesets are applied at a fixed point in the instruction cycle, ensuring
precise timing. You can define 0–5 bits for sideset use when assembling
the PIO program. You can choose to have sidesetting mandated for every
command - or only when an Enable Bit (which uses up one of bits 00-04)
is set on specific instructions.

Mandatory sidesetting is enabled with the '.side_set N' directive and every command must
have a sideset:

    .side_set 1
    loop:
        set pins, 1 side 1
        set pins, 0 side 0
        jmp loop side 1

Optional sidesetting is enabled with the additon of the opt argument:

    .side_set 1 opt
    loop:
        set pins, 1
        set pins, 0 side 0
        jmp loop

#### .pio Files

Assembler proghrammes are usually written as text files with a .pio extension.
These files are then assembled with the RP2040 SDK or MicroPython. In this
project the pio files also contain c bindings and wrapper functions for the
PIO code. The pio files are converted into header files that can be included
in the main program. i.e blink.pio would be assembled into blink.pio.h.

A structure pointer '\<name\>_program' is automatically generated
by the assembler. The .program directive dictates the name e.g.
'.program blink' in assembly will cause blink_program to be
generated for use in the c-sdk code. The following table describes
this and additional 'convention' elements.

| Element                  | Format / Naming Convention              | Description                                      |
|--------------------------|------------------------------------------|--------------------------------------------------|
| Program name in `.pio`   | `.program <name>`                        | Declares the name used for all bindings          |
| Instruction array        | `<name>_program_instructions[]`          | Encoded 16-bit instructions                      |
| Program descriptor       | `const struct pio_program <name>_program`| Main variable used with `pio_add_program()`      |
| Default config function  | `<name>_program_get_default_config()`    | Returns a pre-filled `pio_sm_config`             |

### PIO Block

The twelve state machines are split between three containers knowns as PIO
Blocks. A GPIO pin can only be assigned to the state machines in
one of these blocks at any given moment. GPIO pins are not
completely heterogenous and some are better suited to different tasks or
for use by a specific PIO block:

The choice of which PI block to use for which pins (or the task at hand) should
be made accordingly.

PIO Blocks have their own instruction memory. All state machines in a
given block share their PIO Block's instruction memory - which can store only 32
commands in total.

### GPIO State Machine Assignmemt

Each state machine in a PIO block can control a set of GPIO pins. You assign pins thusly:

    sm_config_set_set_pins(&config, base_pin, count);
    sm_config_set_out_pins(&config, base_pin, count);
    sm_config_set_in_pins(&config, base_pin);
    sm_config_set_sideset_pins(&config, base_pin);

Collectively, these four functions define how any pins acessible to a given
state machine are affected by all available instructions. Each instruction
type interacts with their pins differently:

| Instruction | Purpose                        | Pin Assignment Function             | Behavior                                                                 |
|-------------|--------------------------------|-------------------------------------|--------------------------------------------------------------------------|
| `SET`       | Set pin(s) to 0 or 1           | `sm_config_set_set_pins()`          | Sets all assigned pins to the same value (not bitwise)                   |
| `OUT`       | Output bits from OSR to pins   | `sm_config_set_out_pins()`          | Shifts bits from OSR to assigned pins (bit 0 → base pin, etc.)           |
| `IN`        | Input bits from pins to ISR    | `sm_config_set_in_pins()`           | Samples bits from assigned pins into ISR (bit 0 ← base pin, etc.)        |
| `SIDES`     | Side-set bits for parallel pin control | `sm_config_set_sideset_pins()` | Applies side-set bits to assigned pins alongside main instruction        |
| `MOV`       | Move data between registers    | *(not pin-related)*                 | Used for register manipulation, not GPIO                                 |

- **Base pin** is the lowest-numbered GPIO in the group.
- **OUT/IN** use bitwise mapping: bit 0 maps to base pin, bit 1 to base+1, etc.
- **SET** applies the same value to all assigned pins.
- **SIDES** is limited to 0–5 bits and runs in parallel with the main instruction.
- You must also configure pin directions using `pio_sm_set_consecutive_pindirs()` or `pio_gpio_init()`.

## DRAM Operations
