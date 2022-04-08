#ifndef __HDLC_TX_H__
#include "pico/stdlib.h"

/**
 * Configures emitter
 **/
void configureEmitter();

/**
 * Send clock on bus used to send echo
 * @param enabled True if the clock is enabled
 **/
void setClock(bool enabled);

/**
 * Send data to bus
 * @param buffer buffer to be sent, without CRC
 * @param len lenght of the buffer to be sent
 **/
void sendData(const uint8_t* buffer, uint32_t len);

#endif