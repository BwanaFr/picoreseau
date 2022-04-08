#ifndef __HDLC_RX_H__
#include "pico/stdlib.h"

enum receiver_status {idle, in_progress, check_crc, done, crc_error, aborted, error};

/**
 * Configures receiver
 **/
void configureReceiver();

/**
 * Starts the receiver state machine
 **/
void startReceiver();

/**
 * Gets receiver status
 * @return status as defined in receiver_status
 **/
receiver_status getReceiverStatus();

/**
 * Gets the RX buffer
 * @param len Lenght of the buffer
 * @return Pointer to buffer or NULL
 **/
const volatile uint8_t* getRxBuffer(uint32_t& len);

#endif