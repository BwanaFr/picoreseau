#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.h"
#include "hdlc_tx.h"


static uint8_t testData[] = {0x0, 0xFF, 0x02};
static uint testDataLen = sizeof(testData);

// static uint8_t testData2[] = {0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0XF};
static uint8_t testData2[16];// = {0xF5, 0xF5, 0xF5, 0xF5,0xF5, 0xF5};
static uint testDataLen2 = sizeof(testData2);

void core1_entry() {
    //Initialize RX state machines
    configureReceiver();
    //Starts the receiver
    while(true){
        startReceiver();
        while(true){
            receiver_status status = getReceiverStatus();
            if(status == done){
                uint32_t size = 0;
                const volatile uint8_t* buffer = getRxBuffer(size);
                for(uint32_t i=0;i<size;++i){
                    printf("0x%02x ", buffer[i]);
                }
                printf("\n");
                break;
            }else if(status == crc_error){
                printf("BAD CRC!\n");
                uint32_t size = 0;
                const volatile uint8_t* buffer = getRxBuffer(size);
                for(uint32_t i=0;i<size;++i){
                    printf("0x%02x ", buffer[i]);
                }
                printf("\n");
                break;
            }else if(status == aborted){
                printf("Aborted!\n");
                break;
            }
        }
    }
}

/**
 * Application main entry
 **/
int main() {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    //Start core 1 for receiver
    // multicore_launch_core1(core1_entry);
    //Initialize TX state machines
    configureEmitter();

    for(int i=0;i<30;++i){
        printf(".");
        sleep_ms(100);
    }
    printf(" Sending %u bytes\n", testDataLen2);
    for(uint i=0;i<testDataLen2;++i){
        testData2[i] = (uint8_t)i;
    }
    setClock(true);
    sleep_ms(1);
    sendData(testData, testDataLen);
    // sleep_ms(1);
    sendData(testData2, testDataLen2);
    sleep_ms(1);
    setClock(false);
    

    //Completed loop
    while(true){        
        sleep_ms(200);
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}