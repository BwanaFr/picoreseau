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

    # List of known tasks codes
    TC_INIT_CALL = 0    # Initialization request (for master), nop for slaves
    TC_PUT_ON_HOLD = 1  # Mise en attente
    TC_EXEC_CODE = 2    # Execute 6809 code on slave
    TC_DISPLAY = 3      # Display on slave screen
    TC_SEND_SCREEN = 4  # Get slave screen
    TC_SEND_MEMORY = 5  # Send memory
    TC_COPY_REPORT = 6  # Copy the report (compte-rendu)
    TC_BASIC_1 = 7      # Used by basic 1.0
    TC_FILE = 9         # File system
    TC_PRINTER = 11     # Printer spooler

    TASK_CODE = {
        TC_INIT_CALL : 'Inital call/nop',
        TC_PUT_ON_HOLD : 'On hold',
        TC_EXEC_CODE : 'Execute code',
        TC_DISPLAY : 'Display on slave',
        TC_SEND_SCREEN : 'Send screen',
        TC_SEND_MEMORY : 'Send memory',
        TC_COPY_REPORT : 'Copy report',
        TC_BASIC_1 : 'Basic 1.0',
        TC_FILE : 'File',
        TC_PRINTER : 'Printer',
    }

    COMPUTER_TO7 = 0
    COMPUTER_MO5 = 1
    COMPUTER_TO7_70 = 2

    COMPUTER = {
        COMPUTER_TO7 : 'TO7',
        COMPUTER_MO5 : 'MO5',
        COMPUTER_TO7_70 : 'TO7/70'
    }

    APPLICATION_UNSPEC = 0
    APPLICATION_BASIC_1 = 1
    APPLICATION_LOGO = 2
    APPLICATION_LSE = 3

    APPLICATION = {
        APPLICATION_UNSPEC : 'Unspecified',
        APPLICATION_BASIC_1 : 'Basic 1.0',
        APPLICATION_LOGO : 'LOGO',
        APPLICATION_LSE : 'LSE',
    }

    logger = logging.getLogger("Consigne")

    def __init__(self, bin_size=None):
        """
        Class constructor
        """
        self.length = 0
        self.dest = 0
        self.delayed = False
        self.code_tache = 0
        self.code_app = 0
        self.msg_len = 0
        self.page = 0
        self.msg_addr = 0
        self.computer = 0
        self.application = 0
        self.ctx_data = bytearray(0)
        self.bin_size = bin_size
        self.padded = True

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
        if self.code_tache & 0x80:
            self.code_tache &= ~0x80
            self.delayed = True

    def to_bytes(self):
        # Compute length of the consigne
        length = self.CONSIGNE_HEADER_SIZE - 2 + len(self.ctx_data)
        if self.bin_size:
            # If size of the consigne is specified (and big enough), use it
            if length < self.bin_size:
                length = self.bin_size
        if self.padded:
            if length % 4 != 0:
                length = int(length/4) + 1
            else:
                length = int(length/4)
            length *= 4 
        # Create consigne
        code_tache = self.code_tache
        if self.delayed:
            code_tache |= 0x80
        
        ret = bytearray(struct.pack(self.CONSIGNE_HEADER,
                        length,
                        self.dest,
                        code_tache,
                        self.code_app,
                        self.msg_len,
                        self.page,
                        self.msg_addr,
                        self.computer,
                        self.application))
        # Add context data
        ret.extend(self.ctx_data)
        # Insert padding
        rem_bytes = length - (len(ret) -2)
        ret.extend(bytearray(rem_bytes))
        return ret
    
    @staticmethod
    def get_enum_string(enum_def, value):
        if value in enum_def:
            return enum_def[value]
        return f'unknown ({value})'

    @staticmethod
    def get_code_task_string(value):
        return Consigne.get_enum_string(Consigne.TASK_CODE, value)

    @staticmethod
    def get_computer_string(value):
        return Consigne.get_enum_string(Consigne.COMPUTER, value)

    @staticmethod
    def get_application_string(value):
        return Consigne.get_enum_string(Consigne.APPLICATION, value)

    def __str__(self):
        return f'Consigne : Tache {self.get_code_task_string(self.code_tache)} {self.delayed and "Delayed" or ""}, app {self.code_app}, msg_len {self.msg_len}, page {self.page}, addr ${self.msg_addr:04x}'

