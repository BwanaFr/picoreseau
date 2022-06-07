#include "usb_tasks.hxx"
#include "tusb_config.h"
#include "tusb.h"
#include "usb/get_unique_serial.h"
#include "usb/cdc_uart.h"
#include "pico/sync.h"
#include "pico/time.h"

#pragma pack (1)
typedef struct USB_STATUS_OUT {
    uint8_t state;              // Device state
    uint8_t error;              // Error code
    char errorMsg[60];          // Error message
}USB_STATUS_OUT;

#pragma pack (1)
typedef struct USB_CONSIGNE_OUT {
    uint8_t peer;               // Last peer identifier
    uint8_t exchange_num;       // Last exchange number
    Consigne consigne;          // Consigne data
}USB_CONSIGNE_OUT;

#pragma pack (1)
typedef struct USB_DATA_IN {
    uint8_t cmd;
    union cmd_payload
    {
        uint8_t address;    // Address to disconnect
        uint16_t rx_tx_len; // Lenght of data to send/receive
        Consigne consigne;  // Consigne to be received
    }cmd_payload;
}USB_DATA_IN;

static USB_STATE usb_state = IDLE;
static USB_DATA_IN data_in;
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
            uint32_t r = tud_vendor_read(&data_in, 1);
            if(r == 1){
                switch(data_in.cmd){
                    case CMD_GET_STATUS:
                        usb_state = SENDING_STATUS;
                        break;
                    case CMD_GET_CONSIGNE:
                        usb_state = SENDING_CONSIGNE;
                        break;
                    case CMD_PUT_CONSIGNE:
                        printf("USB: Got send consigne\n");
                        usb_state = RECEIVE_CONSIGNE;
                        break;
                    case CMD_PUT_DATA:
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

    switch(usb_state){
        case SENDING_STATUS:
            // Status requested by host
            mutex_enter_blocking(&usb_mutex);
            tud_vendor_write_all(&status_out, sizeof(USB_STATUS_OUT));            
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
            if(tud_vendor_available()){
                //Gets consigne lenght
                tud_vendor_read(&data_in.cmd_payload.consigne.length, sizeof(data_in.cmd_payload.consigne.length));                
                //Read consigne bytes
                uint32_t r = tud_vendor_read(&data_in.cmd_payload.consigne.dest, 
                                    sizeof(data_in.cmd_payload.consigne) - sizeof(data_in.cmd_payload.consigne.length));
                printf("Read %lu/%u\n", r, data_in.cmd_payload.consigne.length);
                if(r>1){
                    if(r == data_in.cmd_payload.consigne.length) {
                        printf("Received complete consigne!\n");
                        usb_state = IDLE;
                    }
                }
            }
            break;
        case RECEIVE_DATA:
            //Prepare to receive data on USB
            if(tud_vendor_available()){
                //Gets number of following bytes
                uint32_t lenRx = tud_vendor_read(&data_in.cmd_payload.rx_tx_len, sizeof(data_in.cmd_payload.rx_tx_len));
                printf("Will receive %u/%lu bytes\n", data_in.cmd_payload.rx_tx_len, lenRx);
                int32_t rx_len = data_in.cmd_payload.rx_tx_len;
                absolute_time_t start = get_absolute_time();
                while(rx_len>0){
                    if(tud_vendor_available()) {
                        uint32_t r = tud_vendor_read(usb_buffer, rx_len);
                        rx_len -= r;
                    }
                    tud_task();
                }
                int64_t elapsed_us = absolute_time_diff_us(start, get_absolute_time());
                double speed = (data_in.cmd_payload.rx_tx_len * (1000000.0/elapsed_us)) / 1024.0;
                printf("RX completed in %lldus (%fkB/s)\n", elapsed_us, speed);
                usb_state = IDLE;
            }
            break;
        case SENDING_DISCONNECT:
            uint32_t lenRx = tud_vendor_read(&data_in.cmd_payload.address, sizeof(data_in.cmd_payload.address));
            printf("Disconnecting %u\n", data_in.cmd_payload.address);
            
            usb_state = IDLE;
            break;
        default:
            usb_state = IDLE;
            break;
    }
}

void nr_usb_set_state(NR_STATE state) {
    mutex_enter_blocking(&usb_mutex);
    status_out.state = state;
    mutex_exit(&usb_mutex);
}

void nr_usb_set_error(NR_ERROR error, const char* errMsg) {
    mutex_enter_blocking(&usb_mutex);
    status_out.error = error;
    memset(status_out.errorMsg, 0, sizeof(status_out.errorMsg));
    strlcpy(status_out.errorMsg, errMsg, sizeof(status_out.errorMsg));
    mutex_exit(&usb_mutex);
}

void nr_usb_set_consigne(uint8_t peer,  uint8_t exchange_num, const Consigne* consigne) {
    mutex_enter_blocking(&usb_mutex);
    consigne_out.peer = peer;
    consigne_out.exchange_num = exchange_num;
    memcpy(&consigne_out.consigne, consigne, sizeof(consigne_out.consigne));
    mutex_exit(&usb_mutex);
}