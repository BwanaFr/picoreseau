#include "hdlc_tx.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_tx.pio.h"

#include <stdio.h>

uint txEnablePin = 0;
PIO txPIO = pio1;           //PIO block for data emit
uint txClockSM = 0;         //Clock emit state machine number
uint txDataSM = 0;          //Data emit state machine number

static volatile bool dataActive = false;
static volatile bool flagSent = false;

void __isr pio1_isr()
{
    //Interrupt handler when flag is completed
    //Re-enable the clock if needed
    flagSent = true;
    gpio_put(txEnablePin, dataActive);
    pio_interrupt_clear(txPIO, 0);
}

/**
 * Configures emitter
 **/
void configureEmitter(uint txEnPin, uint clkTxPin, uint dataTxPin)
{
    txEnablePin = txEnPin;
    //Clock enable output
    gpio_init(txEnablePin);
    gpio_set_dir(txEnablePin, GPIO_OUT);
    gpio_put(txEnablePin, false);

    //HDLC TX clock configuration
    uint offset = pio_add_program(txPIO, &clock_tx_program);
    txClockSM = pio_claim_unused_sm(txPIO, true);
    clock_tx_program_init(txPIO, txClockSM, offset, clkTxPin, txEnablePin);

    //HDLC TX data configuration
    offset = pio_add_program(txPIO, &hdlc_tx_program);
    txDataSM = pio_claim_unused_sm(txPIO, true);
    hdlc_tx_program_init(txPIO, txDataSM, offset, dataTxPin, txEnPin);

    //Configure interrupts
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_isr);                 //Set IRQ handler for flag send
    irq_set_enabled(PIO1_IRQ_0, true);                               //Enable IRQ
    pio_set_irq0_source_enabled(txPIO, pis_interrupt0, true);        //IRQ0 (flag sent)*/
}

/**
 * Send clock on bus used to send echo
 * @param enabled True if the clock is enabled
 **/
void setClock(bool enabled)
{
    if(enabled){
        dataActive = true;
        gpio_put(txEnablePin, true); 
    }else{
        dataActive = false;
        while(gpio_get(txEnablePin)){
            tight_loop_contents();
        }
    }
}

/**
 * Send data to bus
 * @param buffer buffer to be sent, without CRC
 * @param len lenght of the buffer to be sent
 **/
void sendData(const uint8_t* buffer, uint len)
{
    //Prepare a DMA transfer
    uint8_t sDMAChannel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(sDMAChannel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(txPIO, txDataSM, true));
    channel_config_set_sniff_enable(&c, true);

    // Turn on CRC-16/X-25
    dma_sniffer_enable(sDMAChannel, 0x3, true);
    dma_hw->sniff_ctrl |= 0x800;    //Out inverted (bitwise complement, XOR out)
    dma_hw->sniff_ctrl |= 0x400;    //Out bit-reversed
    dma_hw->sniff_data = 0xFFFF;    //Start with 0xFFFF

    //Configure and start DMA
    dma_channel_configure(
        sDMAChannel,
        &c,
        &txPIO->txf[txDataSM],
        buffer,
        len,
        true    // Start immediately
    );    
    dma_channel_wait_for_finish_blocking(sDMAChannel);
    dma_channel_unclaim(sDMAChannel);
    //Send CRC
    flagSent = false;
    pio_sm_put_blocking(txPIO, txDataSM, ((dma_hw->sniff_data>>16) & 0xFF));
    pio_sm_put_blocking(txPIO, txDataSM, ((dma_hw->sniff_data>>24) & 0xFF));
    while(!flagSent){
        tight_loop_contents();
    }
    /*flagSent = false;
    while(!flagSent){
        tight_loop_contents();
    }*/
}
