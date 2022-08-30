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
#define RX_TRCV_ENABLE_PIN  2           // Receiver transceiver enable GPIO
// GPIO definitions for TX
#define DATA_TX_PIN 3
#define CLK_TX_PIN 4
#define TX_TRCV_ENABLE_PIN 5            // Emit transceiver enable GPIO

#define DEV_NUMBER 0x0                  // Device address on BUS (0 for master)
#define PCH_RETRIES 5                   // Number of retries to send PCH

#define ECHO_DETECT_TIMEOUT 5           // Number of ms before failing to detect echo clock 

static uint8_t buffer[65535];           // Data buffer (for RX and TX)
static NR_STATE nr_state = NR_IDLE;     // Actual state
static NR_CMD nr_command = NR_NONE;     // Pending command to be executed

static Consigne current_consigne;       // Actual consigne
static uint8_t disconnect_peer = 0;     // Peer to disconnect
static uint16_t buffer_size = 0;        // Size to transmit

static Station peers[32];           // Peers status


uint16_t to_thomson(uint16_t val){
    uint16_t ret = (val >> 8) & 0xFF;
    ret |= (val << 8) & 0xFF00;
    return ret;
}

/**
 * Dumps current config to sdio
 **/
void dump_current_consigne() {
    printf("****** Consigne *******\n");
    printf("Lenght : %u, dest : %u\n", current_consigne.length, current_consigne.dest);
    printf("Network task code : %u (delayed %u), Application task code : %u\n", (current_consigne.data.code_tache & 0x7f),
                ((current_consigne.data.code_tache & 0x80) != 0), current_consigne.data.code_app);
    printf("Msg bytes %u, Code page : %u, Code address : $%04x\n", to_thomson(current_consigne.data.msg_len),
                current_consigne.data.page, to_thomson(current_consigne.data.msg_addr));
    printf("Computer : %u, Application : %u\n", current_consigne.data.ordinateur, current_consigne.data.application);
    printf("Context data: \n");
    for(uint i=0;i<sizeof(current_consigne.data.ctx_data);++i){
        printf("%02x ", current_consigne.data.ctx_data[i]);
        if(((i+1) % 8) == 0){
            printf("\n");
        }
    }
    printf("\n*************************\n");
}

/**
 * Sets the nanoreseau state
 */
void set_nr_state(NR_STATE state) {
    nr_state = state;
    nr_usb_set_state(nr_state);
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
 * Convert a consigne to TX buffer
 */
uint consigne_to_buffer(const Consigne* consigne, const Station& dest, uint8_t* buffer) {
    uint len = 3 + consigne->length;
    buffer[0] = consigne->dest; // Destinataire
    buffer[1] = dest.msg_num;   // Control word
    buffer[2] = DEV_NUMBER;     // Expediteur
    memcpy(&buffer[3], &consigne->data, sizeof(consigne->data));
    return len;
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
            wait_for_no_clock();
            //TODO: Maybe timeout if bus is busy for too long
            sendData(pl, sizeof(pl));
            if(expected == MCNONE){
                ret = done;
                retriesCount = 0;
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

receiver_status wait_for_echo() {
    absolute_time_t end = make_timeout_time_ms(ECHO_DETECT_TIMEOUT);
    while(!is_clock_detected()){
        tight_loop_contents();
        if(absolute_time_diff_us(end, get_absolute_time()) > 0){        
            return time_out;
        }
    }
    //No waits for no clock
    wait_for_no_clock();
    return done;
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
 * Waits (or receive) the initial call
 **/
void receive_initial_call() {
    enum InternalState {WAIT_SELECT, GET_COMMAND, PCH, ERROR};
    static InternalState internalState = WAIT_SELECT;
    static uint8_t from = 0;
    static uint8_t consigneBytes = 0;
    static absolute_time_t lastStateChange = 0;

    uint32_t nbBytes = 0;                       //Number of bytes in initial call
    receiver_status status = bad_crc;           //HDLC receiver status
    InternalState nextState = internalState;    //Next state to take into account
    // int64_t elapsed = 0;                        //Time elapsed 
    switch (internalState)
    {
    case WAIT_SELECT:
        //Waits to receive a "Prise de ligne/Appel initial" request        
        if(wait_for_ctrl_nb(status, consigneBytes, from, MCAPI)){
            // Set state to receiving inital call
            set_nr_state(NR_RCV_INIT_CALL);
            // Reset USB error
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
        peers[from].msg_num = 0;
        status = send_ctrl(from, MCPCH, peers[from].msg_num, MCAMA);
        if(status == done){
            // Got select
            printf("Avis de mise en attente de %u (msg num : %x)\n", from, peers[from].msg_num);
            nextState = WAIT_SELECT;
            peers[from].waiting = true; // Received avis de mise en attente
            nr_usb_set_consigne(from, &current_consigne);
            dump_current_consigne();
        }else if(status == time_out){
            peers[from].waiting = false;
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
        // Go to IDLE if done
        if(internalState == WAIT_SELECT){
            set_nr_state(NR_IDLE);
        }
    }
}

void send_disconnect() {
    printf("Sending disconnection to station %u\n", disconnect_peer);
    uint8_t msg_num = 0;
    receiver_status status = busy;
    while((status = send_ctrl(disconnect_peer, MCDISC, msg_num, MCUA)) == busy){
        tight_loop_contents();
    }
    if(status == done){
        //TODO: Handle error
        peers[disconnect_peer].waiting = false;
        disconnect_peer = 0;
        set_nr_state(NR_IDLE);
        nr_command = NR_NONE;
    }else{
        printf("Disconnection failed!\n");
    }
}

void send_consigne() {
    printf("Will send consigne!\n");
    dump_current_consigne();
    uint8_t dest = current_consigne.dest;
    CTRL_WORD appel = MCAPA;   
    if(!peers[dest].waiting){
        printf("Performing initial call on peer %d\n", dest);
        appel = MCAPI;
        peers[dest].msg_num = 0xFF; //TODO: Not right, consigne lenght must be used here
    }
    printf("Sending send_ctrl\n");
    //TODO: Handle error, timeout...
    //In this case, the message lenght is given by multiple of 4
    uint8_t consLen = current_consigne.length/4;
    while(send_ctrl(dest, appel, consLen) != done){
        tight_loop_contents();    
    }
    printf("Waits echo\n");
    // Waits for echo, TODO: handle error
    receiver_status status = wait_for_echo();
    if(status == time_out){
        nr_usb_set_error(TIMEOUT, "Echo timeout!");
        return;
    }
    sleep_us(110);
    //Send the consigne
    uint len = consigne_to_buffer(&current_consigne, peers[dest], buffer);
    bool no_ack = false;
    for(int i=0;i<5;++i){
        setClock(true);
        sleep_us(50);
        sendData(buffer, len, true);    
        sleep_us(100);
        setClock(false);
        sleep_us(250);
        /*if(current_consigne.data.code_tache & 0x80){
            printf("Delayed execution (at disconnect)\n");
            return;
        }*/
        printf("Wait for ack\n");
        //Waits for ack
        CTRL_WORD ack = peers[dest].waiting ? MCOK : MCPCH;
        uint8_t caller = 0;
        if(wait_for_ctrl(peers[dest].msg_num, caller, ack, DEFAULT_RX_TIMEOUT) != done){
            no_ack = true;
        }else{
            no_ack = false;
            break;
        }
    }
    if(no_ack){
         nr_usb_set_error(TIMEOUT, "No ack");
         return;
    }
    nr_usb_set_cmd_done();
}

void send_data()
{
    CTRL_WORD appel = MCVR;
    uint8_t peer = buffer[0];
    while(send_ctrl(peer, appel, peers[peer].msg_num) != done){
        tight_loop_contents();
    }
    printf("Waits echo\n");
    // Waits for echo, TODO: handle error
    receiver_status status = wait_for_echo();
    if(status == time_out){
        nr_usb_set_error(TIMEOUT, "Echo timeout!");
        return;
    }
    sleep_us(110);
    buffer[1] = peers[peer].msg_num;
    buffer[2] = DEV_NUMBER;
    bool no_ack = false;
    for(int i=0;i<5;++i){
        setClock(true);
        sleep_us(50);
        sendData(buffer, buffer_size, true);    
        sleep_us(100);
        setClock(false);
        /*if(current_consigne.data.code_tache & 0x80){
            printf("Delayed execution (at disconnect)\n");
            return;
        }*/
        printf("Wait for ack\n");
        //Waits for ack
        CTRL_WORD ack = peers[peer].waiting ? MCOK : MCPCH;
        uint8_t caller = 0;
        if(wait_for_ctrl(peers[peer].msg_num, caller, ack, DEFAULT_RX_TIMEOUT) != done){
            no_ack = true;
        }else{
            no_ack = false;
            break;
        }
    }
    if(no_ack){
         nr_usb_set_error(TIMEOUT, "No ack");
         return;
    }
    nr_usb_set_cmd_done();

}

/**
 * Send a disconnect request for specified peers
 * TODO: Return a status if the command is rejected
 */
void request_nr_disconnect(uint8_t peer) {
    // TODO: Maybe add a mutex here
    disconnect_peer = peer;
    nr_command = NR_DISCONNECT;
}

/**
 * Sends a consigne to a peer
 * TODO: Return something in case of command rejected
 */
void request_nr_consigne(const Consigne* consigne) {
    // TODO: Maybe add a mutex here
    memcpy(&current_consigne, consigne, sizeof(Consigne));
    nr_command = NR_SEND_CONSIGNE;
}

void request_nr_tx_data(const void* txBuffer, uint16_t size, uint8_t peer) {
    //TODO: Not optimal. We may directly copy to buffer
    //more synchro between USB and this core must be added here
    buffer[0] = peer;
    memcpy(&buffer[3], txBuffer, size);
    buffer_size = size + 3;
    nr_command = NR_SEND_DATA;
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

    //Initializes peers state
    memset(peers, 0, sizeof(peers));

    //Initialize the clock detection
    initialize_clock_detect();    
    //Initialize TX state machine
    configureEmitter(TX_TRCV_ENABLE_PIN, CLK_TX_PIN, DATA_TX_PIN);
    //Initialize RX state machine
    configureHDLCReceiver(RX_TRCV_ENABLE_PIN, CLK_RX_PIN, DATA_RX_PIN);
    enableHDLCReceiver(true);
    absolute_time_t pTime = make_timeout_time_ms(500);
    while(true){
        if((nr_state == NR_IDLE) && (nr_command != NR_NONE)){
            nr_state = NR_BUSY;            
        }

        if((nr_state == NR_IDLE) || (nr_state == NR_RCV_INIT_CALL)){
            // We are waiting (or receiving) the initial call
            receive_initial_call();
            // Just to be sure USB is alive...
            if(absolute_time_diff_us(pTime, get_absolute_time())>0){
                pTime = make_timeout_time_ms(1000);            
                printf(".");
            }
        }else{
            // Interface is busy sending a command
            switch(nr_command){
                case NR_NONE:
                    // Should not happen
                    set_nr_state(NR_IDLE);
                    break;
                case NR_SEND_CONSIGNE:
                    //Send consigne to a peer
                    send_consigne();
                    break;
                case NR_SEND_DATA:
                    send_data();
                    break;
                case NR_GET_DATA:
                    // Receives data from a peer
                    break;
                case NR_DISCONNECT:
                    // Sends a disconnect to a peer
                    send_disconnect();
                    break;
            }
            nr_state = NR_IDLE;
            nr_command = NR_NONE;
        }        
    }
}
