from genericpath import isfile
from picoreseau import files
from picoreseau import device
import sys
import time
import logging
from pathlib import Path

#Configuration file
cfg_file = None

#Online stations
on_line_sta = {}

class Station:
    """
        This class represents a nanoreseau station
    """
    def __init__(self):
        self.waiting = False

# Status callback of picoreseau device
def dev_status_cb(status_code, error_code, error_msg):
    print(f'Status : {status_code}')

# Error callback of picoreseau device
def dev_error_cb(status_code, error_code, error_msg):
    print(f'Error : {error_msg}')

# Server loop method
def server(cfg_file_path):
    global cfg_file
    #Loads nr3.dat configuration file
    nr3_dat_file = None
    a_folder = Path(cfg_file_path).joinpath('A')
    if not a_folder.is_dir():
        print(f'Unable to find A folder in {cfg_file_path}')
        return
    if a_folder.joinpath("NR3.DAT").is_file():
        nr3_dat_file = a_folder.joinpath("NR3.DAT")

    b_folder = Path(cfg_file_path).joinpath('B')
    if not b_folder.is_dir():
        print(f'Unable to find B folder in {cfg_file_path}')
    if not nr3_dat_file and b_folder.joinpath("NR3.DAT").is_file():
        nr3_dat_file = b_folder.joinpath("NR3.DAT")
    
    if not nr3_dat_file:
        print("NR3.DAT file not found, can't continue")
        return
    cfg_file = files.NRConfigurationFile(nr3_dat_file)
    print(str(cfg_file))

    #Opens USB device
    print("Detecting picoreseau USB device...")
    usb_device = device.PicoreseauDevice.detect_device()
    if not usb_device:
        print("No USB picoreseau device found!")
        exit()
    usb_device.register_callback(dev_status_cb, dev_error_cb)

    usb_device.start()
    print("USB picoreseau found! Server is running...")

    #Server loop, see what we do in it...
    try:
        while True:
            time.sleep(1)
    except:
        pass
    usb_device.stop()

if __name__ == "__main__":
    logging.basicConfig()
    logging.getLogger().setLevel(logging.DEBUG)
    if len(sys.argv) > 1:
        print("Starting NR server")
        server(sys.argv[1])
    else:
        print("Please provide a folder containing A and B subfolders!")