# rx63nprog
A simple tool to flash Renesas RX63n MCUs via its Boot Mode interface.

Reference: [RX63N Group, RX631 Group User's Manual: Hardware](https://www.renesas.com/en-us/doc/products/mpumcu/doc/rx_family/r01uh0041ej0180_rx63n631.pdf?key=8c59560b326ffcba50495fec2fa58da2), Section 47.8 Boot Mode

## Building
Run make against the Makefile. If the build is successful, `rx63nprog` should be created.

## Usage
`./rx63nprog <device> <firmware image>`

Where `device` is the device's node in `/dev` and `firmware image` is the firmware image in Intel HEX format.
