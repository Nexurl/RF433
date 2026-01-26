# RF433

A little project about 433 MHz transcievers !


The objective of this project is to learn about 433 MHz hacking and everything around it.

This repository will only host the code that we need to share between members of the project.

## Hardware used

- A Flipper Zero
- A PandwaRF

- Aruino UNO cards
- RX470C - V01 recievers
- WL102 transmitters
- MX-RM-5V recievers
- FS1000A transmitters

## Repository layout

- `custom_protocol/` : Custom protocol implementations (multiple versions).
	- `custom_protocol_tx_v1/` : Transmitter v1 sketch (`custom_protocol_tx_v1.ino`). Early prototype.
	- `custom_protocol_tx_v2/` : Transmitter v2 sketch (`custom_protocol_tx_v2.ino`). Improved encoding/CRC handling for RH_ASK-like protocol.
	- `custom_protocol_rx_v1/` : Receiver v1 sketch (`custom_protocol_rx_v1.ino`). Basic RX logic.
	- `custom_protocol_rx_v2/` : Receiver v2 sketch (`custom_protocol_rx_v2.ino`). Matches v2 transmitter and uses symbol decoding and CRC validation.
	- `custom_protocol_tx_v3/` : Transmitter v3 (`custom_protocol_tx_v3.ino`). Rolling-code experiment (TX side), simple checksum and rolling-code injection.
	- `custom_protocol_rx_v3/` : Receiver v3 (`custom_protocol_rx_v3.ino`). Rolling-code experiment (RX side), extracts and validates rolling code and checksum.
	- `custom_protocol_transceiver/` : Example transceiver code (both TX and RX in one sketch) where applicable.
	- `custom_protocol_transceiver_rolling` : Bidirectional rolling code (Work in progress)

- `log_raw_timings/` : Sketch for logging raw timings from a receiver to aid protocol reverse-engineering.
	- `log_raw_timings.ino`

- `Logger/` : Misc logger-related sketches.
	- `key_logger/` : `key_logger.ino` — keypress logger example.

- `RCSwitch/` : Examples and ports of the RCSwitch-style sketches for common remote protocols.
	- `recieve_telec/` and `recieve_telec_adv/` : receiver variants
	- `send_telec/` : transmitter example

- `SimpleRcScanner/` : `SimpleRcScanner.ino` — a simple scanner to capture repeating patterns.

- `tests/` : Small Arduino sketches used for testing specific behaviours.
	- `test_433_bidirectional/` : bidirectional test
	- `test_433_BRUT/`, `test_433_BRUT_HEX/` : displays raw data from reciever
	- `test_433_read/`, `test_433_read2/` : recievers using the RH_ASK library
	- `test_433_send/` : transmitter using the RH_ASK library
	- `test_SD/` : SD card test

## Key files and purpose (quick reference)

- `custom_protocol/custom_protocol_tx_v2/custom_protocol_tx_v2.ino` :
	- TX implementation using 4-to-6 bit symbol encoding, preamble/start symbol, and a checksum/CRC scheme. Implements timer-driven bit banging and symbol output.

- `custom_protocol/custom_protocol_rx_v2/custom_protocol_rx_v2.ino` :
	- RX implementation compatible with the v2 transmitter. Uses a simple PLL-style sampling (ramp), integrates 8 samples/bit, decodes symbols to bytes, collects a buffer and validates with checksum/CRC.

- `custom_protocol/custom_protocol_tx_v3/custom_protocol_tx_v3.ino` :
	- v3 transmitter experiment that injects a 4-byte rolling code into each frame and appends a simple checksum byte. Includes a clearer `setup()` and debug prints.

- `custom_protocol/custom_protocol_rx_v3/custom_protocol_rx_v3.ino` :
	- v3 receiver experiment that extracts the rolling code, checks it increments (simple anti-replay), and validates the simple checksum.

- `log_raw_timings/log_raw_timings.ino` :
	- Useful for dumping raw pulse timings to help analyze unknown protocols.

- `RCSwitch/*` :
	- Various ports and examples of remote-control protocols; handy for interoperability testing with cheap 433MHz remotes.

## Notes on protocols and checksums

- The project contains several iterations of a custom protocol inspired by RadioHead's RH_ASK. v1/v2 used CCITT CRC approaches and 4-to-6 symbol encoding; during development a simple checksum (sum modulo 256) was used for easier debugging.
- v3 introduces a 4-byte rolling code embedded in the message to experiment with anti-replay. The current implementation is a prototype and **not** secure — if you need real security, consider using proper authenticated encryption.

## How to build / test

- Open the desired `.ino` file in the Arduino IDE and select the correct board (Arduino Uno) and serial port.
- Upload TX and RX sketches to two separate Arduinos and use the Serial monitor (115200 baud) for debug logging.