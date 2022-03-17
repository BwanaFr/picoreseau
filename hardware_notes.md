# Notes on hardware constraints

- Clock input must be a PWM_B channel in order to measure the clock presence


# Prototype pinout
- Clock input (RX) -> GP1   : Clock received from RS485 driver (always active)
- Data input (RX) -> GP2    : Data received from RS485 driver (always active)
- Clock output (TX) -> GP3  : Clock send to RS485 driver (activated by another output)
- Clock output enable -> GP4: Enable the RS485 clock driver to transmit
- Data output (TX) -> GP5   : Data send to RS485 driver (activated by output bellow)
- Data output enable -> GP6 : Enable the RS485 data driver to transmit
