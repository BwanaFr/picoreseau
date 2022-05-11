#ifndef __HDLC_RX_H__
#include "pico/stdlib.h"

extern uint rxEnablePin;

enum receiver_status {busy, done, timeout, bad_crc, frame_short};

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

/**
 * Enables the receiver of the transciever
 **/
static inline void enableReceiver(bool enable)
{
    gpio_put(rxEnablePin, !enable);
}

/**
 * Gets if the receiver is enabled
 **/
static inline bool isReceiverEnabled()
{
    return !gpio_get(rxEnablePin);
}


#endif