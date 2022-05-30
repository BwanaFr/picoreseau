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

#include "usb/usb_tasks.hxx"
#include "picoreseau.hxx"

#include "tusb.h"

#define DATA_RX_PIN 0
#define CLK_RX_PIN 1
#define RX_TRCV_ENABLE_PIN  2       // Receiver transceiver enable GPIO

#define DATA_TX_PIN 3
#define CLK_TX_PIN 4
#define TX_TRCV_ENABLE_PIN 5        // Emit transceiver enable GPIO

#define DEV_NUMBER 0x0              // Device address on BUS (0 for master)

#define MIN_CONSIGNE_LEN 11         // A consigne must be at least 11 bytes

uint8_t buffer[65535];
NR_STATE nr_state = NR_IDLE;
NR_ERROR nr_error = NO_ERROR;
Consigne current_consigne;
uint8_t dest = 0;
uint8_t msg_num = 0;

/**
 * Convert a RX buffer to consigne
 **/
void buffer_to_consigne(uint8_t* buffer, Consigne* consigne, uint32_t len) {
    if(len > (sizeof(Consigne) + 3)){
        len = sizeof(Consigne) + 3;
    }
    memcpy(&consigne->data, &buffer[1], len-1);
    consigne->length = len - 3;     // Remove the 3 first bytes (this is part of the transport)
    consigne->dest = buffer[0];     // Destination is the first byte of the rx buffer
}

/**
 * Handles when the device is IDLE
 **/
// void handle_state_idle() {
//     enum InternalState {WAIT_SELECT, GET_COMMAND, ACK, WAIT_IDLE, ERROR};
//     static InternalState int_state = WAIT_SELECT;
//     uint32_t nbBytes = 0;
//     receiver_status status = bad_crc;
//     switch (int_state)
//     {
//     case WAIT_SELECT:
//         //Waits to receive a "Prise de ligne" request
//         consigne.length = 0;
//         status = receiveHDLCData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
//         if((status == done) && (nbBytes == 2)){
//             uint8_t ctrlW = buffer[0] & 0xF0;
//             // Lenght of the consigne
//             consigne.length = (buffer[0] & 0xF) * 4;
//             dest = buffer[1];
//             if(ctrlW == MCAPI){
//                 // Got select
//                 //printf("Appel initial de %u\n", dest);
//                 // Send echo by outputing a clock
//                 wait_for_no_clock();
//                 sleep_us(50);
//                 setClock(true);
//                 sleep_us(300);
//                 int_state = GET_COMMAND;
//                 return;
//             }
//         }
//         break;
//     case GET_COMMAND:
//         //Stop sending echo
//         setClock(false);
//         // Receives the command
//         status = receiveHDLCData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
//         if(status == done){
//             if(nbBytes < consigne.length){
//                 printf("Received %u bytes/%lu", nbBytes, consigne.length);
//                 int_state = ERROR;
//                 break;
//             }else{
//                 buffer_to_consigne(buffer, &consigne, nbBytes);
//                 int_state = ACK;
//             }
//         }
//         break;
//     case ACK:
//         // Sends the acknowledge
//         uint8_t ack[3];
//         ack[0] = dest;
//         ack[1] = MCPCH;
//         ack[2] = DEV_NUMBER;
//         sendData(ack, sizeof(ack));
//         int_state = WAIT_IDLE;
//         break;
//     case WAIT_IDLE:
//         status = receiveHDLCData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
//         if((status == done) && (nbBytes == 2)){
//             uint8_t ctrlW = buffer[0] & 0xF0;
//             if(ctrlW == MCAMA){
//                 msg_num = buffer[0] & 0xF;
//                 // Got select
//                 printf("Avis de mise en attente de %u (msg num : %x)\n", dest, msg_num);
//                 int_state = WAIT_SELECT;
//                 state = SELECTED;
//                 return;
//             }else{
//                 int_state = ERROR;
//             }
//         }else if(status != busy){
//             printf("%u ", status);
//             int_state = ERROR;
//         }
//         break;
//     default:
//         break;
//     }
//     if(int_state == ERROR){
//         printf("Error!\n");
//         setClock(false);
//         int_state = WAIT_SELECT;
//         state = IDLE;
//     }
// }

bool wait_for_ctrl(uint8_t& payload, uint8_t& caller, CTRL_WORD expected, uint64_t timeout) {
    absolute_time_t stopTime = make_timeout_time_us(timeout);
    uint8_t rxBuffer[10];  //Received buffer contains only 2 bytes because our address is already filtered by receiveHDLCData
    uint32_t nbBytes = 0;
    do{
        if((receiveHDLCData(DEV_NUMBER, rxBuffer, sizeof(rxBuffer), nbBytes) == done) && (nbBytes == 2)) {
            uint8_t ctrlW = rxBuffer[0] & 0xF0;
            if(ctrlW == expected) {
                payload = rxBuffer[0] & 0xF;
                caller = rxBuffer[1];
                return true;
            }
        }
    }while((timeout == 0) || (absolute_time_diff_us(stopTime, get_absolute_time()) > 0));
    //Here, a timeout occured
    return false;
}

/**
 * Core 1 entry for running USB tasks
 **/
 void core1_entry() {
     multicore_lockout_victim_init();
     // Initializes the USB stack
     nr_usb_init();
     while(true){
         // Runs USB tasks
         nr_usb_tasks();
     }
}

/**
 * Application main entry
 **/
int main() {
    //board_init();
    multicore_launch_core1(core1_entry);
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
    printf("Lenght of Consigne data is %u\n", sizeof(ConsigneData));
    //Initialize the clock detection
    initialize_clock_detect();    
    //Initialize TX state machine
    configureEmitter(TX_TRCV_ENABLE_PIN, CLK_TX_PIN, DATA_TX_PIN);
    //Initialize RX state machine
    configureHDLCReceiver(RX_TRCV_ENABLE_PIN, CLK_RX_PIN, DATA_RX_PIN);
    enableHDLCReceiver(true);
    absolute_time_t pTime = make_timeout_time_ms(500);
    uint8_t pl, from = 0;
    while(true){
        /*switch (state)
        {
        case WAITING_FOR_LINE:
            
            if(wait_for_ctrl(pl, from)){
                printf("Appel initial from %x with %u bytes\n", from, (pl*4));
                state = LINE_TAKEN;
            }
            break;
        case LINE_TAKEN:                 
            break;
        default:
            break;
        }*/
        if(absolute_time_diff_us(pTime, get_absolute_time())>0){
            pTime = make_timeout_time_ms(1000);            
            if(tud_vendor_mounted()){
                // printf(".");
                //nr_usb_publish_state(LINE_TAKEN, &consigne);
            }
        }
    }
}


NR_STATE get_nr_state(NR_ERROR &error) {
    error = nr_error;
    return nr_state;
}

const Consigne* get_nr_current_consigne() {
    return &current_consigne;
}