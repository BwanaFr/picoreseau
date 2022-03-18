#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "hdlc_rx.pio.h"
#include "hdlc_tx.pio.h"

#define CLK_PIN 1
#define DATA_PIN 2

PIO rxPIO = pio0;
PIO txPIO = pio1;

uint txClockSM = 0;
uint txDataSM = 0;
uint rxFlagSM = 0;
uint rxDataSM = 0;

/**
 * Interrupt service routine on PIO 0
 * Interrupt 0 is raised if a flag is found
 * Interrupt 1 is raised if an abort is found
 **/
void __isr pio0_isr() {
    if(pio_interrupt_get(rxPIO, 0)){
        pio_interrupt_clear(rxPIO, 0);
        printf("\nFlag!\n");
    }else if(pio_interrupt_get(rxPIO, 1)){
        pio_interrupt_clear(rxPIO, 1);
        printf("A");
        pio_sm_restart(rxPIO, rxDataSM);
    }
}

/**
 * Application main entry
 **/
int main() {
    stdio_init_all();
    gpio_init(CLK_PIN);    
    gpio_set_dir(CLK_PIN, GPIO_OUT);
    gpio_init(DATA_PIN);
    gpio_set_dir(DATA_PIN, GPIO_OUT);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    //HDLC flag hunter PIO configuration
    uint offset = 0;
    offset = pio_add_program(rxPIO, &hdlc_hunter_program);
    rxFlagSM = pio_claim_unused_sm(rxPIO, true);
    hdlc_hunter_program_init(rxPIO, rxFlagSM, offset, CLK_PIN, DATA_PIN);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_isr);                 //Set IRQ handler for new command
    irq_set_enabled(PIO0_IRQ_0, true);                               //Enable IRQ
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt0, true);        //IRQ0
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, true);        //IRQ1
    
    //HDLC RX PIO configuration
    offset = pio_add_program(rxPIO, &hdlc_rx_program);
    rxDataSM = pio_claim_unused_sm(rxPIO, true);
    hdlc_rx_program_init(rxPIO, rxDataSM, offset, CLK_PIN, DATA_PIN);

    //HDLC TX clock configuration
    offset = pio_add_program(txPIO, &clock_tx_program);
    txClockSM = pio_claim_unused_sm(txPIO, true);
    clock_tx_program_init(txPIO, txClockSM, offset, CLK_PIN);
    
    //HDLC TX data configuration
    offset = pio_add_program(txPIO, &hdlc_tx_program);
    txDataSM = pio_claim_unused_sm(txPIO, true);
    hdlc_tx_program_init(txPIO, txDataSM, offset, DATA_PIN);


    for(int i=0;i<50;++i){
        printf(".");
        sleep_ms(100);
    }
    printf("\n");

    //Test HDLC flag hunter
    /*uint64_t data = 0b0001111111000000;
    for(int i=0;i<16;++i){
        bool dataBit = (data >> i) & 0x1;
        gpio_put(DATA_PIN, dataBit);
        sleep_us(1);
        gpio_put(CLK_PIN, true);
        sleep_us(1);
        gpio_put(CLK_PIN, false);
    }*/

    //Test HDLC rx
    /*uint8_t bytes[] = {0xFF, 0x2, 0x3};
    for(int i=0;i<3;++i){
        uint8_t data = bytes[i];
        uint oneCount = 0;
        printf("Sending : ");
        for(int j=0;j<8;++j){
            bool dataBit = (data >> j) & 0x1;
            if(dataBit){
                // Insert a zero if one count > 5
                if(++oneCount>5){
                    dataBit = false;
                    --j;
                }
            }
            if(!dataBit){
                oneCount = 0;
            }
            gpio_put(DATA_PIN, dataBit);
            // sleep_us(2);
            gpio_put(CLK_PIN, true);
            // sleep_us(2);
            gpio_put(CLK_PIN, false);
            printf("%d", dataBit ? 1 : 0);
        }
        printf("\nWrote : 0x%02x\n", data);
        if(!pio_sm_is_rx_fifo_empty(rxPIO, sm)){
            uint32_t rxByte = pio_sm_get_blocking (pio, sm);
            printf("Read : 0x%02lx\n", (rxByte>>24));
        }
    }*/

    while(!pio_sm_is_rx_fifo_empty(rxPIO, rxDataSM)){
        uint32_t rxByte = pio_sm_get_blocking (rxPIO, rxDataSM);
        printf("Before read : 0x%02lx\n", (rxByte>>24));
    }
    //Push bytes in the TX machine
    /*uint8_t bytes[] = {0x1, 0x2, 0x3};
    for(int i=0;i<3;++i){
        pio_sm_put_blocking(txPIO, txDataSM, bytes[i]);        
    }*/
    //Enable the TX clock
    pio_sm_set_enabled(txPIO, txClockSM, true);
    sleep_ms(20);
    pio_sm_set_enabled(txPIO, txClockSM, false);
    while(!pio_sm_is_rx_fifo_empty(rxPIO, rxDataSM)){
        uint32_t rxByte = pio_sm_get_blocking (rxPIO, rxDataSM);
        printf("Read : 0x%02lx\n", (rxByte>>24));
    }
    /*
    for(int i=0;i<100;++i){
        pio_sm_restart(txPIO, txDataSM);
        pio_sm_set_enabled(txPIO, txClockSM, true);
        sleep_ms(200);
        pio_sm_set_enabled(txPIO, txClockSM, false);
        sleep_ms(2000);
    }*/

    //Completed loop
    while(true){        
        sleep_ms(200);
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}