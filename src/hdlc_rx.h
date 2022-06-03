#ifndef __HDLC_RX_H__
#define __HDLC_RX_H__
#include "pico/stdlib.h"

extern uint rxEnablePin;

enum receiver_status {busy, done, time_out, bad_crc, frame_short};

/**
 * Configures receiver
 **/
void configureHDLCReceiver(uint rxEnablePin, uint clkInPin, uint dataInPin);

/**
 * Receives data, blocks until done
 * @param address Receiver address
 * @param buffer Buffer to store data
 * @param bulLen Maximum length of buffer
 * @param rcvLen Effective bytes received
 * @param timeout Timeout, if 0 no timeout is made
 * @return receiver_status Status of the receiver
 **/
receiver_status receiveHDLCData(uint8_t address, uint8_t* buffer, uint32_t bufLen, uint32_t& rcvLen, uint64_t timeout=0);

/**
 * Enables the receiver of the transciever
 **/
static inline void enableHDLCReceiver(bool enable)
{
    gpio_put(rxEnablePin, !enable);
}

/**
 * Gets if the receiver is enabled
 **/
static inline bool isHDLCReceiverEnabled()
{
    return !gpio_get(rxEnablePin);
}

/**
 * Resets the receiver internal state
 **/
void resetReceiverState();


#endif