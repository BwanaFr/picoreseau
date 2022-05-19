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

#define MIN_CONSIGNE_LEN 11         // A consigne must be at least 11 bytes

uint8_t buffer[65535];
NR_STATE state = IDLE;
Consigne consigne;
uint8_t dest = 0;
uint8_t msg_num = 0;
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
    enum InternalState {WAIT_SELECT, GET_COMMAND, ACK, WAIT_IDLE, ERROR};
    static InternalState int_state = WAIT_SELECT;
    uint32_t nbBytes = 0;
    receiver_status status = bad_crc;
    switch (int_state)
    {
    case WAIT_SELECT:
        //Waits to receive a "Prise de ligne" request
        consigne.length = 0;
        status = receiveData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if((status == done) && (nbBytes == 2)){
            uint8_t ctrlW = buffer[0] & 0xF0;
            // Lenght of the consigne
            consigne.length = (buffer[0] & 0xF) * 4;
            dest = buffer[1];
            if(ctrlW == MCAPI){
                // Got select
                //printf("Appel initial de %u\n", dest);
                // Send echo by outputing a clock
                wait_for_no_clock();
                sleep_us(50);
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
            if(nbBytes < consigne.length){
                printf("Received %u bytes/%lu", nbBytes, consigne.length);
                int_state = ERROR;
                break;
            }else{
                buffer_to_consigne(buffer, &consigne, nbBytes);
                int_state = ACK;
            }
        }
        break;
    case ACK:
        // Sends the acknowledge
        uint8_t ack[3];
        ack[0] = dest;
        ack[1] = MCPCH;
        ack[2] = DEV_NUMBER;
        sendData(ack, sizeof(ack));
        int_state = WAIT_IDLE;
        break;
    case WAIT_IDLE:
        status = receiveData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if((status == done) && (nbBytes == 2)){
            uint8_t ctrlW = buffer[0] & 0xF0;
            if(ctrlW == MCAMA){
                msg_num = buffer[0] & 0xF;
                // Got select
                printf("Avis de mise en attente de %u (msg num : %x)\n", dest, msg_num);
                int_state = WAIT_SELECT;
                state = SELECTED;
                return;
            }else{
                int_state = ERROR;
            }
        }else if(status != busy){
            printf("%u ", status);
            int_state = ERROR;
        }
        break;
    default:
        break;
    }
    if(int_state == ERROR){
        printf("Error!\n");
        setClock(false);
        int_state = WAIT_SELECT;
        state = IDLE;
    }
}

void handle_state_selected() {
    enum InternalState {SDCALL, WAIT_ECHO, SEND_CMD, WAIT_ACK};
    static InternalState int_state = SDCALL;
    switch(int_state){
        case SDCALL:
            printf("SDCALL\n");
            sleep_us(100);
            uint8_t msg[3];
            msg[0] = dest;
            //msg[1] = MCAPA | msg_num;
            msg[1] = MCDISC;
            msg[2] = DEV_NUMBER;
            sendData(msg, sizeof(msg));
            int_state = WAIT_ECHO;
            break;
        case WAIT_ECHO:
            while(!is_clock_detected(true)){}
            printf("Echo detected!\n");
            int_state = SDCALL;
            state = IDLE;
            break;
        case SEND_CMD:
            break;
    }
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
    enableReceiver(true);
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