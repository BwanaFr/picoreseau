from .consigne import Consigne
import struct

class USBCommand:
    """
    Class representing USB commands
    """
    CMD_GET_STATUS = 0
    CMD_GET_CONSIGNE = 1
    CMD_PUT_CONSIGNE = 2
    CMD_GET_DATA = 3
    CMD_PUT_DATA = 4
    CMD_DISCONNECT = 5

    def __init__(self, **kwargs):
        self.payload = None
        self.cmd = None
        if "get_status" in kwargs:
            self.cmd = USBCommand.CMD_GET_STATUS
        if "get_consigne" in kwargs:
            self.cmd = USBCommand.CMD_GET_CONSIGNE
        if "consigne" in kwargs:
            self.cmd = USBCommand.CMD_PUT_CONSIGNE
            msg_num = 0
            if "msg_num" in kwargs:
                msg_num = kwargs["msg_num"]
            self.payload = bytearray(struct.pack('B', msg_num))
            self.payload.extend(kwargs["consigne"].to_bytes())
        if "tx_data" in kwargs:
            # Send data to nanoreseau
            self.cmd = USBCommand.CMD_PUT_DATA
            self.payload = struct.pack('<H', len(kwargs["tx_data"]))
        if "rx_data" in kwargs:
            # Send data to nanoreseau
            self.cmd = USBCommand.CMD_GET_DATA
            self.payload = struct.pack('<H', len(kwargs["rx_data"]))
        if "disconnect" in kwargs:
            # Send data to nanoreseau
            self.cmd = USBCommand.CMD_DISCONNECT
            msg_num = 0
            if "msg_num" in kwargs:
                msg_num = kwargs["msg_num"]
            self.payload = struct.pack('BB', kwargs["disconnect"], msg_num)
    
    def to_bytes(self):
        ret = None
        ret = bytearray(struct.pack('B', self.cmd))
        if self.payload:
            ret.extend(self.payload)
        return ret

