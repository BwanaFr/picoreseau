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
 * @param error Error code
 * @param current_consigne Current consigne
 **/
void nr_usb_publish_state(NR_STATE state, NR_ERROR error, const Consigne* current_consigne);

/**
 * Gets any USB pending command
 * @return false if no command is in queue
 **/
bool nr_usb_get_pending_command();
#endif