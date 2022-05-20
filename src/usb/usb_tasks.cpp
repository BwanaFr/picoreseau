#include "usb_tasks.hxx"
#include "tusb_config.h"
#include "tusb.h"
#include "usb/get_unique_serial.h"
#include "usb/cdc_uart.h"
#include "pico/sync.h"

#pragma pack (1)
typedef struct USB_DATA_OUT {
    uint8_t state;
    Consigne consigne;
    Consigne consigne2;
}USB_DATA_OUT;

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
    mutex_enter_blocking(&usb_mutex);
    if(send_data){
        uint8_t* ptr = (uint8_t*)&data_out;
        ptr += nb_write;
        nb_write = tud_vendor_write(ptr, sizeof(USB_DATA_OUT)-nb_write);
        if(nb_write>= sizeof(USB_DATA_OUT)){
            printf("%lu ", tud_vendor_write_available());
            send_data = false;
        }
    }
    mutex_exit(&usb_mutex);
}

void nr_usb_publish_state(NR_STATE state, const Consigne* current_consigne) {
    mutex_enter_blocking(&usb_mutex);
    data_out.state = state;
    memcpy(&data_out.consigne, current_consigne, sizeof(Consigne));
    nb_write = 0;    
    send_data = true;
    mutex_exit(&usb_mutex);
}