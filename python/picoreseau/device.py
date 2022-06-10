import logging
import threading
import ctypes
from time import sleep
from queue import Queue
import struct
import usb.core
import usb.util
from .consigne import Consigne
from .usb import USBCommand

__EP_OUT__ = 0x3    # Picoreseau data USB endpoint out
__EP_IN__ = 0x83    # Picoreseau data USB endpoint in

class PicoreseauDevice(threading.Thread):

    logger = logging.getLogger("PicoreseauDevice")
    """
        Class representing the Picoreseau device
    """
    def __init__(self, usb_device, poll_interval=5):
        """
            Class constructor
            
            Parameters
            ----------
            usb_device:
                USB device object
            status_changed_cb:
                Callback function for change of status
            error_changed_cb:
                Callback function for change of error
            poll_interval:
                Device status polling interval in ms
        """
        super().__init__(daemon=False)
        self.stopThread = False
        self.device = usb_device
        self.__status_cb__ = None
        self.__error_cb__ = None
        self.poll_interval = poll_interval/1000
        self.__msg_queue__ = Queue()

    def register_callback(self, status_changed_cb=None, error_change_cb=None):
        """
            Register callback to be called when the device state changes

            Parameters
            ----------
            status_changed_cb:
                Callback function for change of status
            error_changed_cb:
                Callback function for change of error
        """
        self.__status_cb__ = status_changed_cb
        self.__error_cb__ = error_change_cb

    def stop(self):
        thread_id = self.get_id()
        res = ctypes.pythonapi.PyThreadState_SetAsyncExc(thread_id,
              ctypes.py_object(SystemExit))
        if res > 1:
            ctypes.pythonapi.PyThreadState_SetAsyncExc(thread_id, 0)
            print('Exception raise failure')

    def get_id(self):
        # returns id of the respective thread
        if hasattr(self, '_thread_id'):
            return self._thread_id
        for id, thread in threading._active.items():
            if thread is self:
                return id

    def run(self):
        """
            Thread method
        """
        last_status = None
        last_error = None
        status = DeviceStatus(self.device)

        while True:
            sleep(self.poll_interval)
            status_code, error_code, error_msg = status.get_state()
            if status_code != last_status:
                last_status = status_code
                self.logger.debug(f'New status : {str(status)}')
                if status_code == 0:    
                    sleep(1)
                    # Test, send consigne
                    # c = Consigne()
                    # c.dest = 2
                    # t = bytearray(41)
                    # for i in range(len(t)):
                    #     t[i] = i
                    # c.ctx_data = t
                    # self.send_consigne(c, 1)
                if self.__status_cb__:
                    self.__status_cb__(status_code, error_code, error_msg)
                if status_code == 1:
                    consigne, peer, msg_num = status.get_consigne()
                    self.logger.debug(f'New command from {peer} msg #{msg_num}: {str(consigne)}')
                    self.logger.debug(consigne.ctx_data)

            if error_code != last_error:
                last_error = error_code
                self.logger.debug(f'New error : {str(status)}')
                if self.__error_cb__:
                    self.__error_cb__(status_code, error_code, error_msg)

    def disconnect_peer(self, peer, msg_num):
        """
            Sends a request to disconnect specific peer
        """
        self.logger.debug(f'Disconnecting peer #{peer}')
        cmd = USBCommand(disconnect=peer, msg_num=msg_num)
        self.device.write(__EP_OUT__, cmd.to_bytes())
            
    def send_consigne(self, consigne, msg_num):
        """
            Sends a consigne to a device
            peer identifier is already in the consigne object

            Parameters
            ----------
            consigne: Consigne
                Nanoreseau consigne to send on the network
            msg_num: int
                Message number in the network frame
        """
        self.logger.debug(f'Sending consigne : {str(consigne)}')
        cmd = USBCommand(consigne=consigne, msg_num=msg_num)
        self.device.write(__EP_OUT__, cmd.to_bytes())

    @staticmethod
    def detect_device():
        """
            This method detects a picoreseau USB device

            Returns
            -------
            PicoreseauDevice instance or None if no USB device found
        """
        # find our device
        dev = usb.core.find(idVendor=0xbaba, idProduct=0x0001)
        if dev is None:
            PicoreseauDevice.logger.error('No USB device found')
            return None
        PicoreseauDevice.logger.info(f'Found device {dev.product}')
        # set the active configuration. With no arguments, the first
        # configuration will be the active one
        dev.set_configuration()
        return PicoreseauDevice(dev)

class DeviceStatus:
    """
        This class represents the status of the picoreseau
        device
    """   
    DEVICE_STATE = {
        0 : "idle",
        1 : "selected",
        2 : "send_consigne",
        3 : "send_data",
        4 : "get_data",
        5 : "disconnect"
    }
    
    __STATE_REPLY_STRUCT__ = 'BB60s'    # State reply structure definition
    __STATE_REPLY_STRUCT_LEN_ = struct.calcsize(__STATE_REPLY_STRUCT__)
    __CONSIGNE_HEADER_REPLY__ = 'BB'    # Consigne reply header
    __CONSIGNE_HEADER_REPLY_LEN_ = struct.calcsize(__CONSIGNE_HEADER_REPLY__)

    logger = logging.getLogger("DeviceStatus")

    def __init__(self, device):
        """
            Constructor

            Parameters
            ----------
            device:
                USB device object to read/write data
            
            Attributes
            ----------
                status_code: int
                    Latest status code read
                error_code: int
                    Latest error code read
                error_msg: str
                    Latest error message read
        """        
        self.dev = device
        self.status_code = None
        self.error_code = None
        self.error_msg = None

    def get_state(self):
        """
            Sends a read request to MCU and read-back status

            Returns
            -------
            Status code, error code and error message
        """
        cmd = USBCommand(get_status=True)
        self.dev.write(__EP_OUT__, cmd.to_bytes())
        c = self.dev.read(__EP_IN__, DeviceStatus.__STATE_REPLY_STRUCT_LEN_)
        self.status_code, self.error_code, error_msg = struct.unpack(DeviceStatus.__STATE_REPLY_STRUCT__, c)
        self.error_msg = error_msg.decode()
        return self.status_code, self.error_code, self.error_msg

    def get_consigne(self):
        """
            Sends a request to get the current consigne

            Returns
            -------
            Current consigne, peer id and message number
        """
        cmd = USBCommand(get_consigne=True)
        self.dev.write(__EP_OUT__, cmd.to_bytes())
        total_bytes = DeviceStatus.__CONSIGNE_HEADER_REPLY_LEN_ + Consigne.CONSIGNE_SIZE
        self.logger.debug(f'Reading {total_bytes} for consigne')
        c = self.dev.read(__EP_IN__, total_bytes)
        peer, exchange_num = struct.unpack_from(DeviceStatus.__CONSIGNE_HEADER_REPLY__, c)
        ret = Consigne(c[2:])
        return ret, peer, exchange_num

    def get_state_string(self):
        if self.status_code in DeviceStatus.DEVICE_STATE:
            return DeviceStatus.DEVICE_STATE[self.status_code]
        return f'unknown ({self.status_code})'

    def __str__(self):
        return f'DeviceStatus : {self.get_state_string()}, Error #{self.error_code} : {self.error_msg}'


if __name__ == "__main__":
    logging.basicConfig()
    logging.getLogger().setLevel(logging.DEBUG)
    dev = PicoreseauDevice.detect_device()
    dev.start()
    try:
        while True:
            sleep(1)
    except KeyboardInterrupt:
        dev.stop()