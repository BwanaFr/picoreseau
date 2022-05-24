#include "usb_tasks.hxx"
#include "tusb_config.h"
#include "tusb.h"
#include "usb/get_unique_serial.h"
#include "usb/cdc_uart.h"
#include "pico/sync.h"

#pragma pack (1)
typedef struct USB_DATA_OUT {
    uint8_t state;
    uint8_t error;
    Consigne consigne;
}USB_DATA_OUT;

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
static USB_DATA_OUT data_out;
static uint nb_write = 0;
static bool send_data = false;
auto_init_mutex(usb_mutex);

void nr_usb_init() {
    // Generate an unique serial ID string
    usb_serial_id_init();
    // Initializes the CDC uart for printf
    cdc_uart_init();
    // Initializes TinyUSB stack
    tusb_init();
}

void nr_usb_tasks() {
    tud_task();

    switch(usb_state){
        case IDLE:
            if(tud_vendor_available()){
                uint32_t r = tud_vendor_read(&data_in, 1);
                if(r == 1){
                    switch(data_in.cmd){
                        case CMD_PUT_CONSIGNE:
                            printf("USB: Got send consigne\n");
                            usb_state = RECEIVE_CONSIGNE;
                            break;
                        case CMD_GET_DATA:
                            printf("USB: Get data");
                            usb_state = RECEIVE_DATA;
                            break;
                        case CMD_PUT_DATA:
                            printf("USB: Put data");
                            usb_state = SENDING_DATA_HEADER;
                            break;
                        case CMD_DISCONNECT:
                            printf("Disconnect\n");
                            usb_state = SENDING_DISCONNECT;
                            break;
                    }
                }
            }
            break;
        case RECEIVE_CONSIGNE:
            if(tud_vendor_available()){
                uint32_t r = tud_vendor_read(&data_in.cmd_payload.consigne, sizeof(data_in.cmd_payload.consigne));
                printf("Read %lu/%u\n", r, data_in.cmd_payload.consigne.length);
                if(r>1){
                    if(data_in.cmd_payload.consigne.length == 
                        (sizeof(data_in.cmd_payload.consigne) - sizeof(data_in.cmd_payload.consigne.length))) {
                            printf("Received complete consigne!\n");
                            usb_state = IDLE;
                    }
                }
            }
            break;
    }
    /*mutex_enter_blocking(&usb_mutex);
    if(send_data){
        uint8_t* ptr = (uint8_t*)&data_out;
        ptr += nb_write;
        nb_write = tud_vendor_write(ptr, sizeof(USB_DATA_OUT)-nb_write);
        if(nb_write>= sizeof(USB_DATA_OUT)){
            printf("%lu ", tud_vendor_write_available());
            send_data = false;
        }
    }
    mutex_exit(&usb_mutex);*/
}

void nr_usb_publish_state(NR_STATE state, NR_ERROR error, const Consigne* current_consigne) {
    mutex_enter_blocking(&usb_mutex);
    data_out.state = state;
    data_out.error = error;
    memcpy(&data_out.consigne, current_consigne, sizeof(Consigne));
    nb_write = 0;    
    send_data = true;
    mutex_exit(&usb_mutex);
}