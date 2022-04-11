#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.h"
#include "hdlc_tx.h"


static uint8_t testData[] = {0x0, 0xFF, 0x02};
static uint testDataLen = sizeof(testData);


/**
 * Application main entry
 **/
int main() {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    //Initialize RX state machines
    configureReceiver();
    //Initialize TX state machines
    configureEmitter();

    for(int i=0;i<30;++i){
        printf(".");
        sleep_ms(100);
    }
    printf("\n");

    printf("Enabling TX clock\n");
    /*setClock(true);
    sleep_ms(2);
    setClock(false);*/
    setClock(true);
    printf("Sending %u bytes", testDataLen);
    setDataEnabled(true);
    sleep_ms(2);
    sendData(testData, testDataLen);
    setDataEnabled(false);
    sleep_ms(2);
    setClock(false);
    printf(" done.\n");
    //Starts the receiver
    startReceiver();
    printf("Receiver started!\n");
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

    //Completed loop
    while(true){        
        sleep_ms(200);
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}