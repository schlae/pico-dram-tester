# Glossary of Terms Related to Vintage RAM Operation and PIO Programming

## Pico PIO Programming

### PIO - Programmable IO

PIO is a hardware subsystem unique to the RP2040 micro controller (used in the
Pico) that allows you to create custom digital interfaces using small,
programmable state machines.

### State Machine

The Pico 1 has eight PIO state machines. Think of a state machine as a
microprocessor with a small and very task-specific instruction set.
Each of these state machines can run independently from its siblings
and also the Pico's main ARM processor. The Pico 2 added four additional
state machines.

Each state machine has its own output and input FIFOs which it uses to
communicate with the main processor.

You can write state machine programs in raw assembly or - with the use of
libraries - higher level languages.

Each state machine can run at a different clock cycle speed.

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
it is used as an index into a table of arbitrary values. This allows
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

Optional sidesetting is enabled with the addition of the opt argument:

    .side_set 1 opt
    loop:
        set pins, 1
        set pins, 0 side 0
        jmp loop

#### .pio Files

Assembly programs are usually written as text files with a .pio extension.
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

### GPIO State Machine Assignment

Each state machine in a PIO block can control a set of GPIO pins. You assign pins thusly:

    sm_config_set_set_pins(&config, base_pin, count);
    sm_config_set_out_pins(&config, base_pin, count);
    sm_config_set_in_pins(&config, base_pin);
    sm_config_set_sideset_pins(&config, base_pin);

Collectively, these four functions define how any pins accessible to a given
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

### ramxxxx.pio files

All ram files in this project run similar - or even identical - assembly loops.
The loops are capable or performing either standard read cycle or a specific type
of write cycle - which might be described as the earliest possible 'late write'.

There is provison for fast page (CAS only) test loops, but many of the pio files
lack the c bindings to make use of this.

Reads or writes are differentiated by a c bind function argument. And have been coded
to execute in the same number of pio clock cycles.

The assembly loops include nop statements which can be used to introduce chip
specific delays. Ideally they will ensure that key stages on a read or
write cycle are being triggered 'just in spec', when compared to the minimum
operating timings specified in a chip's data sheet for appropriately rated
version.

### Tolerances

### Overview

Manfacturers would decide chip speeds at the end of a manufacturing run. ANd
 'bin' them into classes based on access time and potentially other key
 timings. This means that, say, a 150ns chip might still be able to scrape a
 pass on an 'just-in-spec' test at the next highest speed. It should not pass
 a test at two grades faster, however.

### 📊 DRAM Speed Binning Tolerances (1980s)

| Speed Grade | Nominal tRAC | Typical Tolerance | Timing Range (ns) | Notes                                |
|-------------|--------------|-------------------|--------------------|--------------------------------------|
| -15         | 150 ns       | ±15%              | 127.5–172.5 ns     | Common for 4164, 41256               |
| -12         | 120 ns       | ±10%              | 108–132 ns         | Used in faster 64K/256K DRAM         |
| -10         | 100 ns       | ±7%               | 93–107 ns          | Often binned from same wafers        |
| -8          | 80 ns        | ±5%               | 76–84 ns           | Premium or military-grade            |
| -7          | 70 ns        | ±5%               | 66.5–73.5 ns       | Rare, high-performance bins          |

#### Notes

- **tRAC**: Row Access Time — from RAS falling edge to valid data out
- Tolerances reflect manufacturer binning margins, not absolute electrical limits
- Faster bins often came from the same wafer as slower parts

### The Read Loop

The Basic Read Loop runs as follows:

#### Row and Column Address Select Pre-charging

The Read Cycle Begins When The Row and Column Address Select Pins are Pulled
High (after any preceding ram access operation was holding them low). They
must remain high for a specified amount of time before then can be re-used as
part of the next operation. This is known as pre-charing and the column and
row address select pins will have their own minimum pre-charge times -
referenced in the data sheet. Row and Column Address Select Precharge times
are known as tRP and tCPN respectively. The row address select pin is commonly
known as RAS and the column pin, CAS.

A delay may be required between raising and CAS and RAS being lowered later in
the cycle. This is known as tCRP. In practice, however, many chips specify a
zero value. But CAS and RAS may be raised at the same time, and this is optimal
for cases where tCRP is non-zero.

#### Row Address Selection

The ram chip will use the same address pins to specify which row and column holds
the data bit we wish to access. We can set the row address while we are waiting
for tRP to be met. Some chips require that we observe a delay between setting the
address and the next stage in the cycle. This is known as tASR, but you will find
that for some chips this value is zero and therefore of no consequence.
set. For chips with a non-zero tASR, it is in our interest to set the row address
immediately after raising RAS.

#### Row Address Strobing

With the address pins now holding the row address and tASR met, we can 'strobe'
this into address into the chip. As hinted at previously, we do this by
lowering the RAS pin. The starting and trailing edge of lowering of the RAS pin are
the starting points for a number of key timings. Their minimum values must be met
for normal operation of the chip.

As RAS starts to fall, tRC and tRWC start. As RAS approaches its low level, tRAS,
tAR, tCSH, tRCD and tRAC begin. The address pins must hold the row address for a
short time after RAS goes low so that the chip has enough time to read and store the
row value. This is known as tRAH. Once tRAH has elapsed, we are free to load the
column address.

#### Column Address Selection and Strobing

This occurs in much the same way that a row address is loaded into the chip.
tASC is the counterpart to tASR. As tCAH is to tRAH - in so much as it must
have elapsed before we can make any changes to the values of the address pins.
We can lower CAS once tRCD has reached its minimum value: tRCD(min). tRCD also
has a maximum value and this is significant in determining when the data pin
will contain valid data.

CAS reaching its low value signifies the start of tCAH and tCAC.

#### Valid Data Read

Once we have set both the row and column addresses into the chip, we must wait
a while for that addresses data bit to become available on the data out pin -
known as Q. The amount of time we must wait depends upon exactly when some of
preceding steps happened. If CAS was lowered before tRCD(max) we can access valid
data once tRAC is reached. If CAS was lowered after tRCD(max) we must wait until
tCAC is met. The fastest access time is only achievable with the former approach.

### The Write Loop

#### Late Write or Early Write?

An early write is one where the W pin is lowered before CAS is lowered within
a threshold know as tWCS. For some chips, tWCS is zero and in this scenrio,
if CAS and W are lowered at the same time. It is not clear if we are performing an early or
a late write - as W going low is not instantaneous and tWCS is measured from
when W reaches its low level. So it is possible that - with our common
test PIOs as they stand - that we are in late write mode with an indeterminate
value on Q.

Some chips have a small negative tWCS - which means that early write *can*
be achieved by lowering W and CAS at the same. So, depending on the specific
chip, our write cycle will be either Early Write or a very early Late Write!

It is assumed that the multiple write cycles can be safely performed
end-on-end at tRC intervals, rather than tRWC. The author is not 100% clear on
this point, but delays listed on the sheets in the timings directory currently
rely on this assumption. CAS and W going low at the same time can never result
in a valid Read-Write or Read-Modify-Write Cycle.

### The Write Cycle

This is very similar to the read cycle. The only differences are that:

- we set the input data bit (D) when we set the column address
- we also lower W when we lower CAS
- both read and write timings must be met if - as we do - we share much
of our write cycle code with our read cycle.
