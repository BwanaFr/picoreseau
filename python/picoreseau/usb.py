from .consigne import Consigne
import struct

class USBCommand:
    """
    Class representing USB commands
    """
    CMD_PUT_CONSIGNE = 0
    CMD_GET_DATA = 1
    CMD_PUT_DATA = 2
    CMD_DISCONNECT = 3

    def __init__(self, **kwargs):
        self.payload = None
        self.cmd = None
        if "consigne" in kwargs:
            self.cmd = USBCommand.CMD_PUT_CONSIGNE
            self.payload = kwargs["consigne"].to_bytes()
        if "tx_data" in kwargs:
            # Send data to nanoreseau
            self.cmd = USBCommand.CMD_PUT_DATA
            self.payload = struct.pack('>H', kwargs["tx_data"])
        if "rx_data" in kwargs:
            # Send data to nanoreseau
            self.cmd = USBCommand.CMD_GET_DATA
            self.payload = struct.pack('>H', kwargs["rx_data"])
        if "disconnect" in kwargs:
            # Send data to nanoreseau
            self.cmd = USBCommand.CMD_DISCONNECT
            self.payload = struct.pack('>B', kwargs["disconnect"])
    
    def to_bytes(self):
        ret = bytearray(struct.calcsize('>B') + len(self.payload))
        struct.pack_into('>B', ret, 0, self.cmd)
        ret[struct.calcsize('>B'):] = self.payload
        return ret