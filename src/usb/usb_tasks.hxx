/*
 * This file contains USB task to be run on core 1
 */
#ifndef _USB_TASKS_H_
#define _USB_TASKS_H_

#include "picoreseau.hxx"

enum USB_CMD{
    SEND_INITIAL_CALL,
    SEND_CONSIGNE,
    RECEIVE_DATA,
    SEND_DATA,
    DISCONNECT,
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
 * Updates the picoreseau state
 * on USB
 * @param state Picoreseau state
 * @param current_consigne Current consigne
 **/
void nr_usb_publish_state(NR_STATE state, const Consigne* current_consigne);

/**
 * Gets any USB pending command
 * @return false if no command is in queue
 **/
bool nr_usb_get_pending_command();
#endif