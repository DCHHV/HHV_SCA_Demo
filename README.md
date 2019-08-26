# DEF CON Hardware Hacking Village Side Channel Attack Demo
This demo focuses on teaching and exploiting a simple timing attack vulnerable set up. It is a hardware device that consists of a PIC12F1572 (Enhanced Mid-range), 4 tactile buttons, LED indicators, an FTDI USB serial interface, and a connection for a logic analyzer (specifically a USBee AX PRO which the HHV has a small stock of). The demo is self-guided, that is, the serial interface provides instructions on each stage of the demo.


## About
The demo consists of 4 stages that become increasingly difficult. Each stage requires the user to identify a valid PIN for that stage. The PIN is a 4 digit, 4 value number; this provides 256 combinations. Due to the low complexity and lack of timeout penalty, brute forcing a PIN for each stage is reasonably possible but highly discouraged since the intent is to learn about side-channel attacks.

There are two LEDs, a red and a green, that flicker when the PIN is being input. The green LED turns on when a button is pressed, and off when it is released. Depending on the stage, once the full PIN is input, the red LED will flash if the pin was incorrect, or the green LED will turn solid for a few seconds if it was correct. The USB serial interface also has a red and green LED to indicate serial activity.

The digits of the PIN are connected to a single analog input of the PIC. Between the switches, a resistor ladder is set up. This allows for a consistent and unique analog voltage value for each digit. The analog input is polled at roughly 8 ms from the PIC (this timing has no bearing on the SCA demo or its vulnerabilities).

The red and green LEDs for the PIN input are connected to a 2x5 0.1" female header. A USBee AX PRO can be directly connected to this input. The LEDs are then connected to D0 and D1 of the logic analyzer. This logic analyzer is compatible with `sigrok`/`pulseview`, it is not a Saleae clone. Any other 3.3 V compatible logic analyzers can be used instead; it was highly convenient to use this pinout as the HHV has a stock of the USBee logic analyzers.

A full breakdown of each stage, how the vulnerability works, step by step instructions, and proper mitigation techniques for this specific setup, are available in `docs/writeup.md`

## Setup
There is a specific order to the following steps, this is due to `pulseview` recognizing the FTDI device and attempting to utilize it as a logic analyzer interface. Doing this in any other order is possible but it becomes a hassle as `pulseview` tries to consume the USB serial device, even if it is already open in some cases.

1. Connect the USBee AX PRO to the host computer
2. Open `pulseview`. The tool should automatically detect and initialize the logic analyzer, if not, it can be manually set up by scanning for 'fx2lafw' devices. Note that on Linux based systems, there is a separate firmware package for the 'fx2lafw' firmware files.
3. Connect the FTDI USB serial cable
4. Open a serial terminal to the just created USB terminal at 9600 baud, 8n1.
5. Press "RESET" button on the HHV SCA Demo board; text should appear on the terminal
