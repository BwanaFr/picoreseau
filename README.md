# picoreseau
A nanoreseau USB adapter based on raspberry pico.

Modules to be implemented:
- Clock detector to detect when bus is free (PIO + interrupts)
- HDLC TX/RX to send/receive data (PIO + DMA)
- CRC-16 verification
- USB endpoints to exchange with outside (TinyUSB)
