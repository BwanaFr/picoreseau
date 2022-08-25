from genericpath import isfile
from picoreseau import files
from picoreseau.device import PicoreseauDevice
from picoreseau.device import DeviceStatus
from picoreseau.consigne import Consigne
import sys
import time
import logging
from pathlib import Path
import struct

class Station:
    """
        This class represents a nanoreseau station
    """
    def __init__(self, station_id, computer):
        self.id = station_id
        self.computer = computer

class Server:
    logger = logging.getLogger("Server")

    """
        The nanoreseau master
    """
    def _handle_init_call(self, consigne, station):
        """
            Handles the initial call on a station
        """
        station_sig = consigne.ctx_data[:32]
        signature = ''.join(f'{letter:02x}' for letter in station_sig)
        self.logger.info(f'Looking for signature {signature}')
        if signature in self.cfg_file.identifiers:
            load_file = self.cfg_file.identifiers[signature]
            bin_file_path = Path(self.base_path).joinpath(load_file.get_drive_name()).joinpath(load_file.get_file_name())        
            bin_file = files.NanoreseauFile(bin_file_path)
            #TODO: Change code depending on target (MO5, TO7)
            self.send_execute_code_request(station,  b'\x34\x52\x11\x8C\x20\x80\x23\x06\x11\x8C\x20\xCC\x23\x14\x1A\xFF\xCE\x20\xAC\x86\x10\xAE\xE1\xAF\xC1\x4A\x26\xF9\x10\xCE\x20\xAC\x1C\x00\x35\xD2')
            # Send where to download the file
            self.send_new_address(bin_file.binary_data.bin_code[0].code_address, len(bin_file.binary_data.bin_code[0].data), bin_file.binary_data.bin_code[0].code_page, station)
            # Send file to computer
            self.device.send_data(bin_file.binary_data.bin_code[0].data, station.id)
            # Send code to computer in order to launch the MENU (last two bytes are the address specified in bin_file.binary_data.bin_code[0].code_address)
            self.send_execute_code_request(station,  b'\x10\xCE\x20\xCC\x7E\x50\x00', True)
            self.device.disconnect_peer(station.id)
        else:
            self.logger.info(f'Station identifier {signature} not found in configuration file')
            self.device.disconnect_peer(station.id)
        
    # Maps of tasks
    SERVER_TASKS = {
        Consigne.TC_INIT_CALL : {
            0 : (_handle_init_call, 'INIT', 'Initial call'),
        },
        Consigne.TC_FILE : {
            # TODO : Handle file requests here
            36 : (None, 'OUVFL', 'Open file for reading'),
            37 : (None, 'OUVFE', 'Open file for writing'),
            40 : (None, 'LIRFI', 'Read from file'),
            41 : (None, 'ECRFI', 'Write to file'),
            42 : (None, 'FERFI', 'Close file'),
            39 : (None, 'CREFI', 'Create file'),
            45 : (None, 'SUPFI', 'Delete file'),
            46 : (None, 'RENFI', 'Rename file'),
            43 : (None, 'RESFI', 'Reserve file'),
            44 : (None, 'RELFI', 'Release file'),
            47 : (None, 'COPIE', 'Copy file'),
            34 : (None, 'CATP', 'File catalog'),
            35 : (None, 'CATS', 'File catalog'),
            32 : (None, 'DATE', 'Request date and time'),
            33 : (None, 'ID', 'Declare an identifier to server'),
            48 : (None, 'IMPRIM', 'Use server printer'),
            49 : (None, 'SYSINF', 'Request system informations'),
            50 : (None, 'SYSTEM', 'Back to operating system'),
            51 : (None, 'DSKF', 'Request free space on a server disk'),
            52 : (None, 'LIRATT', 'Get file attributes'),
            53 : (None, 'ECRATT', 'Write file attributes'),
            54 : (None, 'SETMODE', 'Sets file creation mode'),
            55 : (None, 'CHBIN', 'Load binary file'),
            56 : (None, 'CLEAR', 'Clean slave descriptor on server'),
            57 : (None, 'RBUFF', 'Read common buffer'),
            58 : (None, 'WBUFF', 'Write common buffer'),
            65 : (None, 'CHAENR', 'Load an indexed file record'),
            64 : (None, 'GARENR', 'Park an indexed file record'),
            66 : (None, 'SUPENR', 'Delete an indexed file record'),
            67 : (None, 'RESENR', 'Reserve an indexed file record'),
            68 : (None, 'RELENR', 'Release an indexed file record'),
            69 : (None, 'NES', 'Request next indexed file record number'),
            70 : (None, 'DNE', 'Request last indexed file record number'),
             1 : (None, 'PROG', 'Exchange program'),
            16 : (None, 'TELE', 'Download program'),
        },
    }

    def __init__(self, base_path):
        """
            NR server constructor

            Parameters:
            -----------
            base_path : str
                Base path containing NR disk folders (typically A and B)            
        """
        self.base_path = base_path
        self.device = None
        self.cfg_file = None
        self.on_line_sta = {}

    @staticmethod
    def pad_file_name(name, length):
        """
            Pads a file name with spaces
        """
        ret = name
        for i in range(len(name), length):
            ret += ' '
        return ret

    def send_report(self, data, station):
        """
            Send report (compte-rendu) to specified station

            Parameters:
            -----------
            data: bytes
                Report data
            station: Station
                Targeted station
        """
        cons = Consigne()
        cons.dest = station.id
        cons.computer = station.computer
        cons.code_tache = Consigne.TC_COPY_REPORT
        cons.ctx_data = data
        self.device.send_consigne(cons)

    def send_new_address(self, address, lenght, page, station):
        cons = Consigne()
        cons.dest = station.id
        cons.computer = station.computer
        cons.code_tache = Consigne.TC_INIT_CALL
        cons.msg_addr = address
        cons.msg_len = lenght
        cons.page = page
        self.device.send_consigne(cons)

    def send_execute_code_request(self, station, code, delayed=False):
        """
            Sends a execute code request to specified station
        """
        cons = Consigne(60)
        cons.dest = station.id
        cons.computer = station.computer
        cons.delayed = delayed
        cons.code_tache = Consigne.TC_EXEC_CODE
        cons.ctx_data = code
        self.device.send_consigne(cons)

    def send_binary_file(self, file, station):
        """
            Sends a binary file to specified station
        """
        bin_file_path = Path(self.base_path).joinpath(file.get_drive_name()).joinpath(file.get_file_name())        
        bin_file = files.NanoreseauFile(bin_file_path)
        self.logger.info(f'Will send the file {str(bin_file)}')
        if not bin_file.binary_data and type(bin_file.binary_data) != files.BinaryData:
            self.logger.error(f'File {bin_file.identifier} is not a valid binary file!')
            return
        bin_data = bin_file.binary_data
        for c in bin_data.bin_code:
            # Built a CHBIN consigne
            cons = Consigne()
            cons.dest = station.id
            cons.code_tache = Consigne.TC_FILE
            cons.code_app = 55  #CHBIN
            cons.msg_len = len(c.data)
            cons.page = c.code_page
            cons.msg_addr = c.code_address
            cons.computer = bin_data.machine_type
            cons.application = 1 #bin_data.code_language
            cons.ctx_data = struct.pack('>B8s3sB', 
                file.drive, Server.pad_file_name(file.file_name, 8).encode('utf-8'), Server.pad_file_name(file.extension, 3).encode('utf-8'), 0 #bin_data.loading_byte
            )
            self.device.send_consigne(cons)      

    def init_server(self):
        """
            Initializes the server.
            Loads the configuration file and detects USB device
        """
        #Loads nr3.dat configuration file
        nr3_dat_file = None
        a_folder = Path(self.base_path).joinpath('A')
        if not a_folder.is_dir():
            raise Exception(f'Unable to find A folder in {self.base_path}')
        if a_folder.joinpath("NR3.DAT").is_file():
            nr3_dat_file = a_folder.joinpath("NR3.DAT")
            
        b_folder = Path(self.base_path).joinpath('B')
        if not b_folder.is_dir():
            self.logger.info(f'Unable to find B folder in {self.base_path}')
        elif not nr3_dat_file and b_folder.joinpath("NR3.DAT").is_file():
            nr3_dat_file = b_folder.joinpath("NR3.DAT")            
        
        if not nr3_dat_file:
            raise Exception("NR3.DAT file not found, can't continue")

        self.logger.info(f'Found configuration file at {nr3_dat_file}')
        self.cfg_file = files.NRConfigurationFile(nr3_dat_file)

        #Opens USB device
        self.logger.info("Detecting picoreseau USB device...")
        self.device = PicoreseauDevice.detect_device()
        if not self.device:
            raise Exception("No USB picoreseau device found!")
        self.logger.info("USB picoreseau found! Server is ready...")


    def run_server(self):
        """
            Runs the server
        """
        if not self.cfg_file or not self.device:
            raise Exception("Server not initialized!")
        #Server loop, see what we do in it...
        while True:
            status = self.device.wait_new_status()
            self.logger.info(f'New state : {str(status)}')
            if status.event == DeviceStatus.EVT_SELECTED:
                self.logger.info('Device selected, receiving consigne')
                c, station_num = self.device.get_consigne()
                self.logger.info(f'Received consigne {str(c)} from {station_num}')
                if not station_num in self.on_line_sta:
                    self.logger.info(f'Discovered new station #{station_num}')
                    station = Station(station_num, c.computer)
                    self.on_line_sta[station_num] = station
                else:
                    station = self.on_line_sta[station_num]
                # Try to find a task to be executed for this consigne
                if not c.code_tache in self.SERVER_TASKS:
                    self.logger.error(f'Unsupported task code : {c.code_tache}. Disconnecting peer.')
                    self.device.disconnect_peer(station.id)
                    continue                    
                tasks = self.SERVER_TASKS[c.code_tache]
                task_name = Consigne.get_code_task_string(c.code_tache)
                if not c.code_app in tasks:
                    self.logger.error(f'Unsupported application code {c.code_app} for task {task_name}. Disconnecting peer.')
                    self.device.disconnect_peer(station.id)
                    continue
                task = tasks[c.code_app]
                if not task[0]:
                    self.logger.error(f'No handler defined for task {task[1]}/{task[2]}. Disconnecting peer.')
                    self.device.disconnect_peer(station.id)
                    continue
                # Call the handler for this task
                task[0](self, c, station)


if __name__ == "__main__":
    logging.basicConfig()
    logging.getLogger().setLevel(logging.DEBUG)
    if len(sys.argv) > 1:
        print("Starting NR server")
        server = Server(sys.argv[1])
        server.init_server()
        server.run_server()
    else:
        print("Please provide a folder containing A and B subfolders!")