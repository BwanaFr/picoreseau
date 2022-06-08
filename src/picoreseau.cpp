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

// GPIO definitions for RX
#define DATA_RX_PIN 0
#define CLK_RX_PIN 1
#define RX_TRCV_ENABLE_PIN  2       // Receiver transceiver enable GPIO
// GPIO definitions for TX
#define DATA_TX_PIN 3
#define CLK_TX_PIN 4
#define TX_TRCV_ENABLE_PIN 5        // Emit transceiver enable GPIO

#define DEV_NUMBER 0x0              // Device address on BUS (0 for master)
#define PCH_RETRIES 5               // Number of retries to send PCH

static uint8_t buffer[65535];
static NR_STATE nr_state = NR_IDLE;
static NR_ERROR nr_error = NO_ERROR;
static Consigne current_consigne;

static uint8_t actual_peer = 0;
static uint8_t actual_msg_num = 0;

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
 * @param rcv_status HDLC receiver status
 * @param payload Received control-word payload
 * @param caller Caller computer network address
 * @param expected Control word to listen to
 * @return true when the expected control word is received
 **/
bool wait_for_ctrl_nb(receiver_status& rcv_status, uint8_t& payload, uint8_t& caller, CTRL_WORD expected) {
    static uint8_t ctrlBuffer[2]; // Will wait for control word + ID of the caller
    uint32_t nbBytes = 0;
    rcv_status = receiveHDLCData(DEV_NUMBER, ctrlBuffer, sizeof(ctrlBuffer), nbBytes);
    if((rcv_status == done) && (nbBytes == 2)) {
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
receiver_status wait_for_ctrl(uint8_t& payload, uint8_t& caller, CTRL_WORD expected, uint64_t timeout) {
    receiver_status ret = busy;
    absolute_time_t stopTime = make_timeout_time_us(timeout);
    do{
        if(wait_for_ctrl_nb(ret, payload, caller, expected)){
            return done;
        }
        if((ret != busy) && (ret != done)){
            //HDLC receiver error
            return ret;
        }
    }while((timeout == 0) || (absolute_time_diff_us(get_absolute_time(), stopTime) > 0));
    //Here, a timeout occured
    return time_out;
}

/**
 * Sends a control word
 **/
receiver_status send_ctrl(uint8_t to, CTRL_WORD ctrl, uint8_t& payload, CTRL_WORD expected, uint64_t timeout, uint32_t retries) {
    enum InternalState {SEND_DATA, WAIT_RESPONSE};
    static InternalState internalState = SEND_DATA;
    static uint32_t retriesCount = 0;
    receiver_status ret = busy;
    uint8_t from = 0;
    switch(internalState){
        case SEND_DATA:
            // Sends the acknowledge
            uint8_t pl[3];
            pl[0] = to;
            pl[1] = ctrl | (payload & 0xF);
            pl[2] = DEV_NUMBER;
            //TODO: Maybe timeout if bus is busy for too long
            sendData(pl, sizeof(pl));
            if(expected == MCNONE){
                ret = done;
            }else{
                ret = busy;
                retriesCount = 0;
                internalState = WAIT_RESPONSE;                
            }
            break;
        case WAIT_RESPONSE:
            ret = wait_for_ctrl(payload, from, expected, timeout);
            if(ret == done){
                if(from == to){
                    // Got answer we wanted
                    internalState = SEND_DATA;
                    retriesCount = 0;
                    return ret;
                }
            }
            if(ret != busy){
                // Receiver is not busy anymore
                // Something not expected occured
                internalState = SEND_DATA;
                if(++retriesCount>=retries){
                    ret = time_out;
                    retriesCount = 0;                    
                }else{
                    //Still busy, try again
                    ret = busy;
                }
            }
            break;
    }
    return ret;
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
    enum InternalState {WAIT_SELECT, GET_COMMAND, PCH, ERROR};
    static InternalState internalState = WAIT_SELECT;
    static uint8_t from = 0;
    static uint8_t consigneBytes = 0;
    static absolute_time_t lastStateChange = 0;

    uint32_t nbBytes = 0;                       //Number of bytes in initial call
    receiver_status status = bad_crc;           //HDLC receiver status
    InternalState nextState = internalState;    //Next state to take into account
    int64_t elapsed = 0;                        //Time elapsed 
    switch (internalState)
    {
    case WAIT_SELECT:
        //Waits to receive a "Prise de ligne/Appel initial" request        
        if(wait_for_ctrl_nb(status, consigneBytes, from, MCAPI)){
            nr_usb_set_error(NO_ERROR, "");
            // Got select
            consigneBytes = consigneBytes * 4;
            // printf("Appel initial de %u with a %u bytes consigne\n", from, consigneBytes);
            // Send echo by outputing a clock
            // TODO: Maybe make this non-blocking too?
            wait_for_no_clock();
            sleep_us(50);   // Do not send echo too fast, it seems to be not supported by MO5
            setClock(true); // Output echo clock during 300ns, giving time to "silence detection" circuit to go false
            sleep_us(300);  
            nextState = GET_COMMAND;
        }
        break;
    case GET_COMMAND:
        //Stop sending echo
        setClock(false);
        // Receives the command
        if(absolute_time_diff_us(lastStateChange, get_absolute_time()) >= DEFAULT_RX_TIMEOUT){
            nr_usb_set_error(TIMEOUT, "Command rx timeout");
            resetReceiverState();
            nextState = ERROR;
        }else{
            status = receiveHDLCData(DEV_NUMBER, buffer, sizeof(buffer), nbBytes);
            if(status == done){
                //Checks if the control word is 0 and the frame is received from our peer
                // if not, simply ignore we will timeout anyway
                if(((buffer[0] & 0xF0) == 0x0) && (buffer[1] == from)){
                    //TODO: Checks this, consigneBytes can be 2 bytes less
                    if(nbBytes < consigneBytes){
                        // printf("Received %lu bytes, expecting %u", nbBytes, consigneBytes);
                        nr_usb_set_error(SHORT_FRAME, "Command data too short");
                        nextState = ERROR;
                        break;
                    }else{
                        buffer_to_consigne(buffer, &current_consigne, consigneBytes);
                        nextState = PCH;
                        break;
                    }
                }
            }
        }
        break;
    case PCH:
        // Sends the acknowledge
        {
        actual_msg_num = 0;
        status = send_ctrl(from, MCPCH, actual_msg_num, MCAMA);
        if(status == done){
            // Got select
            printf("Avis de mise en attente de %u (msg num : %x)\n", from, actual_msg_num);
            nextState = WAIT_SELECT;
            set_nr_state(NR_SELECTED);
            actual_peer = from;
            nr_usb_set_consigne(from, actual_msg_num, &current_consigne);
            dump_current_consigne();
        }else if(status == time_out){
            nr_usb_set_error(TIMEOUT, "MCAMA rx timeout");
            resetReceiverState();
            nextState = ERROR;
        }
        }
        break;
    default:
        nextState = WAIT_SELECT;
        break;
    }
    if(nextState == ERROR){
        printf("Error!\n");
        setClock(false);
        nextState = WAIT_SELECT;
    }

    if(nextState != internalState){
        // Future internal state changed, get time for watchdog
        lastStateChange = get_absolute_time();
        internalState = nextState;
    }
}

void handle_state_disconnect() {
    receiver_status status = send_ctrl(actual_peer, MCDISC, actual_msg_num, MCUA);
    if(status == done){
        set_nr_state(NR_IDLE);
    }
}

void set_nr_state(NR_STATE state) {
    nr_state = state;
    nr_usb_set_state(nr_state);
}

void send_nr_disconnect(uint8_t peer, uint8_t msg_num) {
    actual_peer = peer;
    actual_msg_num = msg_num;
    set_nr_state(NR_DISCONNECT);
}

void send_nr_consigne(const Consigne* consigne) {
    memcpy(&current_consigne, consigne, sizeof(Consigne));
    dump_current_consigne();
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
    while(true){
        switch (nr_state)
        {
        case NR_IDLE:
            handle_state_idle();
            break;
        case NR_DISCONNECT:
            handle_state_disconnect();
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
