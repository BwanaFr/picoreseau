#ifndef __HDLC_RX_H__
#include "pico/stdlib.h"

enum receiver_status {busy, done, timeout, bad_crc};

/**
 * Configures receiver
 **/
void configureReceiver(uint rxEnablePin, uint clkInPin, uint dataInPin);

/**
 * Receives data, blocks until done
 * @param address Receiver address
 * @param buffer Buffer to store data
 * @param bulLen Maximum length of buffer
 * @param rcvLen Effective bytes received
 * @return receiver_status Status of the receiver
 **/
receiver_status receiveData(uint8_t address, uint8_t* buffer, uint32_t bufLen, uint32_t& rcvLen);


#endif