#include "hdlc_rx.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "pico/sync.h"
#include "pico/time.h"

#include "hdlc_rx.pio.h"

#include <stdio.h>

PIO rxPIO = pio0;               // PIO block for data receipt
uint rxDataSM = 0;              // Data reception state machine number
int rxDMAChannel = -1;          // Data reception DMA channel

uint rxEnablePin = 0;           // Tranceiver TX enable pin

uint8_t* rxBuffer = nullptr;    // RX buffer
uint32_t rxBufferMaxLen = 0;    // Maximum length of buffer 
uint8_t rcvAddress = 0;         // Our expected address on the bus

uint8_t destAddress = 0xFF;     // Address read on frame
static volatile uint16_t dma_crc[3] = {0, 0, 0};    // CRC from DMA sniffer at current byte, -1 ,2
static volatile uint8_t data_crc[2] = {0,0};        // Last two bytes of received data for CRC computation
static volatile uint32_t rxCount = 0;               // Number of received bytes
static volatile bool rxCompleted = false;           // RX completed
static volatile bool skipData = false;              // Skip this frame
static bool firstUse = true;                        // First use of the receiveData function
static absolute_time_t timeOut = 0;                 // Timeout timestamp

static uint8_t tmp = 0;

#define USE_ABORT

static inline void prepareRx()
{
    rxCompleted = false;
    skipData = false;
    rxCount = 0;
    pio_sm_clear_fifos(rxPIO, rxDataSM);
    // Starts DMA
    dma_sniffer_enable(rxDMAChannel, 0x3, true);            // Turn on CRC-16/X-25
    dma_hw->sniff_ctrl |= 0x800;    //Out inverted (bitwise complement, XOR out)
    dma_hw->sniff_ctrl |= 0x400;    //Out bit-reversed
    dma_hw->sniff_data = 0xFFFF;    //Start with 0xFFFF
    dma_channel_set_write_addr(rxDMAChannel, &destAddress, true);   //Start DMA transfer
}

/**
 * Interrupt service routine on PIO 0
 * Interrupt 0 is raised if an abort is found
 * Interrupt 1 is raised if TX is completed is found
 **/
void __isr __time_critical_func(pio0_isr)() {
#ifdef USE_ABORT
    //IRQ 0 is raised when a HDLC abort is received
    if(pio_interrupt_get(rxPIO, 0)){
        pio_interrupt_clear(rxPIO, 0);
        pio_set_irq0_source_enabled(rxPIO, pis_interrupt0, false);
        if(!rxCompleted) {
            //Prepare for another transfer
            prepareRx();
        }
    }
#endif
    //IRQ 1 is raised when a HDLC flag is recieved
    if(pio_interrupt_get(rxPIO, 1)) {
        pio_interrupt_clear(rxPIO, 1);
        //Disable this IRQ (one shot IRQ)
        pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, false);
        if(skipData){
            //Prepare for another transfer
            prepareRx();
        }else if(rxCount > 0){
            rxCompleted = true;            
        }
    }
    gpio_put(PICO_DEFAULT_LED_PIN, false);
}

/**
 * Interrupt when RX DMA transfer is completed
 **/
void __isr __time_critical_func(rx_dma_isr)() {
    if(dma_channel_get_irq1_status(rxDMAChannel)){
        dma_channel_acknowledge_irq1(rxDMAChannel);
        //Keep latest 3 CRC
        dma_crc[2] = dma_crc[1];
        dma_crc[1] = dma_crc[0];
        dma_crc[0] = (dma_hw->sniff_data >> 16) & 0xFFFF;
        //First transfer done on destAddress
        if(dma_channel_hw_addr(rxDMAChannel)->write_addr == (uintptr_t)&destAddress){
            //Enable the PIO interrupt 0 to get notified when transfer is done
#ifdef USE_ABORT
            pio_interrupt_clear(rxPIO, 0);
            pio_set_irq0_source_enabled(rxPIO, pis_interrupt0, true);
#endif
            pio_interrupt_clear(rxPIO, 1);
            pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, true);
            data_crc[1] = destAddress;
            if(destAddress != rcvAddress){
                //Address don't match, skip all data
                skipData = true;
            }else{
                gpio_put(PICO_DEFAULT_LED_PIN, true);
                skipData = false;
                rxCount = 0;
                //Receive data in real buffer
                dma_channel_set_write_addr(rxDMAChannel, rxBuffer, true);
            }
        }else if(skipData){
            // Continue to clear the FIFO using DMA
            dma_channel_set_write_addr(rxDMAChannel, nullptr, true);
        }else if(rxCount < rxBufferMaxLen) {
            data_crc[0] = data_crc[1];
            data_crc[1] = rxBuffer[rxCount];
            ++rxCount;
            if(rxCount < rxBufferMaxLen) {
                //Start another transfer (buffer is big enough)
                dma_channel_set_write_addr(rxDMAChannel, &rxBuffer[rxCount], true);
            }else{
                //Transfer to CRC buffer
                dma_channel_set_write_addr(rxDMAChannel, &tmp, true);
            }
        }else if(dma_channel_hw_addr(rxDMAChannel)->write_addr == (uintptr_t)&tmp){
            ++rxCount;
            data_crc[0] = data_crc[1];
            data_crc[1] = tmp;
            dma_channel_set_write_addr(rxDMAChannel, &tmp, true);
        }
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
    channel_config_set_write_increment(&c, false);   //Increment address pointer of rxBuffer
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
        &destAddress,                           // Destination pointer
        (io_rw_8*)&rxPIO->rxf[rxDataSM] + 3,    // Source pointer (get MSB)
        1,                                      // Number of transfers (one by one)
        false                                   // Don't start immediately
    );
}

/**
 * Configures receiver
 **/
void configureHDLCReceiver(uint rxEnPin, uint clkInPin, uint dataInPin)
{
    //configure transceiver enable GPIO
    rxEnablePin = rxEnPin;
    gpio_init(rxEnablePin);
    gpio_set_dir(rxEnablePin, GPIO_OUT);
    enableHDLCReceiver(false);    //Disable RX for now

    //HDLC RX PIO configuration
    pio_sm_config rxDataSMCfg;
    uint rxDataOffset = 0;
    rxDataOffset = pio_add_program(rxPIO, &hdlc_rx_program);
    rxDataSM = pio_claim_unused_sm(rxPIO, true);
    rxDataSMCfg = hdlc_rx_program_init(rxPIO, rxDataSM, rxDataOffset, dataInPin);
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt0, false);       //IRQ0 (abort received)
    pio_set_irq0_source_enabled(rxPIO, pis_interrupt1, false);       //IRQ1 (TX completed)
    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_isr);                 //Set IRQ handler for new command
    irq_set_enabled(PIO0_IRQ_0, true);                               //Enable IRQ

    //Configure DMA channel to receive bytes
    configureRXDMA();
}

receiver_status receiveHDLCData(uint8_t address, uint8_t* buffer, uint32_t bufLen, uint32_t& rcvLen, uint64_t timeout)
{
    if(firstUse) {
        //Sets receiver address
        rcvAddress = address;
        rxBuffer = buffer;
        rxBufferMaxLen = bufLen;
        //Makes a timeout value
        timeOut = make_timeout_time_us(timeout);
        //Prepare DMA for receiving data
        prepareRx();
        //Enable the RX transceiver
        enableHDLCReceiver(true);
        firstUse = false;
    }
    receiver_status ret = busy;
    if(rxCompleted){
        if(rxCount > 1){
            // We received enough data for a valid HDLC frame
            // data + CRC     
            uint16_t crcDMACheck = dma_crc[2];
            uint16_t crcDataCheck = (data_crc[1]<<8) | data_crc[0];
            if(crcDMACheck == crcDataCheck){
                ret = done;
            }else{
                ret = bad_crc;
            }
            rcvLen = rxCount - 2;
            if(rcvLen > rxBufferMaxLen){
                rcvLen = rxBufferMaxLen;
            }
        }else{
            rcvLen = 0;
            ret = frame_short;
        }
        firstUse = true;
        dma_sniffer_disable();
    }else if((timeout != 0) && (absolute_time_diff_us(timeOut, get_absolute_time()) > 0)){
        firstUse = true;
        dma_sniffer_disable();
        ret = time_out;
    }
    return ret;
}
