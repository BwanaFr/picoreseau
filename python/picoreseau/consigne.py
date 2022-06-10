import struct
import logging

class Consigne:
    """
    Class for representing a nanoreseau consigne

    ...

    Attributes
    ----------
    dest : int
        Consigne destination. This is the number of the nanoreseau station (0-31).
    code_tache : int
        Task code number.
    code_app : int
        Application code number.
    msg_len : int
        Number of bytes in the message
    page : int
        Memory page number
    msg_addr : int
        Memory message addres (to read or write from/to)
    computer : int
        Type of computer (0 : TO7, 1 : MO5, 2: TO7/70)
    application : int
        Application type (0 : Unknown, 1 : Basic 1.0, 2 : LOGO, 3 : LSE)
    ctx_data : bytes
        Context dependant data. Up to 51 bytes.
    """
    # typedef struct ConsigneData {
    #     uint8_t code_tache;     // Code tache reseau (start of command RX)
    #     uint8_t code_app;       // Code tache application 
    #     uint16_t msg_len;       // Nombre d'ocets du message
    #     uint8_t page;           // Page
    #     uint16_t msg_addr;      // Message adresse
    #     uint8_t ordinateur;     // Ordinateur (0 : TO7, 1 : MO5, 2: TO7/70)
    #     uint8_t application;    // Application (0 : Unknown, 1 : Basic 1.0, 2 : LOGO, 3 : LSE)
    #     uint8_t ctx_data[51];   // Context dependant bytes
    # }ConsigneData;

    # //Nanoreseau consigne with meta-data
    # #pragma pack (1)
    # typedef struct Consigne {
    #     uint8_t length;         // Longueur de la consigne
    #     uint8_t dest;           // Destinataire
    #     ConsigneData data;      // Consigne data
    # }Consigne;

    CONSIGNE_HEADER = '>BBBBHBHBB'
    CONSIGNE_HEADER_SIZE = struct.calcsize(CONSIGNE_HEADER)

    CONSIGNE_CONTEXT_DATA_SIZE = 51
    CONSIGNE_SIZE = CONSIGNE_HEADER_SIZE + CONSIGNE_CONTEXT_DATA_SIZE

    logger = logging.getLogger("Consigne")

    def __init__(self, src=None):
        """
        Class constructor
        """
        self.length = 0
        self.dest = 0
        self.code_tache = 0
        self.code_app = 0
        self.msg_len = 0
        self.page = 0
        self.msg_addr = 0
        self.computer = 0
        self.application = 0
        self.ctx_data = bytearray(0)
        if src:            
            self.from_bytes(src)

    def from_bytes(self, b):
        """
            Unpacks a consigne from the bytes

        ...

        Parameters
        ----------
        b : bytes
            Array of bytes containing data to be deserialized        
        """
        # uint8_t length;         // Longueur de la consigne
        # uint8_t dest;           // Destinataire
        # uint8_t code_tache;     // Code tache reseau (start of command RX)
        # uint8_t code_app;       // Code tache application 
        # uint16_t msg_len;       // Nombre d'ocets du message
        # uint8_t page;           // Page
        # uint16_t msg_addr;      // Message adresse
        # uint8_t ordinateur;     // Ordinateur (0 : TO7, 1 : MO5, 2: TO7/70)
        # uint8_t application;    // Application (0 : Unknown, 1 : Basic 1.0, 2 : LOGO, 3 : LSE)
        # uint8_t ctx_data[51];   // Context dependant bytes
        Consigne.logger.debug(f'Unpacking consigne from a {len(b)} bytes array')
        self.length, \
        self.dest, \
        self.code_tache, \
        self.code_app, \
        self.msg_len, \
        self.page, \
        self.msg_addr, \
        self.computer, \
        self.application = struct.unpack_from(self.CONSIGNE_HEADER, b)
        self.ctx_data = b[self.CONSIGNE_HEADER_SIZE:]

    def to_bytes(self):
        # Consigne length is always a multiple of 4
        length = self.CONSIGNE_HEADER_SIZE - 2 + len(self.ctx_data)
        if length % 4 != 0:
            length = int(length/4) + 1
        else:
            length = int(length/4)
        # Create consigne
        ret = bytearray(struct.pack(self.CONSIGNE_HEADER,
                        length,
                        self.dest,
                        self.code_tache,
                        self.code_app,
                        self.msg_len,
                        self.page,
                        self.msg_addr,
                        self.computer,
                        self.application))
        # Add context data
        ret.extend(self.ctx_data)
        # Insert padding
        rem_bytes = (length*4) - (len(ret) -2)
        ret.extend(bytearray(rem_bytes))
        return ret
    
    def __str__(self):
        return f'Consigne : Tache {self.code_tache}, app {self.code_app}, msg_len {self.msg_len}, page {self.page}, addr {self.msg_addr}'

