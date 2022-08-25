import logging
import numbers
from pydoc import doc
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


# Things to change/do:
# - Add concept of session to handle message numbers (should be done in MCU)
# - Maybe remove the thread in PicoreseauDevice
# - Add an acknowledge mechanism for all commands send to MCU (to retrieve next msg_num)


class PicoreseauDevice(threading.Thread):


    __STATE_REPLY_STRUCT__ = 'BBB60s'    # State reply structure definition
    # typedef struct USB_STATUS_OUT {
    #     uint8_t state;              // Device state
    #     uint8_t error;              // Error code
    #     uint8_t event;              // Event signal
    #     char errorMsg[60];          // Error message
    # }USB_STATUS_OUT;
    __STATE_REPLY_STRUCT_LEN_ = struct.calcsize(__STATE_REPLY_STRUCT__)
    __CONSIGNE_HEADER_REPLY__ = 'B'    # Consigne reply header
    __CONSIGNE_HEADER_REPLY_LEN_ = struct.calcsize(__CONSIGNE_HEADER_REPLY__)

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
            poll_interval:
                Device status polling interval in ms
        """
        super().__init__(daemon=False)
        self.stopThread = False
        self.device = usb_device
        self.__status_cb__ = None
        self.poll_interval = poll_interval/1000
        self.__msg_queue__ = Queue()

    def register_callback(self, status_changed_cb=None):
        """
            Register callback to be called when the device state changes

            Parameters
            ----------
            status_changed_cb:
                Callback function for change of status            
        """
        self.__status_cb__ = status_changed_cb

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
        
        while True:
            sleep(self.poll_interval)
            status = self.get_state()
            if status.status_code != last_status or status.error_code != last_error \
                or status.event != DeviceStatus.EVT_NONE:
                last_status = status.status_code
                last_error = status.error_code
                self.logger.debug(f'New status : {str(status)}')
                if self.__status_cb__:
                    self.__status_cb__(self, status)

    def disconnect_peer(self, peer):
        """
            Sends a request to disconnect specific peer
        """
        self.logger.debug(f'Disconnecting peer #{peer}')
        cmd = USBCommand(disconnect=peer)
        self.device.write(__EP_OUT__, cmd.to_bytes())
            
    def send_consigne(self, consigne):
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
        c_bin = ''.join(f'{letter:02x}' for letter in consigne.to_bytes())
        self.logger.debug(f'Sending consigne : {c_bin}')
        cmd = USBCommand(consigne=consigne)
        self.device.write(__EP_OUT__, cmd.to_bytes())
        # TODO: Wait for end of transmission on bus and check errors

    def send_data(self, data, dest, msg_num):
        """
            Sends binary data to a device (vas-y recois)

            Parameters
            ----------
            data : bytes
                Binary data to be send
            dest : int
                Slave address to send data to
            msg_num : int
                Message number
        """
        pass

    def get_state(self):
        """
            Sends a read request to MCU and read-back status

            Returns
            -------
            DeviceStatus containing status code, error code and error message
        """
        cmd = USBCommand(get_status=True)
        self.device.write(__EP_OUT__, cmd.to_bytes())
        c = self.device.read(__EP_IN__, self.__STATE_REPLY_STRUCT_LEN_)
        self.status_code, self.error_code, event, error_msg = struct.unpack(self.__STATE_REPLY_STRUCT__, c)
        self.error_msg = error_msg.decode()
        return DeviceStatus(self.status_code, self.error_code, event, self.error_msg)

    def get_consigne(self):
        """
            Sends a request to get the current consigne

            Returns
            -------
            Current consigne, peer id and message number
        """
        cmd = USBCommand(get_consigne=True)
        self.device.write(__EP_OUT__, cmd.to_bytes())
        total_bytes = self.__CONSIGNE_HEADER_REPLY_LEN_ + Consigne.CONSIGNE_SIZE
        self.logger.debug(f'Reading {total_bytes} for consigne')
        c = self.device.read(__EP_IN__, total_bytes)
        peer = struct.unpack_from(self.__CONSIGNE_HEADER_REPLY__, c)
        ret = Consigne()
        ret.from_bytes(c[1:])
        return ret, peer[0]

    def reset_device_state(self):
        """
            Resets MCU internal states
        """
        self.device.ctrl_transfer(2 << 5 | 1, 1, data_or_wLength = bytes())

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
        return PicoreseauDevice(dev, 0)


class DeviceStatus:
    """
        This class represents the status of the picoreseau
        device
    """   
    STATE_IDLE = 0
    STATE_RDV_INIT_CALL = 1
    STATE_BUSY = 2

    DEVICE_STATE = {
        STATE_IDLE : "idle",
        STATE_RDV_INIT_CALL : "receiving_initial_call",
        STATE_BUSY : "busy",
    }

    EVT_NONE = 0
    EVT_ERROR = 1
    EVT_SELECTED = 2
    EVT_CMD_DONE = 3

    EVENT_TYPE = {
        EVT_NONE: "none",
        EVT_ERROR: "error",
        EVT_SELECTED: "selected",
        EVT_CMD_DONE: "cmd_done",
    }


    logger = logging.getLogger("DeviceStatus")

    def __init__(self, status_code, error_code, event, error_msg):
        """
            Constructor

            Parameters
            ----------
            status_code:
                Picoreseau device status code
            error_code:
                Picoreseau device error code
            event:
                Event identifier
            error_msg:
                Picoreseau device error message
            
            Attributes
            ----------
                status_code: int
                    Latest status code read
                error_code: int
                    Latest error code read
                event: int
                    Type of event
                error_msg: str
                    Latest error message read
        """        
        self.status_code = status_code
        self.error_code = error_code
        self.event = event
        self.error_msg = error_msg

    def get_state_string(self):
        if self.status_code in DeviceStatus.DEVICE_STATE:
            return DeviceStatus.DEVICE_STATE[self.status_code]
        return f'unknown ({self.status_code})'
    
    def get_event_string(self):
        if self.event in DeviceStatus.EVENT_TYPE:
            return DeviceStatus.EVENT_TYPE[self.event]
        return f'unknown ({self.event})'

    def __str__(self):
        return f'DeviceStatus : {self.get_state_string()}, Event : {self.get_event_string()}, Error #{self.error_code} : {self.error_msg}'


#Testing
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