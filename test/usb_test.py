from socket import timeout
import usb.core
import usb.util

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
            usb.util.ENDPOINT_IN)

    assert ep is not None
    while(True):
        try:
            data = ep.read(125, timeout = 100)
            print(data)
        except usb.core.USBTimeoutError as e:
            print('.', end=None)
            pass