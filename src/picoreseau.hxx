#ifndef _PICORESEAU_HXX__
#define _PICORESEAU_HXX__
#include "pico/stdlib.h"
#include "hdlc_rx.h"

/**
 * Main state machine states
 */
enum NR_STATE{
    NR_IDLE,           // Waits for a peer to select us
    NR_SELECTED,       // Line taken with peer
    NR_SEND_CONSIGNE,  // Sends consigne to a peer
    NR_SEND_DATA,      // Sends data to a peer (previously selected)
    NR_GET_DATA,       // Reads data from a peer (previously selected)
    NR_DISCONNECT      // Disconnects the peer
};

/**
 * Errors codes
 */
enum NR_ERROR{
    NO_ERROR,
    TIMEOUT,
    SHORT_FRAME,
};

//Control words
enum CTRL_WORD {
    MCNONE  = 0b00000000,     // None
    MCVR    = 0b10000000,     // Vas-y recois
    MCPCH   = 0b10010000,     // Prise en charge
    MCAMA   = 0b10100000,     // Avis de mise en attente
    MCVE    = 0b10110000,     // Vas-y emets
    MCDISC  = 0b11000000,     // Deconnecte
    MCAPA   = 0b11010000,     // Appel sous attente
    MCOK    = 0b11100000,     // Ok
    MCUA    = 0b11100000,     // UA (ok de disconnect)
    MCAPI   = 0b11110000,     // Appel initial
};

//Consigne data on the wire
#pragma pack (1)
typedef struct ConsigneData {
    uint8_t code_tache;     // Code tache reseau (start of command RX)
    uint8_t code_app;       // Code tache application 
    uint16_t msg_len;       // Nombre d'ocets du message
    uint8_t page;           // Page
    uint16_t msg_addr;      // Message adresse
    uint8_t ordinateur;     // Ordinateur (0 : TO7, 1 : MO5, 2: TO7/70)
    uint8_t application;    // Application (0 : Unknown, 1 : Basic 1.0, 2 : LOGO, 3 : LSE)
    uint8_t ctx_data[51];   // Context dependant bytes
}ConsigneData;

//Nanoreseau consigne with meta-data
#pragma pack (1)
typedef struct Consigne {
    uint8_t length;         // Longueur de la consigne
    uint8_t dest;           // Destinataire
    ConsigneData data;      // Consigne data
}Consigne;

#define DEFAULT_RX_TIMEOUT  2000    // RX timeout in microseconds
#define SEND_CTRL_RETRIES 5         // Number of times to try to resend the control word on bus

/**
 * Waits for a 3 bytes specified control word
 * @param payload The 4 bits payload sent with the control word
 * @param caller Caller station number
 * @param expected Control word to expect
 * @param timeout RX timeout in us (0 if no timeout)
 * @return Receiver status
 **/
receiver_status wait_for_ctrl(uint8_t& payload, uint8_t& caller, CTRL_WORD expected = MCAPI, uint64_t timeout=0);

/**
 * Sends a control word to the specified peer and waits for reply
 * @param to Peer to send the control word to
 * @param ctrl Control word to send
 * @param payload LSB of the control word byte (probably message number)
 * @param expected Expected response, MCNONE if no response expected
 * @param timeout Response timeout, taken into account only if expected != MCNONE
 * @param retries Number of time to retry to send the message
 * @return status of the receiver
 * */
receiver_status send_ctrl(uint8_t to, CTRL_WORD ctrl, uint8_t& payload, CTRL_WORD expected=MCNONE, 
                            uint64_t timeout=DEFAULT_RX_TIMEOUT, uint32_t retries=SEND_CTRL_RETRIES);

/**
 * Sends a disconnect request to the station
 * @param peer Station ID to disconnect
 * @param msg_num Message number in the nanoreseau frame
 **/
void send_nr_disconnect(uint8_t peer, uint8_t msg_num);

/**
 * Sends a consigne to a device (called from USB functions)
 * If the device is actually the one selected
 * A MCAPA will be issued. Else, a line take request will be made.
 * 
 **/
void send_nr_consigne(const Consigne* consigne);

/**
 * Sets the main state machine state
 **/
void set_nr_state(NR_STATE state);

#endif