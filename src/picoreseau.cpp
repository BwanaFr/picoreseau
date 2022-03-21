#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.pio.h"
#include "hdlc_tx.pio.h"

#define CLK_PIN 1
#define DATA_PIN 2

PIO rxPIO = pio0;       //PIO block for data receipt
PIO txPIO = pio1;       //PIO block for data emit

uint txClockSM = 0;     //Clock emit state machine number
uint txDataSM = 0;      //Data emit state machine number
uint rxFlagSM = 0;      //Flag detection state machine number
uint rxDataSM = 0;      //Data reception state machine number
int rxDMAChannel = -1;  //Data reception DMA channel

static volatile uint8_t rxBuffer[65535];    //Data reception buffer
static volatile uint32_t rxCount = 0;       //Number of byte(s) transfered with DMA

static uint8_t testData[] = {0x0, 0xFF, 0x2, 0xDE, 0x34};

void arm_rx_dma();

/**
 * Interrupt service routine on PIO 0
 * Interrupt 0 is raised if a flag is found
 * Interrupt 1 is raised if an abort is found
 **/
void __isr pio0_isr() {
    if(pio_interrupt_get(rxPIO, 0)){
        pio_interrupt_clear(rxPIO, 0);
        printf("F");
        /*dma_channel_abort(rxDMAChannel);
        rxCount = 0;
        arm_rx_dma();*/
    }
    if(pio_interrupt_get(rxPIO, 1)){
        pio_interrupt_clear(rxPIO, 1);
        printf("A");
    }
}

/**
 * Arm the RX DMA
 **/
void arm_rx_dma() {
    dma_channel_config c = dma_channel_get_default_config(rxDMAChannel);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(rxPIO, rxDataSM, false));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    dma_channel_configure(
        rxDMAChannel,
        &c,
        rxBuffer + rxCount,                     // Destination pointer
        (io_rw_8*)&rxPIO->rxf[rxDataSM] + 3,    // Source pointer (get MSB)
        1,                                      // Number of transfers
        true                                    // Start immediately
    );
}

void __isr dma_isr() {
    if(dma_channel_get_irq1_status(rxDMAChannel)){
        dma_channel_acknowledge_irq1(rxDMAChannel);
        printf("DMA 0x%02x\n", rxBuffer[rxCount]);
        ++rxCount;        
        arm_rx_dma();
    }
}

/**
 * Computes CRC using DMA sniffer
 **/
void test_crc(){
    uint8_t dummy;
    uint8_t sDMAChannel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(sDMAChannel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_sniff_enable(&c, true);

    // Turn on CRC-16/X-25
    dma_sniffer_enable(sDMAChannel, 0x3, true);
    dma_hw->sniff_ctrl |= 0x800;    //Out inverted (bitwise complement, XOR out)
    dma_hw->sniff_ctrl |= 0x400;    //Out bit-reversed
    dma_hw->sniff_data = 0xFFFF;    //Start with 0xFFFF

    dma_channel_configure(
        sDMAChannel,
        &c,
        &dummy,
        testData,
        5,
        true    // Start immediately
    );

    dma_channel_wait_for_finish_blocking(sDMAChannel);
    dma_channel_unclaim(sDMAChannel);
    printf("CRC is 0x%02lx\n", (dma_hw->sniff_data>>16));   //Shift as out bit reversed is set
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

    //DMA
    rxDMAChannel = dma_claim_unused_channel(true);
    dma_channel_set_irq1_enabled(rxDMAChannel, true);
    irq_add_shared_handler(DMA_IRQ_1, dma_isr, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);

    for(int i=0;i<30;++i){
        printf(".");
        sleep_ms(100);
    }
    printf("\n");
    test_crc();
    return 0;

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
    uint8_t bytes[] = {0x1, 0x5, 0x3};
    for(int i=0;i<3;++i){
        pio_sm_put_blocking(txPIO, txDataSM, bytes[i]);        
    }
    //arm_rx_dma();
    //Enable the TX clock
    pio_sm_set_enabled(txPIO, txClockSM, true);
    sleep_ms(1);
    pio_sm_set_enabled(txPIO, txClockSM, false);
    arm_rx_dma();
    
    while(!pio_sm_is_rx_fifo_empty(rxPIO, rxDataSM)){
        io_rw_8 *rxfifo_shift = (io_rw_8*)&rxPIO->rxf[rxDataSM] + 3;
        printf("FIFO : 0x%02x\n", (char)*rxfifo_shift);
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