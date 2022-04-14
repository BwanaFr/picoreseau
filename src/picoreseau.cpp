#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.h"
#include "hdlc_tx.h"
#include "pico/time.h"

repeating_timer_t timer;

#define CLK_RX_PIN 0                //Clock data in GPIO
#define RX_TRCV_ENABLE_PIN  1       //Receiver transceiver enable GPIO
#define CLK_TX_PIN 2                //Clock data out GPIO
#define TX_TRCV_ENABLE_PIN 3        //Emit transceiver enable GPIO
#define DATA_RX_PIN 4               //Data in GPIO
#define DATA_TX_PIN 5               //Data out GPIO

bool blink_callback(repeating_timer_t *rt) {
    gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    return true;
}

void core1_entry() {
    printf("Hello from core 1!\n");
    //Initialize RX state machines
    configureReceiver(RX_TRCV_ENABLE_PIN, CLK_RX_PIN, DATA_RX_PIN);
    enableReceiver(true);
    //Starts the receiver
    while(true){
        receiver_status status = getReceiverStatus();
        startReceiver();
        while(true){
            receiver_status newStatus = getReceiverStatus();
            if(status != newStatus){                
                status = newStatus;
                bool exit_loop = false;
                switch(status){
                    case done:
                    {
                        uint32_t size = 0;
                        const volatile uint8_t* buffer = getRxBuffer(size);
                        printf("Data :");
                        for(uint32_t i=0;i<size;++i){
                            printf("0x%02x ", buffer[i]);
                        }
                        printf("\n");
                        exit_loop = true;
                        break;
                    }
                    case crc_error:
                    {
                        printf("BAD CRC! ");
                        uint32_t size = 0;
                        const volatile uint8_t* buffer = getRxBuffer(size);
                        for(uint32_t i=0;i<size;++i){
                            printf("0x%02x ", buffer[i]);
                        }
                        printf("\n");
                        exit_loop = true;
                        break;
                    }
                    case aborted:
                    {
                        printf("Aborted!\n");
                        exit_loop = true;
                        break;
                    }
                    case in_progress:
                    {
                        printf("In progress\n");                        
                        break;
                    }
                    case check_crc:
                    {
                        printf("CRC check\n");
                        break;
                    }
                    case error:
                    {
                        printf("Error\n");
                        break;
                    }
                    case idle:
                    {
                        printf("Idle\n");
                        break;
                    }
                }
                if(exit_loop){
                    break;
                }
            }else{
                tight_loop_contents();
                sleep_us(2);
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
    add_repeating_timer_ms(1000, blink_callback, NULL, &timer);

    for(int i=0;i<30;++i){
        printf(".");
        sleep_ms(100);
    }
    printf("\n");
    //configureEmitter(TX_TRCV_ENABLE_PIN, CLK_TX_PIN, DATA_TX_PIN);
    core1_entry();
    //Start core 1 for receiver
    ///multicore_launch_core1(core1_entry);
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
        sleep_ms(200);
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}