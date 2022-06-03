/*
 * This file contains USB task to be run on core 1
 */
#ifndef _USB_TASKS_H_
#define _USB_TASKS_H_

#include "picoreseau.hxx"

/**
 * USB commands to receive
 **/
enum USB_CMD{
    CMD_GET_STATUS,             // Gets device status
    CMD_GET_CONSIGNE,           // Gets the current consigne
    CMD_PUT_CONSIGNE,           // Sends a consigne
    CMD_GET_DATA,               // Receive data
    CMD_PUT_DATA,               // Send data
    CMD_DISCONNECT,             // Disconnect remote partner
};

/**
 * USB state machine
 **/
enum USB_STATE{
    IDLE,                   // Monitors USB for data RX
    SENDING_STATUS,         // Sends status to USB
    SENDING_CONSIGNE,       // Sends consigne to USB
    SENDING_DATA_HEADER,    // Sends data header to USB (before sending data chunk)
    SENDING_DATA,           // Sends data received from Nanoreseau to USB
    SENDING_DISCONNECT,     // Sends disconnect request
    RECEIVE_CONSIGNE,       // Receives consigne (or command) from USB
    RECEIVE_DATA,           // Received data to be sent to nanoreseau    
};

/**
 * Initializes the USB stack
 **/
void nr_usb_init();

/**
 * Runs USB tasks
 **/
void nr_usb_tasks();

/**
 * Updates the picoreseau state on USB
 * @param state Picoreseau state
 **/
void nr_usb_set_state(NR_STATE state);

/**
 * Updates the picoreseau error status on USB
 * @param error Error code
 * @param errMsg Error message as string format
 **/
void nr_usb_set_error(NR_ERROR error, const char* errMsg);

/**
 * Updates the actual consigne on USB
 * @param peer Address of the peer sending the consigne (initial call)
 * @param exchange_num Exchange number after the initial call
 * @param consigne Pointer to the received consigne
 **/
void nr_usb_set_consigne(uint8_t peer,  uint8_t exchange_num, const Consigne* consigne);

/**
 * Gets any USB pending command
 * @return false if no command is in queue
 **/
bool nr_usb_get_pending_command();
#endif