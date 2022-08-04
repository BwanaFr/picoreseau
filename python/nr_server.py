from genericpath import isfile
from picoreseau import files
from picoreseau.device import PicoreseauDevice
from picoreseau.device import DeviceStatus
from picoreseau.consigne import Consigne
import sys
import time
import logging
from pathlib import Path

#Configuration file
cfg_file = None

#Base folder path
base_path = None

#Online stations
on_line_sta = {}

class Station:
    """
        This class represents a nanoreseau station
    """
    def __init__(self, station_id):
        self.id = station_id
        self.waiting = False
        self.msg_num = 0

    def next_message_number(self):
        self.msg_num += 1
        return self.msg_num
    
def get_bin_file_consigne(file, dest):
    c = Consigne()
    c.dest = dest.id
    c.code_tache = Consigne.TC_FILE
    c.code_app = 55
    bin_code = file.binary_data.bin_code[0]
    c.msg_len = len(bin_code.data)
    c.page = int(bin_code.code_page)
    c.msg_addr = int(bin_code.code_address)
    c.computer = int(file.binary_data.machine_type)
    c.application = int(file.binary_data.code_language)
    return c


# Handles initial call from station
def handle_initial_call(device, station, consigne):
    global cfg_file
    global base_path
    station_sig = consigne.ctx_data[:32]
    signature = ''.join(f'{letter:02x}' for letter in station_sig)
    print(f'Looking for signature {signature}')
    if signature in cfg_file.identifiers:
        load_file = cfg_file.identifiers[signature]        
        bin_file_path = Path(base_path).joinpath(load_file.drive).joinpath(load_file.file_name)        
        bin_file = files.NanoreseauFile(bin_file_path)
        print(f'Will send the file {str(bin_file)}')
        bin_cons = get_bin_file_consigne(bin_file, station)
        print(f'Consigne : {str(bin_cons)}')
        device.send_consigne(bin_cons, station.next_message_number())
    else:
        print(f'Station identifier {signature} not found in configuration file')
        device.disconnect_peer(station.id, station.next_message_number())

# Status callback of picoreseau device
def dev_status_cb(device, status):
    print(f'Status : {status}')
    if status.status_code == DeviceStatus.STATE_SELECTED:
        print('Device selected, receiving consigne')
        c, station_num, msg = device.get_consigne()
        print(f'Received consigne {str(c)} from {station_num} msg #{msg}')
        if not station_num in on_line_sta:
            station = Station(station_num)
        else:
            station = on_line_sta[station_num]
        station.msg_num = msg
        # Checks if it's an initial call
        # TODO : Make a dict with all possible task code
        if c.code_tache == 0 and c.code_app == 0:
            print(f'Appel initial de {station_num}')
            handle_initial_call(device, station, c)


# Server loop method
def server(cfg_file_path):
    global cfg_file
    global base_path
    base_path = cfg_file_path
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
    usb_device = PicoreseauDevice.detect_device()
    if not usb_device:
        print("No USB picoreseau device found!")
        exit()
    usb_device.register_callback(dev_status_cb)

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