# Pico DRAM Tester

Having trouble with your old-school DIP dynamic RAM chips? Here's a simple
RAM tester that will give you a definitive pass or fail for a variety of
chips.

**This is an early beta release. The software is not completed. Feel free to
build a tester and try it out, and submit bug reports.**

![Photo of the board](images/pico-dram-tester-rev2b.jpg)

Benefits

* Tests RAM *at full speed* so you can feel confident about a chip that passes.
* Easy to construct with widely-available parts. No surface mount chips.
* Low cost
* Built-in DC-DC modules generate extra voltages required by 4116-class chips
* Can be operated from a bench supply for voltage margin testing

Supported Devices

* 4027 (4K x 1)
* 4108 (8K x 1 that are 4116s with only half test good: MK4108, TMS4108, etc)
* 4116 (16K x 1)
* 4816 (16K x 1)
* 4132 (32K x 1, piggyback)
* 4132 (32K x 1 that are 4164s with only half tested good: TMS4532, M3732)
* 4164 (64K x 1)
* 41128 (128K x 1, piggyback)
* 41256 (256K x 1)
* 4416 (16K x 4)
* 4464 (64K x 4)
* 44256 (256K x 4)

Out of scope

* SRAM, DRAM chips larger than 1MBit
* Logic chips

## Construction

Revision 2.2 adds a DC Barrel Jack option.  
It also adds solder jumper pads to allow use of the original Panasonic Encoder
or the common Generic Encoders.

[Fab package](fab/pico-dram-tester-rev22.zip)

[Schematic](pico-dram-tester.pdf)

[Bill of Materials](pico-dram-tester.csv)

The board is a simple 2-layer board, 100mm x 100mm, and 1.6mm thick. Fabricate
at your favorite vendor in any color you wish.

You may populate the board only with the sockets for devices you plan to use,
or you may populate all the sockets. You can try substituting the sockets with
standard IC sockets, but I recommend ZIF sockets which are much easier to use.
Why so many sockets? It saves you from having to install dozens of pin control
transistors required to make a tester work with a single universal socket.

Populate Q1, U7, U8, R2, R3, R4, and R5 if you would like to test 4116 and
4132 devices without requiring an external -5V and +12V power supply.

If you want to provide the -5V, +5V, and +12V power from your bench supply,
then you may leave those components off of the board and connect your power
leads to the -5V, +5V, and +12V test points. If you don't plan to test 4116
or 4132 chips, then you can safely leave out those components.

The LCD display is nominally the [Adafruit 4383](https://www.adafruit.com/product/4383) which is to be installed at location J1 (U9 is not required when using
this display). You can also use cheaper devices available from Aliexpress and
others. They are 8-pin LCD displays sold as 1.14" 135x240 IPS ST7789 modules.
Install this display at position J2. You may also need to add the voltage
monitor at position U9.

For several reasons, this board requires a Pico 2 microcontroller board and
will not work at all with the Pico 1.

After installing all the components and inspecting the board for accidental
short circuits, connect the Pico 2 via the USB connector to a computer. The
device should power up and appear as a USB mass storage device. Copy the
prebuilt firmware file [pmemtest.uf2](firmware/pmemtest.uf2) to this device, and the Pico 2 will
program itself. Reset the power and you should see a menu of chips on the LCD
screen.

## Using the DRAM Tester

Before starting, connect the tester to a power source:

* If you did not install U7 and U8, connect a bench supply to the GND, -5V, +5V, and +12V test points. Connect the Pico 2 USB jack to a power adapter or computer.
* If you installed U7 and U8, connect a 5V bench supply to the 5VIN and GND test points. Instead of a bench supply, you could connect the Pico 2 USB jack to a power adapter, but you won't be able to control the voltage. A computer often has a much lower voltage and you may fail RAM chips that actually function at 5V.

1. Install the chip in the socket designated for that type of memory chip, following the pin 1 orientation (the pin 1 notch faces the ZIF socket lever). Note that some of the sockets are used for several types of memory.
2. Rotate the selection knob until the correct part number is highlighted, and push down on the top of the selection knob (it's also a button!)
3. If you make a mistake, press the button to the left of the selection knob to go back.
4. After selecting a part, you need to pick the correct speed grade to run the
test. Often the speed grade is marked on the chip as a suffix. For example, -15
typically means 150ns. If you're not sure, pick the slowest speed.
5. If you are using an external bench power supply to supply -5V and +12V, turn it on now. If you soldered in the on-board voltage converter modules, you don't need to do anything. Push the top of the selection knob to continue.
6. The test runs. You will see a red "X" if the test fails or a green checkmark if the test passes.
7. You can rerun the test immediately by pushing down on the top of the selection knob. Or go back to the menu by pushing the button to the left.

Note: The visualization pane on the left is just for entertainment and doesn't
really represent bad bits.

## Technical Details

The Pico 2 microcontroller (RP2350) has built-in high-speed PIO processors
which are overclocked to 300MHz. This means that the DRAM timing can be set
to a resolution of 3.3ns. The RAM test runs on the second CPU core, decoupling
it from the GUI, which runs on the first CPU core.

RAM testing can be surprisingly complicated. There are many kinds of failures:

* Stuck-at fault - a cell is stuck at a 1 or a 0
* Stuck-open fault - a cell isn't connected (input or output)
* Transition fault - a cell can't go from one state to another (1 to 0 or 0 to 1)
* Data retention fault - a cell flips after a time delay
* Coupling fault - the state of one cell affects the state of another
* Neighborhood pattern sensitive fault - a specific pattern in nearby cells causes a cell to flip
* Address decoder fault - cells may be isolated, or multiple cells accessed at the same address, or multiple addresses may access a single cell

There are a number of "traditional" RAM tests such as the checkerboard or
walking 1/0, but these tests can be slow and don't have precise coverage over
all failure modes. The Pico DRAM Tester uses more modern testing algorithms:

* March-B. This is a sequence of linear reads and writes designed to catch address faults, stuck-at faults, transition faults, and coupling faults.
* Pseudorandom test. This test loads a pseudorandom number sequence into the memory, reads it back, and checks to make sure it didn't change. The test is repeated with 64 different pseudorandom patterns to enhance coverage. The patterns are identical between runs, making the test repeatable. The test only uses pseudorandom data and does not randomize the address. This test can detect many pattern-sensitive faults.
* Refresh test. This test loads a pattern into the memory, waits for a time delay, and then tries to read it back. The time delay is longer than a typical refresh rate. This test can detect data retention faults.

## Known Issues

* The 41128 test is not yet reliable.

## Troubleshooting

Check your solder connections. Does the Pico 2 board show up in bootloader mode when you plug it into a computer?

The Pico DRAM Tester uses an overclocked Pico 2 configuration.
Some Pico 2 boards may not work well at 300MHz, crashing or not drawing the GUI on the screen correctly.
You may be able to get it working by editing pmemtest.c, find the line `vreg_set_voltage(VREG_VOLTAGE_1_15);` and change the voltage setting to `VREG_VOLTAGE_1_20`.


## License

This work is licensed under a Creative Commons Attribution-ShareAlike 4.0
International License. See [https://creativecommons.org/licenses/by-sa/4.0/](https://creativecommons.org/licenses/by-sa/4.0/).