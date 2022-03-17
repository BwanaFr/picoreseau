#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "hdlc_rx.pio.h"

#define CLK_PIN 20
#define DATA_PIN 2

PIO pio = pio0;

void __isr pio0_isr() {
    if(pio_interrupt_get(pio, 0)){
        pio_interrupt_clear(pio, 0);
        printf("\nFlag!\n");
    }else if(pio_interrupt_get(pio, 1)){
        pio_interrupt_clear(pio, 1);
        printf("\nAbort!\n");
    }
}

int main() {
    stdio_init_all();
    gpio_init(CLK_PIN);    
    gpio_set_dir(CLK_PIN, GPIO_OUT);
    gpio_init(DATA_PIN);
    gpio_set_dir(DATA_PIN, GPIO_OUT);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    //HDLC flag hunter PIO configuration
    uint offset = pio_add_program(pio, &hdlc_hunter_program);
    uint sm = pio_claim_unused_sm(pio, true);
    hdlc_hunter_program_init(pio, sm, offset, CLK_PIN, DATA_PIN);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_isr);                 //Set IRQ handler for new command
    irq_set_enabled(PIO0_IRQ_0, true);                              //Enable IRQ
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);         //IRQ0
    pio_set_irq0_source_enabled(pio, pis_interrupt1, true);         //IRQ1

    //HDLC RX PIO configuration
    offset = pio_add_program(pio, &hdlc_rx_program);
    sm = pio_claim_unused_sm(pio, true);
    hdlc_rx_program_init(pio, sm, offset, CLK_PIN, DATA_PIN);

    //Test HDLC flag hunter
    uint64_t data = 0b0001111111000000;
    gpio_put(CLK_PIN, false);
    sleep_ms(5000);
    for(int i=0;i<16;++i){
        bool dataBit = (data >> i) & 0x1;
        gpio_put(DATA_PIN, dataBit);
        sleep_us(1);
        gpio_put(CLK_PIN, true);
        sleep_us(1);
        gpio_put(CLK_PIN, false);
    }

    while(true){        
        sleep_ms(200);
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}