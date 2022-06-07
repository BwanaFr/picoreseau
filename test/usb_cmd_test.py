from socket import timeout
import usb.core
import usb.util
from picoreseau.consigne import Consigne
from picoreseau.usb import USBCommand
import time
import struct

if __name__ == "__main__":   
    # find our device
    dev = usb.core.find(idVendor=0xbaba, idProduct=0x0001)

    # was it found?
    if dev is None:
        raise ValueError('Device not found')

    # set the active configuration. With no arguments, the first
    # configuration will be the active one
    dev.set_configuration()

    # get an endpoint instance
    cfg = dev.get_active_configuration()
    intf = cfg[(0,0)]

    ep_out = usb.util.find_descriptor(
        intf,
        # match the first OUT endpoint
        custom_match = \
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_OUT)

    ep_in = usb.util.find_descriptor(
        intf,
        # match the first OUT endpoint
        custom_match = \
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_IN)


    assert ep_out is not None
    assert ep_in is not None

    cmd = USBCommand(get_status=True)
    dev.write(0x3, cmd.to_bytes())
    status_data = bytearray(struct.calcsize('BB60c'))
    print(f'Status buffer len is {len(status_data)}')
    c = ep_in.read(len(status_data))
    print(c)
    status_code, error_code, error_msg = struct.unpack('BB60s', c)
    print(f'Status : {status_code}, Error #{error_code} : {error_msg.decode()}')
    # data = bytearray(0xffff)
    # for i in range(len(data)):
    #     data[i] = i & 0xff
    # cmd = USBCommand(tx_data=data)
    # ep.write(cmd.to_bytes())
    # ep.write(data)
    # time.sleep(0.2)
    # c = Consigne()
    # c.code_tache = 9
    # c.code_app = 32
    # c.dest = 2
    # cmd = USBCommand(consigne=c)
    # ep.write(cmd.to_bytes())
