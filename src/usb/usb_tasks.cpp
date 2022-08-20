#include "usb_tasks.hxx"
#include "tusb_config.h"
#include "tusb.h"
#include "usb/get_unique_serial.h"
#include "usb/cdc_uart.h"
#include "pico/sync.h"
#include "pico/time.h"

#define VENDOR_REQUEST_RESET 1

#pragma pack (1)
typedef struct USB_STATUS_OUT {
    uint8_t state;              // Device state
    uint8_t error;              // Error code
    uint8_t event;              // Event signal
    char errorMsg[60];          // Error message
}USB_STATUS_OUT;

#pragma pack (1)
typedef struct USB_CONSIGNE_OUT {
    uint8_t peer;               // Last peer identifier
    Consigne consigne;          // Consigne data
}USB_CONSIGNE_OUT;

static USB_STATE usb_state = IDLE;
static USB_STATUS_OUT status_out;
static USB_CONSIGNE_OUT consigne_out;
auto_init_mutex(usb_mutex);
uint8_t usb_buffer[65535];

void nr_usb_init() {
    // Generate an unique serial ID string
    usb_serial_id_init();
    // Initializes the CDC uart for printf
    cdc_uart_init();
    // Initializes TinyUSB stack
    tusb_init();
    // Initializes structures
    memset(&status_out, 0, sizeof(status_out));
    memset(&consigne_out, 0, sizeof(consigne_out));
    strcpy(status_out.errorMsg, "No error");
}

/**
 * Writes all bytes to vendor endpoint
 * @param buffer Buffer containing bytes to write
 * @param bufsize Buffer size
 **/
void tud_vendor_write_all(void const* buffer, uint32_t bufsize) {
    uint8_t const* buf = (uint8_t const*)buffer;
    while(bufsize > 0){
        uint32_t wrote = tud_vendor_write(buf, bufsize);
        bufsize -= wrote;
        buf += wrote;
    }
}

void nr_usb_tasks() {
    //TODO: Check if USB is mounted using tud_vendor_mounted
    tud_task();

    if(usb_state == IDLE){
        // Check is a command is received
        if(tud_vendor_available()){
            // Reads command received on USB
            uint8_t cmd = 0;
            uint32_t r = tud_vendor_read(&cmd, sizeof(cmd));
            if(r == sizeof(cmd)){
                switch(cmd){
                    case CMD_GET_STATUS:
                        // Host wants to know device status
                        usb_state = SENDING_STATUS;
                        break;
                    case CMD_GET_CONSIGNE:
                        // Host wants to get current consigne
                        usb_state = SENDING_CONSIGNE;
                        break;
                    case CMD_PUT_CONSIGNE:
                        // Host sends consigne to a peer
                        usb_state = RECEIVE_CONSIGNE;
                        break;
                    case CMD_PUT_DATA:
                        // Host sends binary data to a peer
                        printf("USB: Put data\n");
                        usb_state = RECEIVE_DATA;
                        break;
                    case CMD_GET_DATA:
                        printf("USB: Get data\n");
                        usb_state = SENDING_DATA_HEADER;
                        break;
                    case CMD_DISCONNECT:
                        usb_state = SENDING_DISCONNECT;
                        break;
                    default:
                        // Here, we lost sync.
                        printf("Unsupported command!\n");
                }
            }
        }else{
            //No command received yet
            return;
        }
    }
    uint32_t lenRx = 0;
    switch(usb_state){
        case SENDING_STATUS:
            // Status requested by host
            mutex_enter_blocking(&usb_mutex);
            tud_vendor_write_all(&status_out, sizeof(USB_STATUS_OUT));
            // Resets the event 
            status_out.event = EVT_NONE;    
            mutex_exit(&usb_mutex);
            usb_state = IDLE;
            break;
        case SENDING_CONSIGNE:
            // Actual consigne requested by host
            mutex_enter_blocking(&usb_mutex);
            tud_vendor_write_all(&consigne_out, sizeof(USB_CONSIGNE_OUT));            
            mutex_exit(&usb_mutex);
            usb_state = IDLE;
            set_nr_state(NR_IDLE);
            break;
        case RECEIVE_CONSIGNE:
            // Consigne sent by host
            if(tud_vendor_available()){
                Consigne rxConsigne;
                memset(&rxConsigne, 0, sizeof(rxConsigne));
                //Gets consigne length and dest
                tud_vendor_read(&rxConsigne.length, sizeof(rxConsigne.length) + sizeof(rxConsigne.dest));
                //Read consigne bytes
                uint32_t r = tud_vendor_read(&rxConsigne.data, rxConsigne.length);
                //Send consigne to nanoreseau
                request_nr_consigne(&rxConsigne);
            }
            usb_state = IDLE;
            break;
        case RECEIVE_DATA:
            //Prepare to receive data on USB
            if(tud_vendor_available()){
                //Gets number of following bytes
                uint16_t rx_tx_len = 0;
                lenRx = tud_vendor_read(&rx_tx_len, sizeof(rx_tx_len));
                printf("Will receive %u/%lu bytes\n", rx_tx_len, lenRx);
                int32_t rx_len = rx_tx_len;
                absolute_time_t start = get_absolute_time();
                while(rx_len>0){
                    if(tud_vendor_available()) {
                        uint32_t r = tud_vendor_read(usb_buffer, rx_len);
                        rx_len -= r;
                    }
                    tud_task();
                }
                int64_t elapsed_us = absolute_time_diff_us(start, get_absolute_time());
                double speed = (rx_tx_len * (1000000.0/elapsed_us)) / 1024.0;
                printf("RX completed in %lldus (%fkB/s)\n", elapsed_us, speed);
                usb_state = IDLE;
            }
            break;
        case SENDING_DISCONNECT:
            uint8_t cmd; //Peer address to disconnect
            lenRx = tud_vendor_read(&cmd, sizeof(cmd));
            printf("Disconnecting %u\n", cmd);
            request_nr_disconnect(cmd);
            usb_state = IDLE;
            break;
        default:
            usb_state = IDLE;
            break;
    }
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  switch (request->bmRequestType_bit.type)
  {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest)
      {
        case VENDOR_REQUEST_RESET:
            printf("Reseting...\n");
            usb_state = IDLE;
            //TODO : Maybe reset other state machines (PIO...)
            return true;            
      }
  }
  return false;
}

void nr_usb_set_state(NR_STATE state) {
    mutex_enter_blocking(&usb_mutex);
    status_out.state = state;
    mutex_exit(&usb_mutex);
}

void nr_usb_set_error(NR_ERROR error, const char* errMsg) {
    mutex_enter_blocking(&usb_mutex);
    status_out.error = error;
    status_out.event = EVT_ERROR;
    memset(status_out.errorMsg, 0, sizeof(status_out.errorMsg));
    strlcpy(status_out.errorMsg, errMsg, sizeof(status_out.errorMsg));
    mutex_exit(&usb_mutex);
}

void nr_usb_set_consigne(uint8_t peer, const Consigne* consigne) {
    mutex_enter_blocking(&usb_mutex);
    status_out.event = EVT_SELECTED;
    consigne_out.peer = peer;
    memcpy(&consigne_out.consigne, consigne, sizeof(consigne_out.consigne));
    mutex_exit(&usb_mutex);
}

void nr_usb_set_cmd_done() {
    mutex_enter_blocking(&usb_mutex);
    status_out.event = EVT_CMD_DONE;
    mutex_exit(&usb_mutex);
}