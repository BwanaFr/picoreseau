/*
 * This file contains USB task to be run on core 1
 */
#ifndef _USB_TASKS_H_
#define _USB_TASKS_H_

#ifdef __cplusplus
extern "C" {
#endif
/**
 * Initializes the USB stack
 **/
void nr_usb_init();

/**
 * Runs USB tasks
 **/
void nr_usb_tasks();
#ifdef __cplusplus
}
#endif
#endif