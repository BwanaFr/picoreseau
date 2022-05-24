from socket import timeout
import usb.core
import usb.util
from picoreseau.consigne import Consigne
from picoreseau.usb import USBCommand

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

    ep = usb.util.find_descriptor(
        intf,
        # match the first OUT endpoint
        custom_match = \
        lambda e: \
            usb.util.endpoint_direction(e.bEndpointAddress) == \
            usb.util.ENDPOINT_OUT)

    assert ep is not None
    c = Consigne()
    c.code_tache = 9
    c.code_app = 32
    c.dest = 2
    cmd = USBCommand(consigne=c)
    ep.write(cmd.to_bytes())