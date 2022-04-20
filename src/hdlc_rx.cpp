#include "hdlc_rx.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/sync.h"

#include "hdlc_rx.pio.h"

#include <stdio.h>

PIO rxPIO = pio0;           //PIO block for data receipt
uint rxDataSM = 0;          //Data reception state machine number
pio_sm_config rxDataSMCfg;
uint rxDataOffset = 0;
int rxDMAChannel = -1;      //Data reception DMA channel

uint rxEnablePin = 0;       //Tranceiver TX enable pin
critical_section_t crit_sec;

typedef struct RXBuffer {
    uint8_t buffer[65535];
    uint32_t rxCount = 0;
    uint32_t crc[3];
}RXBuffer;

static volatile RXBuffer __not_in_flash("hdlc_rx") buffers[2];
static volatile uint32_t currentBuffer = 0;

static volatile bool rxCompleted = false;
static volatile bool rxAborted = false;

/**
 * Interrupt service routine on PIO 0
 * Interrupt 0 is raised if an abort is found
 * Interrupt 1 is raised if TX is completed is found
 **/
void __isr __time_critical_func(pio0_isr)() {
    //IRQ 0 is raised when a HDLC abort is received
    if(pio_interrupt_get(rxPIO, 0)){
        pio_interrupt_clear(rxPIO, 0);
        //Abort received
        rxAborted = true;
    }
    //IRQ 1 is raised when a HDLC flag is recieved
    if(pio_interrupt_get(rxPIO, 1)){
        pio_interrupt_clear(rxPIO, 1);
        //Disable this IRQ (one shot IRQ)
        pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, false);
        if(buffers[currentBuffer].rxCount > 0){
            rxCompleted = true;
            //Got some bytes, swap buffer and start DMA
            currentBuffer = (!currentBuffer) & 0x1;
            buffers[currentBuffer].rxCount = 0;
            dma_hw->sniff_data = 0xFFFF;    //Start with 0xFFFF
            dma_channel_set_write_addr(rxDMAChannel, buffers[currentBuffer].buffer, true);   //Start DMA transfer
        }
    }
}

/**
 * Interrupt when RX DMA transfer is completed
 **/
void __isr __time_critical_func(rx_dma_isr)() {
    if(dma_channel_get_irq1_status(rxDMAChannel)){
        dma_channel_acknowledge_irq1(rxDMAChannel);
        if(buffers[currentBuffer].rxCount == 0){
            //Enable the PIO interrupt 0 to get notified when transfer is done
            pio_interrupt_clear(rxPIO, 1);
            pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, true);
        }

        //Keep latest 2 CRC
        buffers[currentBuffer].crc[2] = buffers[currentBuffer].crc[1];
        buffers[currentBuffer].crc[1] = buffers[currentBuffer].crc[0];
        buffers[currentBuffer].crc[0] = dma_hw->sniff_data;
        //Increment rx count
        ++buffers[currentBuffer].rxCount;        
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
        buffers[currentBuffer].buffer,          // Destination pointer
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
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_isr);                 //Set IRQ handler for new command
    irq_set_enabled(PIO0_IRQ_0, true);                               //Enable IRQ
    //HDLC RX PIO configuration
    rxDataOffset = pio_add_program(rxPIO, &hdlc_rx_program);
    rxDataSM = pio_claim_unused_sm(rxPIO, true);
    rxDataSMCfg = hdlc_rx_program_init(rxPIO, rxDataSM, rxDataOffset, dataInPin);
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt0, true);        //IRQ0 (abort received)
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, false);       //IRQ1 (TX completed)

    //Configure DMA channel to receive bytes
    configureRXDMA();
}

/**
 * Starts the receiver state machine
 **/
void __time_critical_func(startReceiver)()
{
    critical_section_enter_blocking(&crit_sec);
    buffers[currentBuffer].rxCount = 0;         //Restart rx count
    dma_hw->sniff_data = 0xFFFF;                //Restart CRC
    dma_channel_set_write_addr(rxDMAChannel, buffers[currentBuffer].buffer, true);   //Start DMA transfer
    critical_section_exit(&crit_sec);
}

/**
 * Gets receiver status
 * @return status as defined in RX_ symbols
 **/
receiver_status getReceiverStatus()
{   
    receiver_status ret = in_progress;
    uint32_t previousBuffer = 0;
    bool rxDone = false;
    critical_section_enter_blocking(&crit_sec);
    previousBuffer = !currentBuffer & 0x1;
    rxDone = rxCompleted;
    rxCompleted = false;
    critical_section_exit(&crit_sec);

    if(rxDone){
        //Data received, ignore is we receive less than 4 bytes
        //HDLC frame is at least 4 bytes (address, control + 2 crc bytes)       
        if(buffers[previousBuffer].rxCount > 4){
            //We received enough data for a valid HDLC frame
            uint32_t crcCheck = buffers[previousBuffer].crc[2];
            if((((crcCheck>>16) & 0xFF) == buffers[previousBuffer].buffer[buffers[previousBuffer].rxCount-2]) &&
                (((crcCheck>>24) & 0xFF) == buffers[previousBuffer].buffer[buffers[previousBuffer].rxCount-1])){
                    ret = done;
            }else{            
                ret = crc_error;
            }
        }
    }
    return ret;
}

/**
 * Gets the RX buffer
 * @param len Lenght of the buffer
 * @return Pointer to buffer or NULL
 **/
const volatile uint8_t* getRxBuffer(uint32_t& len)
{
    uint32_t previousBuffer = 0;
    critical_section_enter_blocking(&crit_sec);
    previousBuffer = !currentBuffer & 0x1;
    critical_section_exit(&crit_sec);
    len = buffers[previousBuffer].rxCount;
    return buffers[previousBuffer].buffer;
}
