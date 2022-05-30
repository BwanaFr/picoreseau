import struct

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

    CONSIGNE_HEADER = '>BBBHBHBB'
    CONSIGNE_HEADER_SIZE = struct.calcsize(CONSIGNE_HEADER)

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
        self.length = struct.unpack_from('>B', b)
        vals = struct.unpack_from(self.CONSIGNE_HEADER, b, 1)
        self.dest = vals[0]
        self.code_tache = vals[1]
        self.code_app = vals[2]
        self.msg_len = vals[3]
        self.page = vals[4]
        self.msg_addr = vals[5]
        self.computer = vals[6]
        self.application = vals[7]
        self.ctx_data = b[self.CONSIGNE_HEADER_SIZE + 1:]

    def to_bytes(self):
        # Consigne length is always a multiple of 4
        length = (((self.CONSIGNE_HEADER_SIZE + len(self.ctx_data)) % 4) + 1) * 4
        ret = bytearray(length + 1)             # Adds one byte for consigne length
        struct.pack_into('>B', ret, 0, length)  # Sets lenght of consigne
        print(f'Consigne len : {length}')
        # Append consigne structure to our bytearray
        struct.pack_into(self.CONSIGNE_HEADER, ret, 1,
                        self.dest,
                        self.code_tache,
                        self.code_app,
                        self.msg_len,
                        self.page,
                        self.msg_addr,
                        self.computer,
                        self.application)
        ctx_offset = self.CONSIGNE_HEADER_SIZE + 1
        # Insert context data in the buffer
        ret[ctx_offset:ctx_offset+len(self.ctx_data)] = self.ctx_data
        return ret
    
    def __str__(self):
        return f'Consigne : Tache {self.code_tache}, app {self.code_app}, msg_len {self.msg_len}, page {self.page}, addr {self.msg_addr}'

