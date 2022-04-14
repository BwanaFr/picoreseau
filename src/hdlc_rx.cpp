#include "hdlc_rx.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/sync.h"

#include "hdlc_rx.pio.h"

#include <stdio.h>

PIO rxPIO = pio0;           //PIO block for data receipt
uint rxFlagSM = 0;          //Flag detection state machine number
uint rxDataSM = 0;          //Data reception state machine number
pio_sm_config rxDataSMCfg;
uint rxDataOffset = 0;
int rxDMAChannel = -1;      //Data reception DMA channel

uint rxEnablePin = 0;       //Tranceiver TX enable pin
critical_section_t crit_sec;

static volatile receiver_status __not_in_flash("hdlc_rx") rxStatus = idle;
static volatile uint8_t rxBuffer[65535];                                //Data reception buffer
static volatile uint32_t __not_in_flash("hdlc_rx") rxCount = 0;         //Number of byte(s) transfered with DMA
static volatile uint32_t __not_in_flash("hdlc_rx") crc[3] = {0,0,0};    //CRC at current byte, -1 ,2

/**
 * Interrupt service routine on PIO 0
 * Interrupt 0 is raised if an abort is found
 * Interrupt 1 is raised if TX is completed is found
 **/
void __isr __time_critical_func(pio0_isr)() {
    //IRQ 0 is raised when an abort is received by HDLC hunter
    if(pio_interrupt_get(rxPIO, 0)){
        pio_interrupt_clear(rxPIO, 0);
        //Abort received
        rxStatus = aborted;
    }
    //IRQ 1 is raised when the data RX state machine completed
    if(pio_interrupt_get(rxPIO, 1)){
        //Data received, ignore is we receive less than 4 bytes
        //HDLC frame is at least 4 bytes (address, control + 2 crc bytes)
        if(rxCount > 4){
            //We received enough data for a valid HDLC frame
            rxStatus = check_crc;
        }else if(rxCount > 0){
            //Not enough bytes received, re-arm transfer
            rxCount = 0;                    //Restart rx count
            dma_hw->sniff_data = 0xFFFF;    //Restart CRC
            dma_channel_set_write_addr(rxDMAChannel, rxBuffer, true);   //Start DMA transfer
        }
        pio_interrupt_clear(rxPIO, 1);
    }
}

/**
 * Interrupt when RX DMA transfer is completed
 **/
void __isr __time_critical_func(rx_dma_isr)() {
    if(dma_channel_get_irq1_status(rxDMAChannel)){
        dma_channel_acknowledge_irq1(rxDMAChannel);
        //Keep latest 2 CRC
        crc[2] = crc[1];
        crc[1] = crc[0];
        crc[0] = dma_hw->sniff_data;
        //Increment rx count
        ++rxCount;
        //Start another transfer
        dma_channel_start(rxDMAChannel);
    }
}

/**
 * Configures the RX DMA
 **/
void configureRXDMA() {
    //Get an available DMA channel once
    if(rxDMAChannel == -1){
        rxDMAChannel = dma_claim_unused_channel(true);
        dma_channel_set_irq1_enabled(rxDMAChannel, true);
        irq_add_shared_handler(DMA_IRQ_1, rx_dma_isr, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
        irq_set_enabled(DMA_IRQ_1, true);
    }
    //Configure the channel
    dma_channel_config c = dma_channel_get_default_config(rxDMAChannel);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);   //Increment address pointer of rxBuffer
    channel_config_set_dreq(&c, pio_get_dreq(rxPIO, rxDataSM, false));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);  //Transfer 8 bits
    channel_config_set_sniff_enable(&c, true);              //Enable sniffer to compute CRC
    dma_sniffer_enable(rxDMAChannel, 0x3, true);            // Turn on CRC-16/X-25
    dma_hw->sniff_ctrl |= 0x800;    //Out inverted (bitwise complement, XOR out)
    dma_hw->sniff_ctrl |= 0x400;    //Out bit-reversed
    dma_hw->sniff_data = 0xFFFF;    //Start with 0xFFFF
    dma_channel_configure(
        rxDMAChannel,
        &c,
        rxBuffer,                               // Destination pointer
        (io_rw_8*)&rxPIO->rxf[rxDataSM] + 3,    // Source pointer (get MSB)
        1,                                      // Number of transfers (one by one)
        false                                   // Don't start immediately
    );
}

void enableReceiver(bool enable)
{
    gpio_put(rxEnablePin, !enable);
}

/**
 * Configures receiver
 **/
void configureReceiver(uint rxEnPin, uint clkInPin, uint dataInPin)
{
    //configure transceiver enable GPIO
    rxEnablePin = rxEnPin;
    gpio_init(rxEnablePin);
    gpio_set_dir(rxEnablePin, GPIO_OUT);
    enableReceiver(false);    //Disable RX for now
    //Initialize critical section
    critical_section_init(&crit_sec);

    //HDLC flag hunter PIO configuration
    uint offset = 0;
    offset = pio_add_program(rxPIO, &hdlc_hunter_program);
    rxFlagSM = pio_claim_unused_sm(rxPIO, true);
    hdlc_hunter_program_init(rxPIO, rxFlagSM, offset, clkInPin, dataInPin);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_isr);                 //Set IRQ handler for new command
    irq_set_enabled(PIO0_IRQ_0, true);                               //Enable IRQ
    //HDLC RX PIO configuration
    rxDataOffset = pio_add_program(rxPIO, &hdlc_rx_program);
    rxDataSM = pio_claim_unused_sm(rxPIO, true);
    rxDataSMCfg = hdlc_rx_program_init(rxPIO, rxDataSM, rxDataOffset, dataInPin);
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt0, true);        //IRQ0 (abort received)
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, true);        //IRQ1 (TX completed)

    //Configure DMA channel to receive bytes
    configureRXDMA();
}

/**
 * Starts the receiver state machine
 **/
void __time_critical_func(startReceiver)()
{
    critical_section_enter_blocking(&crit_sec);
    rxCount = 0;                    //Restart rx count
    dma_hw->sniff_data = 0xFFFF;    //Restart CRC
    rxStatus = in_progress;
    dma_channel_set_write_addr(rxDMAChannel, rxBuffer, true);   //Start DMA transfer
    critical_section_exit(&crit_sec);
}

/**
 * Gets receiver status
 * @return status as defined in RX_ symbols
 **/
receiver_status getReceiverStatus()
{
    critical_section_enter_blocking(&crit_sec);
    if(rxStatus == check_crc){
        uint32_t crcCheck = crc[2];
        if((((crcCheck>>16) & 0xFF) == rxBuffer[rxCount-2]) &&
            (((crcCheck>>24) & 0xFF) == rxBuffer[rxCount-1])){
                rxStatus = done;
        }else{            
            rxStatus = crc_error;
        }
    }else if(rxStatus == aborted){
        dma_channel_abort(rxDMAChannel);
    }
    receiver_status ret = rxStatus;
    critical_section_exit(&crit_sec);
    return ret;
}

/**
 * Gets the RX buffer
 * @param len Lenght of the buffer
 * @return Pointer to buffer or NULL
 **/
const volatile uint8_t* getRxBuffer(uint32_t& len)
{
    len = rxCount;
    return rxBuffer;
}