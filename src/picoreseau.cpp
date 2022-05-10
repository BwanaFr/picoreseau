#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.h"
#include "hdlc_tx.h"
#include "clock_detect.h"
#include "pico/time.h"

repeating_timer_t timer;

#define DATA_RX_PIN 0
#define CLK_RX_PIN 1
#define RX_TRCV_ENABLE_PIN  2       //Receiver transceiver enable GPIO

#define DATA_TX_PIN 3
#define CLK_TX_PIN 4
#define TX_TRCV_ENABLE_PIN 5        //Emit transceiver enable GPIO

uint8_t buffer[65535];

bool blink_callback(repeating_timer_t *rt) {
    gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    return true;
}


/**
 * Application main entry
 **/
int main() {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    //Disable power-save of buck converter
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, true);

   // add_repeating_timer_ms(1000, blink_callback, NULL, &timer);



    for(int i=0;i<30;++i){
        printf(".");
        sleep_ms(100);
    }
    printf("\n");
    //initialize_clock_detect();
    
    //Initialize TX state machine
    //configureEmitter(TX_TRCV_ENABLE_PIN, CLK_TX_PIN, DATA_TX_PIN);
    gpio_init(TX_TRCV_ENABLE_PIN);
    gpio_set_dir(TX_TRCV_ENABLE_PIN, GPIO_OUT);
    gpio_put(TX_TRCV_ENABLE_PIN, false);
    //Initialize RX state machine
    configureReceiver(RX_TRCV_ENABLE_PIN, CLK_RX_PIN, DATA_RX_PIN);
    gpio_put(RX_TRCV_ENABLE_PIN, false);

    absolute_time_t pTime = make_timeout_time_ms(500);
    while(true){
        uint32_t nbBytes = 0;
        receiver_status status = receiveData(0x00, buffer, sizeof(buffer), nbBytes);
        if(status == done){
            printf("Received :");
            for(uint32_t i=0;i<nbBytes;++i){
                printf("0x%02x ", buffer[i]);
            }
            printf("\n");
        }else if(status == bad_crc){
            printf("BAD CRC\n");
        }else if(absolute_time_diff_us(pTime, get_absolute_time())>0){
            pTime = make_timeout_time_ms(500);
            printf(".");
        }
    }

    //configureEmitter(TX_TRCV_ENABLE_PIN, CLK_TX_PIN, DATA_TX_PIN);
    
    /*
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
    */

    //Completed loop
    while(true){        
        sleep_ms(2000);
        printf(".");
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}