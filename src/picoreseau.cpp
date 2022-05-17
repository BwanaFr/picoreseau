#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

#include "hdlc_rx.h"
#include "hdlc_tx.h"
#include "clock_detect.h"
#include "pico/time.h"

#include "picoreseau.hxx"

#include "tusb_config.h"
#include "tusb.h"
#include "usb/get_serial.h"
#include "usb/cdc_uart.h"


#define DATA_RX_PIN 0
#define CLK_RX_PIN 1
#define RX_TRCV_ENABLE_PIN  2       //Receiver transceiver enable GPIO

#define DATA_TX_PIN 3
#define CLK_TX_PIN 4
#define TX_TRCV_ENABLE_PIN 5        //Emit transceiver enable GPIO

#define DEV_NUMBER 0x0              // Device address on BUS (0 for master)


uint8_t buffer[65535];
NR_STATE state = IDLE;
Consigne consigne;
uint8_t dest = 0;

/**
 * Convert a RX buffer to consigne
 **/
void buffer_to_consigne(uint8_t* buffer, Consigne* consigne, uint32_t len) {
    consigne->length = len - 3; //Remove the 3 first bytes
    memcpy(&consigne->dest, &buffer[3], (sizeof(Consigne) - sizeof(consigne->ctx_data) -sizeof(consigne->length)));
    consigne->ctx_data = &buffer[10];
}

/**
 * Handles when the device is IDLE
 **/
void handle_state_idle() {
    enum InternalState {WAIT_SELECT, GET_COMMAND, ACK, WAIT_IDLE};
    static InternalState int_state = WAIT_SELECT;
    uint32_t nbBytes = 0;
    receiver_status status = bad_crc;
    switch (int_state)
    {
    case WAIT_SELECT:
        //Waits to receive a "Prise de ligne" request
        status = receiveData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if((status == done) && (nbBytes == 2)){
            uint8_t ctrlW = buffer[0] & 0xF0;
            // Lenght of the consigne
            //uint8_t consigneLen = (buffer[0] & 0xF) * 4;
            dest = buffer[1];
            if(ctrlW == MCAPI){
                // Got select
                //printf("Appel initial de %u\n", dest);
                // Wait for the line to be ready
                while(is_clock_detected(true)){}
                sleep_us(40);
                // Send echo by outputing a clock
                setClock(true);
                sleep_us(300);
                int_state = GET_COMMAND;
                return;
            }
        }
        break;
    case GET_COMMAND:
        //Stop sending echo
        setClock(false);
        // Receives the command
        status = receiveData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if(status == done){
            buffer_to_consigne(buffer, &consigne, nbBytes);
            while(is_clock_detected(true)){}
            setClock(true);
            sleep_us(30);
            int_state = ACK;
        }
        break;
    case ACK:
        // Sends the acknowledge
        uint8_t ack[3];
        ack[0] = dest;
        ack[1] = MCPCH;
        ack[2] = DEV_NUMBER;
        sendData(ack, sizeof(ack));
        setClock(false);
        int_state = WAIT_IDLE;
        break;
    case WAIT_IDLE:
        status = receiveData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if((status == done) && (nbBytes == 2)){
            uint8_t ctrlW = buffer[0] & 0xF0;
            if(ctrlW == MCAMA){
                // Got select
                printf("Avis de mise en attente de %u\n", dest);
                int_state = WAIT_SELECT;
                return;
            }else{
                printf("Select done!");
            }
        }else if(status != busy){
            printf("%u ", status);
        }
        break;
    default:
        break;
    }
}

void handle_state_selected() {

}

/**
 * Application main entry
 **/
int main() {
    //board_init();
    usb_serial_init();
    cdc_uart_init();
    tusb_init();

    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    //Disable power-save of buck converter
    gpio_init(PICO_SMPS_MODE_PIN);
    gpio_set_dir(PICO_SMPS_MODE_PIN, GPIO_OUT);
    gpio_put(PICO_SMPS_MODE_PIN, true);

    // For debug, wait some time
    for(int i=0;i<30;++i){
        printf(".");
        sleep_ms(100);
    }
    printf("\n");
    //Initialize the clock detection
    initialize_clock_detect();    
    //Initialize TX state machine
    configureEmitter(TX_TRCV_ENABLE_PIN, CLK_TX_PIN, DATA_TX_PIN);
    //Initialize RX state machine
    configureReceiver(RX_TRCV_ENABLE_PIN, CLK_RX_PIN, DATA_RX_PIN);

    absolute_time_t pTime = make_timeout_time_ms(500);
    while(true){
        switch (state)
        {
        case IDLE:
            handle_state_idle();
            break;
        case SELECTED:
            handle_state_selected();            
            break;
        default:
            break;
        }       
        if(absolute_time_diff_us(pTime, get_absolute_time())>0){
            pTime = make_timeout_time_ms(500);
            printf(".");
        }
    }
}