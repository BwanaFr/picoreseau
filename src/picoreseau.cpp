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

/**
 * Dumps current config to sdio
 **/
void dump_current_consigne() {
    printf("****** Consigne *******\n");
    printf("Lenght : %u, dest : %u\n", current_consigne.length, current_consigne.dest);
    printf("Network task code : %u, Application task code : %u\n", current_consigne.data.code_tache,
                current_consigne.data.code_app);
    printf("Msg bytes %u, Code page : %u, Code address : $%x\n", current_consigne.data.msg_len,
                current_consigne.data.page, current_consigne.data.msg_addr);
    printf("Computer : %u, Application : %u\n", current_consigne.data.ordinateur, current_consigne.data.application);
    printf("Context data: \n");
    for(uint i=0;i<sizeof(current_consigne.data.ctx_data);++i){
        printf("%02x ", current_consigne.data.ctx_data[i]);
        if(((i+1) % 8) == 0){
            printf("\n");
        }
    }
    printf("\n*************************");
}

/**
 * Convert a RX buffer to consigne
 **/
void buffer_to_consigne(uint8_t* buffer, Consigne* consigne, uint32_t len) {
    if(len > (sizeof(Consigne) + 3)){
        len = sizeof(Consigne) + 3;
    }
    memcpy(&consigne->data, &buffer[2], len-1);
    consigne->length = len;         // Lenght of the consigne
    consigne->dest = DEV_NUMBER;    // Dest should be us (TODO check)
}

/**
 * Waits for a specific control word in a non-blocking mode
 * @param payload Received control-word payload
 * @param caller Caller computer network address
 * @param expected Control word to listen to
 * @return true when the expected control word is received
 **/
bool wait_for_ctrl_nb(uint8_t& payload, uint8_t& caller, CTRL_WORD expected) {
    static uint8_t ctrlBuffer[2]; // Will wait for control word + ID of the caller
    uint32_t nbBytes = 0;
    receiver_status status = receiveHDLCData(DEV_NUMBER, ctrlBuffer, sizeof(ctrlBuffer), nbBytes);
    if((status == done) && (nbBytes == 2)) {
        uint8_t ctrlW = ctrlBuffer[0] & 0xF0;
        if(ctrlW == expected) {
            payload = ctrlBuffer[0] & 0xF;
            caller = ctrlBuffer[1];
            return true;
        }
    }
    return false;
}

/**
 * Waits for a specific control word
 **/
bool wait_for_ctrl(uint8_t& payload, uint8_t& caller, CTRL_WORD expected, uint64_t timeout) {
    absolute_time_t stopTime = make_timeout_time_us(timeout);
    do{
        if(wait_for_ctrl_nb(payload, caller, expected)){
            return true;
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
 * Handles when the device is IDLE
 **/
void handle_state_idle() {
    enum InternalState {WAIT_SELECT, GET_COMMAND, ACK, WAIT_IDLE, ERROR};
    static InternalState int_state = WAIT_SELECT;
    static uint8_t from = 0;
    static uint8_t consigne_bytes = 0;
    uint32_t nbBytes = 0;
    receiver_status status = bad_crc;
    switch (int_state)
    {
    case WAIT_SELECT:
        //Waits to receive a "Prise de ligne" request        
        if(wait_for_ctrl_nb(consigne_bytes, from, MCAPI)){
            // Got select
            consigne_bytes = consigne_bytes * 4;
            printf("Appel initial de %u with a %u bytes consigne\n", from, consigne_bytes);
            // Send echo by outputing a clock
            // TODO: Maybe make this non-blocking too
            wait_for_no_clock();
            sleep_us(50);
            setClock(true);
            sleep_us(300);
            int_state = GET_COMMAND;
            return;
        }
        break;
    case GET_COMMAND:
        //Stop sending echo
        setClock(false);
        // Receives the command
        status = receiveHDLCData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if(status == done){
            if(nbBytes < consigne_bytes){
                printf("Received %lu bytes, expecting %u", nbBytes, consigne_bytes);
                int_state = ERROR;
                break;
            }else{
                printf("Command frame is %lu long\n", nbBytes);
                buffer_to_consigne(buffer, &current_consigne, consigne_bytes);
                int_state = ACK;
            }
        }
        break;
    case ACK:
        // Sends the acknowledge
        uint8_t ack[3];
        ack[0] = from;
        ack[1] = MCPCH;
        ack[2] = DEV_NUMBER;
        sendData(ack, sizeof(ack));
        int_state = WAIT_IDLE;
        break;
    case WAIT_IDLE:
        status = receiveHDLCData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
        if((status == done) && (nbBytes == 2)){
            uint8_t ctrlW = buffer[0] & 0xF0;
            if(ctrlW == MCAMA){
                uint8_t msg_num = buffer[0] & 0xF;
                // Got select
                printf("Avis de mise en attente de %u (msg num : %x)\n", from, msg_num);
                int_state = WAIT_SELECT;
                dump_current_consigne();
                //TODO: Update status over USB
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
        //TODO: Update error state over USB
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
        switch (nr_state)
        {
        case NR_IDLE:
            handle_state_idle();
            break;
        default:
            break;
        }
        if(absolute_time_diff_us(pTime, get_absolute_time())>0){
            pTime = make_timeout_time_ms(1000);            
            printf(".");
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