#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.pio.h"
#include "hdlc_tx.pio.h"

#define CLK_PIN 1
#define DATA_PIN 2

PIO rxPIO = pio0;           //PIO block for data receipt
PIO txPIO = pio1;           //PIO block for data emit

uint txClockSM = 0;         //Clock emit state machine number
uint txDataSM = 0;          //Data emit state machine number
uint rxFlagSM = 0;          //Flag detection state machine number
uint rxDataSM = 0;          //Data reception state machine number
pio_sm_config rxDataSMCfg;
uint rxDataOffset = 0;
int rxDMAChannel = -1;      //Data reception DMA channel

static volatile uint8_t rxBuffer[65535];    //Data reception buffer
static volatile uint32_t rxCount = 0;       //Number of byte(s) transfered with DMA

static uint8_t testData[] = {0x3, 0xFF, 0xFF, 0x1F, 0x12, 0x0, 0x0};
static uint testDataLen = sizeof(testData)-2;

void arm_rx_dma();

/**
 * Interrupt service routine on PIO 0
 * Interrupt 0 is raised if a flag is found
 * Interrupt 1 is raised if an abort is found
 **/
void __isr pio0_isr() {
    //IRQ is raised if a flag is received by HDLC hunter
    if(pio_interrupt_get(rxPIO, 0)){
        pio_interrupt_clear(rxPIO, 0);
        //int32_t done = sizeof(rxBuffer) - dma_channel_hw_addr(rxDMAChannel)->transfer_count;
        printf("F");
        /*if(done > 3){
            dma_channel_abort(rxDMAChannel);
        }*/
    }
    //IRQ 1 is raised when an abort is received by HDLC hunter
    if(pio_interrupt_get(rxPIO, 1)){
        pio_interrupt_clear(rxPIO, 1);
        printf("A");
    }
    //IRQ 2 is raised when the data RX state machine completed
    if(pio_interrupt_get(rxPIO, 2)){
        pio_interrupt_clear(rxPIO, 2);
        int32_t done = sizeof(rxBuffer) - dma_channel_hw_addr(rxDMAChannel)->transfer_count;
        if(done < 4){
            pio_sm_restart(rxPIO, rxDataSM);
            //Received less than 4 bytes, ignore and restart
            pio_sm_init(rxPIO, rxDataSM, rxDataOffset, &rxDataSMCfg);
            // Set the state machine running
            pio_sm_set_enabled(rxPIO, rxDataSM, true);
        }else{
            printf("\nD\n");
        }   
    }
}

/**
 * Arm the RX DMA
 **/
void arm_rx_dma() {
    dma_channel_config c = dma_channel_get_default_config(rxDMAChannel);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_sniff_enable(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(rxPIO, rxDataSM, false));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    dma_channel_configure(
        rxDMAChannel,
        &c,
        rxBuffer,                               // Destination pointer
        (io_rw_8*)&rxPIO->rxf[rxDataSM] + 3,    // Source pointer (get MSB)
        sizeof(rxBuffer),                       // Number of transfers
        true                                    // Start immediately
    );
}

void __isr dma_isr() {
    if(dma_channel_get_irq1_status(rxDMAChannel)){
        dma_channel_acknowledge_irq1(rxDMAChannel);
        printf("DMA done!\n");
    }
}

/**
 * Computes CRC using DMA sniffer
 **/
void add_crc(uint8_t* buffer, uint len){
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
        buffer,
        len,
        true    // Start immediately
    );

    dma_channel_wait_for_finish_blocking(sDMAChannel);
    dma_channel_unclaim(sDMAChannel);
    printf("CRC is 0x%02lx\n", (dma_hw->sniff_data>>16));   //Shift as out bit reversed is set
    buffer[len] = (dma_hw->sniff_data>>16) & 0xFF;
    buffer[len+1] = (dma_hw->sniff_data>>24) & 0xFF;
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
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt2, true);        //IRQ2

    //HDLC RX PIO configuration
    rxDataOffset = pio_add_program(rxPIO, &hdlc_rx_program);
    rxDataSM = pio_claim_unused_sm(rxPIO, true);
    rxDataSMCfg = hdlc_rx_program_init(rxPIO, rxDataSM, rxDataOffset, CLK_PIN, DATA_PIN);

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
    //Arm DMA to receive data
    arm_rx_dma();
    //Send clock without data, it sould send bunches of flags
    pio_sm_set_enabled(txPIO, txClockSM, true);
    sleep_ms(1);
    pio_sm_set_enabled(txPIO, txClockSM, false);
    while(!pio_sm_is_rx_fifo_empty(rxPIO, rxDataSM)){
        uint32_t rxByte = pio_sm_get_blocking (rxPIO, rxDataSM);
        printf("Read : 0x%02lx\n", (rxByte>>24));
    }
    printf("Sending %u bytes\n", testDataLen);
    add_crc(testData, testDataLen);
    //Push bytes in the TX machine
    for(uint i=0;i<testDataLen+2;++i){
        pio_sm_put_blocking(txPIO, txDataSM, testData[i]);        
    }
    
    //Enable the TX clock
    pio_sm_set_enabled(txPIO, txClockSM, true);
    sleep_ms(1);
    pio_sm_set_enabled(txPIO, txClockSM, false);
    
    /*while(!pio_sm_is_rx_fifo_empty(rxPIO, rxDataSM)){
        uint32_t rxByte = pio_sm_get_blocking (rxPIO, rxDataSM);
        printf("Read : 0x%02lx\n", (rxByte>>24));
    }*/
    //Completed loop
    while(true){        
        sleep_ms(200);
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}