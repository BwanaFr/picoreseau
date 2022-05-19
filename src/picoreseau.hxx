#ifndef _PICORESEAU_HXX__
#define _PICORESEAU_HXX__
#include "pico/stdlib.h"

//Main state machine
enum NR_STATE{
    WAITING_FOR_LINE,   // Waits for a peer to take line
    LINE_TAKEN,         // Line taken with peer
};

//Control words
enum CTRL_WORD {
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

//Nanoreseau consigne
#pragma pack (1)
typedef struct Consigne {
    uint8_t length;         // Longueur de la consigne
    uint8_t dest;           // Destinataire
    uint8_t code_tache;     // Code tache reseau (start of command RX)
    uint8_t code_app;       // Code tache application 
    uint16_t msg_len;       // Nombre d'ocets du message
    uint8_t page;           // Page
    uint16_t msg_addr;      // Message adresse
    uint8_t ordinateur;     // Ordinateur (0 : TO7, 1 : MO5, 2: TO7/70)
    uint8_t application;    // Application (0 : Unknown, 1 : Basic 1.0, 2 : LOGO, 3 : LSE)
    uint8_t ctx_data[51];   // Context dependant bytes
}Consigne;

/**
 * Waits for a 3 bytes specified control word
 * @param payload The 4 bits payload sent with the control word
 * @param caller Caller station number
 * @param expected Control word to expect
 * @param timeout RX timeout in us (0 if no timeout)
 * @return True on success, false on failure
 **/
bool wait_for_ctrl(uint8_t& payload, uint8_t& caller, CTRL_WORD expected = MCAPI, uint64_t timeout=0);


#endif