# RF433

A little project about 433 MHz transcievers !


The objective of this project is to learn about 433 MHz hacking and everything around it.

This repository will only host the code that we need to share between members of the project.

We're using :

- A Flipper Zero
- A PandwaRF

- Aruino UNO cards
- RX470C - V01 recievers
- WL102 transmitters
- MX-RM-5V recievers
- FS1000A transmitters

## Custom robust protocol (send/recv)

The sketches in `custom_protocol_send/` and `custom_protocol_recv/` now use a more interference‑resilient on‑off keying (OOK) protocol:

- Pulse‑width encoding per bit (no DC levels):
	- 0 => HIGH 1T, LOW 2T
	- 1 => HIGH 2T, LOW 1T
- Preamble: 24 cycles of HIGH 1T / LOW 1T to let the receiver AGC settle
- Sync: HIGH 3T / LOW 3T
- Frame: [LEN][SEQ][PAYLOAD...][CRC8], bytes sent MSB‑first
	- CRC8 polynomial 0x07, init 0x00
	- SEQ increments each second on the sender; the receiver de‑duplicates repeats using SEQ
- Redundancy: each frame is transmitted 3× with ~8 ms gap

Defaults (tunable in code):

- Base time unit T = 500 µs (1 kbps raw symbol rate)
- Preamble cycles = 24
- Repeats = 3

If you experience drops, try reducing the bit rate (increase `T_US` to 700–1200 µs) or increasing repeats.

Expected behavior: The sender transmits the payload byte `0x42` once per second. With the above protocol the receiver should report it reliably (typically 3 receptions are sent but only one is printed due to de‑duplication).
