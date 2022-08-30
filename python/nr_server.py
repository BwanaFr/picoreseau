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
import re
import os
from enum import IntEnum
from datetime import datetime, timezone

class Station:
    """
        This class represents a nanoreseau station
    """
    def __init__(self, station_id):
        self.id = station_id            # Station address (1..31)
        self.computer = None            # Type of computer (None if offline)
        self.identifier = '        '    # Station identifier (received from ID CTA)
        self.open_files = []            # List of open files
        self.reserved_files = []        # List of reserved files
        self.file_listing = None        # Used for CATP/CATS
        self.file_listing_index = 0     # Actual catalog index

    def clean(self):
        """
            Cleans the station identifier
        """
        self.identifier = '        '    # Station identifier (received from ID CTA)
        self.open_files = []            # List of open files
        self.reserved_files = []        # List of reserved files
        self.file_listing = None        # Used for CATP/CATS
        self.file_listing_index = 0     # Actual catalog index

class NRErrors(IntEnum):
    """
        This enumeration contains all possible nanoreseau errors
    """
    BAD_DISK = 128
    FILE_NAME_SYNTAX_ERROR = 129
    BAD_IDENTIFICATION = 130
    TRANSMIT_ERROR = 131
    DUPLICATE_ID = 132
    END_OF_CATALOG = 133
    FILE_NOT_EXISTING = 134
    AMBIGOUS_FILE_NAME = 135
    WRONG_IDENTIFICATION = 137
    FILE_ALREADY_RESERVED = 138
    FILE_ALREADY_OPEN_FOR_WRITE = 139
    LOGIC_NUMBER_TABLE_SATURATED = 140
    FILE_TABLE_SATURATED = 141
    FILE_ALREADY_EXISTS = 142
    FILE_WITHOUT_NETWORK_HEADER = 143
    FILE_READ_ONLY = 144
    FILE_ALREADY_OPEN_FOR_READ = 145
    FILE_ALREADY_OPEN_FOR_WRITE_OTHER_STA = 146
    BAD_LOGIC_NUMBER = 147
    SERVER_DISK_EXCHANGED = 148
    END_OF_FILE = 149
    FILE_OPEN = 151
    FILE_NOT_INDEXED = 153
    NON_EXISTING_FUNCTION = 154
    SERVER_DISK_ERROR = 155
    RX_BUFFER_TOO_SMALL = 157
    REQUESTED_RX_LENGHT_TOO_LONG = 160
    SERVER_DISK_FULL = 161
    BAD_PARAMETERS = 162
    END_OF_SPOOL_FILE = 163
    SPOOL_FILE_NON_EXISTING = 164
    SATURATED_SPOOL = 165
    BINARY_FILE_LOAD_ERROR = 166
    PRINTER_NON_EXISTING = 167
    IO_DEVICE_NOT_OPEN = 169
    INDEXED_FILE_MAX_LENGHT = 180
    RECORD_NON_EXISTING = 181
    RESERVED_RECORD = 182
    TOO_MUCH_RECORDS = 183
    RESERVATION_TABLE_FULL = 184
    RECORD_OFFSET_TOO_BIG = 185
    FILE_PARTIALLY_RESERVED = 186


class Server:

    # Server default configuration
    SRV_VERSION_MAJOR = 3   # NR version 3.3
    SRV_VERSION_MINOR = 3
    OS_TYPE = 2             # Emulate a MS-DOS server


    logger = logging.getLogger("Server")

    """
        The nanoreseau master
    """
    def _handle_init_call(self, consigne, station_id):
        """
            Handles the initial call on a station
        """
        station_sig = consigne.ctx_data[:32]
        signature = ''.join(f'{letter:02x}' for letter in station_sig)
        self.logger.info(f'Looking for signature {signature}')
        if signature in self.cfg_file.identifiers:
            load_file = self.cfg_file.identifiers[signature]
            #TODO: Change code depending on target (MO5, TO7)
            self.send_execute_code_request(station_id,  b'\x34\x52\x11\x8C\x20\x80\x23\x06\x11\x8C\x20\xCC\x23\x14\x1A\xFF\xCE\x20\xAC\x86\x10\xAE\xE1\xAF\xC1\x4A\x26\xF9\x10\xCE\x20\xAC\x1C\x00\x35\xD2')
            # Download binary chuncks
            bin_file = self.send_binary_file(load_file, station_id)
            # Send code to computer in order to launch the MENU (last two bytes are the address specified in bin_file.binary_data.bin_code[0].code_address)
            # TODO: Put this code somewhere else
            #self.send_execute_code_request(station,  b'\x10\xCE\x20\xCC\x7E\x50\x00', True)
            exec_code = bytearray(b'\x10\xCE\x20\xCC\x7E\x50\x00')
            struct.pack_into('>H', exec_code, 5, bin_file.binary_data.exec_address)
            self.send_execute_code_request(station_id, exec_code, True)
            self.disconnect_station(station_id)
        else:
            self.logger.info(f'Station identifier {signature} not found in configuration file')
            self.disconnect_station(station_id)
        
    def _handle_chbin(self, consigne, station_id):
        """
            Handles the CHBIN (load binary file)
        """
        mode, = struct.unpack_from('>B', consigne.ctx_data, 12)
        nr_file = files.ApplicationFile(bytes(consigne.ctx_data[:12]))
        self.logger.info(f'CHBIN request from {station_id}: {str(nr_file)} mode : {mode}')
        self.send_execute_code_request(station_id,  b'\x34\x52\x11\x8C\x20\x80\x23\x06\x11\x8C\x20\xCC\x23\x14\x1A\xFF\xCE\x20\xAC\x86\x10\xAE\xE1\xAF\xC1\x4A\x26\xF9\x10\xCE\x20\xAC\x1C\x00\x35\xD2')
        bin_file = self.send_binary_file(nr_file, station_id)
        cpt_rendu = struct.pack('>BHB', 0, bin_file.binary_data.exec_address, bin_file.binary_data.exec_page)
        self.send_report(cpt_rendu, station_id, bin_file.binary_data.exec_address, bin_file.binary_data.exec_page)
        self.logger.info(f'CHBIN loaded file {str(nr_file)}')
        exec_code = bytearray(b'\x10\xCE\x20\xCC\x7E\x50\x00')
        struct.pack_into('>H', exec_code, 5, bin_file.binary_data.exec_address)
        self.send_execute_code_request(station_id, exec_code, False)
        self.disconnect_station(station_id)

    def _handle_clear(self, consigne, station_id):
        """
            Handles the CLEAR request
        """
        self.stations[station_id].clean()
        cpt_rendu = struct.pack('>B', 0)
        self.send_report(cpt_rendu, station_id)
        self.logger.info(f'CLEAN executed on station {station_id}')
        self.disconnect_station(station_id)

    def _handle_sysinf(self, consigne, station_id):
        """
            Handles the SYSINFO request
        """
        drives = files.NRConfigurationFile.get_available_drives(self.base_path)
        mask = 0
        for i in range(0, len(drives)):
            if drives[i]:
                mask |= 1 << i
        cpt_rendu = struct.pack('>BBBBHB', \
            0, #Null byte
            Server.SRV_VERSION_MAJOR,   # Nanoreseau major version
            Server.SRV_VERSION_MINOR,   # Nanoreseau minor version
            Server.OS_TYPE,             # Server operating system type
            mask,                       # Available disks on server
            0                           # System status (???)
        )
        self.send_report(cpt_rendu, station_id)
        self.logger.info(f'SYSINF executed on station {station_id}')
        self.disconnect_station(station_id)

    def _handle_catp(self, consigne, station_id):
        """
            Handles CATP request
        """
        file_filter = files.ApplicationFile(bytes(consigne.ctx_data[:12]))
        self.logger.info(f'CATP request from {station_id}: using filter {str(file_filter)}')
        # List all files matching the regex
        file_regex = file_filter.get_file_name().replace('?', '[a-zA-Z0-9\\s]')
        root_folder = Path(self.base_path).joinpath(file_filter.get_drive_name())
        self.stations[station_id].file_listing = []
        for f in os.scandir(root_folder):
            file_name = Server.pad_file_name(f.name, 12, False)
            if re.match(file_regex, file_name, re.IGNORECASE):
                self.stations[station_id].file_listing.append(f)
        self.stations[station_id].file_listing_index = 0
        self._send_catalog(consigne, station_id)

    def _handle_cats(self, consigne, station_id):
        """
            Handles CATS request
        """
        # Simply send the remaining catalog
        self._send_catalog(consigne, station_id)

    def _send_catalog(self, consigne, station_id):
        FILE_FORMAT = '>8s3sBBBBBB'
        FILE_FORMAT_SIZE = struct.calcsize(FILE_FORMAT)
        REPORT_FORMAT = '>BHB'
        buffer = None
        err_msg = 0
        nb_names = 0
        if consigne.msg_len < FILE_FORMAT_SIZE:
            # Not enough space for buffer
            err_msg = NRErrors.RX_BUFFER_TOO_SMALL            
        else:
            buffer = bytearray(consigne.msg_len)
            for i in range(self.stations[station_id].file_listing_index, len(self.stations[station_id].file_listing)):
                offset = nb_names*FILE_FORMAT_SIZE
                if offset <= (consigne.msg_len - FILE_FORMAT_SIZE):
                    # Enough space to add another file
                    nb_names += 1
                    file = self.stations[station_id].file_listing[i]
                    file_parts = file.name.rsplit('.', 1)
                    file_name = file_parts[0]
                    file_extension = ''
                    if len(file_parts) > 1:
                        file_extension = file_parts[1]
                    file_stat = file.stat()
                    modified = datetime.fromtimestamp(file_stat.st_mtime, tz=timezone.utc)                   
                    struct.pack_into(FILE_FORMAT, buffer, offset,
                        Server.pad_file_name(file_name, 8).encode('utf-8'),         # File name
                        Server.pad_file_name(file_extension, 3).encode('utf-8'),    # File extension
                        ((file_stat.st_size >> 16) & 0xFF),                         # File size (MSB, third byte)
                        ((file_stat.st_size >> 8) & 0xFF),                          # File size (middle byte)
                        ((file_stat.st_size) & 0xFF),                               # File size (LSB)
                        modified.day,                                               # Modification date
                        modified.month,                                             # Modification month
                        (modified.year % 100)                                       # Modification year
                    )
                    # Put bit 7 of first byte if it's a sub-catalog (a folder, I guess)
                    if file.is_dir():
                        buffer[offset] |= 0x80
                else:
                    break
            # Do we reach the end of the catalog?
            self.stations[station_id].file_listing_index += nb_names
            if (self.stations[station_id].file_listing_index + 1) >= len(self.stations[station_id].file_listing):
                # End of catalog reached
                err_msg = NRErrors.END_OF_CATALOG

        # Send the buffer to give names to the computer
        if buffer:
            buf_len = nb_names*FILE_FORMAT_SIZE
            self.send_new_address(consigne.msg_addr, buf_len, 0, station_id)
            self.device.send_data(buffer[:buf_len], station_id)

        # Prepare the report
        os_type = 0
        if Server.OS_TYPE == 2:
            os_type = 1
        cpt_rendu = struct.pack(REPORT_FORMAT, 
            err_msg,
            nb_names,
            os_type
        )
        self.send_report(cpt_rendu, station_id, delayed=True)
        self.disconnect_station(station_id)


    def _handle_date(self, consigne, station_id):
        self.logger.info(f'DATE request from {station_id}')
        DATE_FORMAT = '>BBBBBBBB'
        now = datetime.now()
        cpt_rendu = struct.pack(DATE_FORMAT, 
            0,                              # Null byte
            now.day,                        # Day
            now.month,                      # Month
            (now.year % 100),               # Year
            now.hour,                       # Hour
            now.minute,                     # Minutes
            now.second,                     # Seconds
            int(now.microsecond/1000000)    # 1/10 seconds
        )
        self.send_report(cpt_rendu, station_id, delayed=False)
        self.disconnect_station(station_id)

    def _handle_dskf(self, consigne, station_id):
        self.logger.info(f'DSKF request from {station_id}')
        REPLY_FORMAT = '>BH'
        drives = files.NRConfigurationFile.get_available_drives(self.base_path)
        disk = consigne.ctx_data[0]
        self.logger.info(f'Getting free space on drive {disk}')
        error = 0
        if not drives[disk]:
            error = NRErrors.BAD_DISK

        cpt_rendu = struct.pack(REPLY_FORMAT, 
            error, 0xFFFF)
        self.send_report(cpt_rendu, station_id, delayed=False)
        self.disconnect_station(station_id)

    def _handle_id(self, consigne, station_id):
        identification = Server.pad_file_name(bytes(consigne.ctx_data[:8]).decode('utf-8'), 8)
        self.logger.info(f'ID request from {station_id}: new ID : {identification}')
        error = 0
        for i in self.stations:
            if self.stations[i].identifier == identification:
                error = NRErrors.DUPLICATE_ID
                break
        if error == 0:
            self.stations[station_id].identifier = identification
        cpt_rendu = struct.pack('>B', error)
        self.send_report(cpt_rendu, station_id, delayed=False)
        self.disconnect_station(station_id)


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
            34 : (_handle_catp, 'CATP', 'File catalog'),
            35 : (_handle_cats, 'CATS', 'File catalog'),
            32 : (_handle_date, 'DATE', 'Request date and time'),
            33 : (_handle_id, 'ID', 'Declare an identifier to server'),
            48 : (None, 'IMPRIM', 'Use server printer'),
            49 : (_handle_sysinf, 'SYSINF', 'Request system informations'),
            50 : (None, 'SYSTEM', 'Back to operating system'),
            51 : (_handle_dskf, 'DSKF', 'Request free space on a server disk'),
            52 : (None, 'LIRATT', 'Get file attributes'),
            53 : (None, 'ECRATT', 'Write file attributes'),
            54 : (None, 'SETMODE', 'Sets file creation mode'),
            55 : (_handle_chbin, 'CHBIN', 'Load binary file'),
            56 : (_handle_clear, 'CLEAR', 'Clean slave descriptor on server'),
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
        self.stations = {}
        for i in range(1,32):
            self.stations[i] = Station(i)

    @staticmethod
    def pad_file_name(name, length, on_end=True):
        """
            Pads a file name with spaces
        """
        ret = name
        for i in range(len(name), length):
            if on_end:
                ret += ' '
            else:
                ret = ' ' + ret
        return ret

    def send_report(self, data, station_id, load_addr=None, load_page=None, delayed=False):
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
        cons.dest = station_id
        cons.computer = self.stations[station_id].computer
        cons.code_tache = Consigne.TC_COPY_REPORT
        cons.ctx_data = data
        cons.delayed = delayed
        if load_addr:
            cons.msg_addr = load_addr
        if load_page:
            cons.page = load_addr
        self.device.send_consigne(cons)

    def send_new_address(self, address, lenght, page, station_id):
        cons = Consigne()
        cons.dest = station_id
        cons.computer = self.stations[station_id].computer
        cons.code_tache = Consigne.TC_INIT_CALL
        cons.msg_addr = address
        cons.msg_len = lenght
        cons.page = page
        self.device.send_consigne(cons)

    def send_execute_code_request(self, station_id, code, delayed=False):
        """
            Sends a execute code request to specified station
        """
        cons = Consigne()
        cons.dest = station_id
        cons.computer = self.stations[station_id].computer
        cons.delayed = delayed
        cons.code_tache = Consigne.TC_EXEC_CODE
        cons.ctx_data = code
        self.device.send_consigne(cons)

    def send_binary_file(self, file, station_id):
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
            # Send where to download the file
            self.send_new_address(c.code_address, len(c.data), c.code_page, station_id)
            # Send file to computer
            self.device.send_data(c.data, station_id)
        return bin_file

    def disconnect_station(self, station_id):
        self.device.disconnect_peer(station_id)
        self.stations[station_id].computer = None   # Disconnected
        self.stations[station_id].clean()

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
                if not self.stations[station_num].computer:
                    self.logger.info(f'Discovered new station #{station_num}')
                    self.stations[station_num].computer = c.computer
                # Try to find a task to be executed for this consigne
                if not c.code_tache in self.SERVER_TASKS:
                    self.logger.error(f'Unsupported task code : {c.code_tache}. Disconnecting peer.')
                    self.disconnect_station(station_num)
                    continue                    
                tasks = self.SERVER_TASKS[c.code_tache]
                task_name = Consigne.get_code_task_string(c.code_tache)
                if not c.code_app in tasks:
                    self.logger.error(f'Unsupported application code {c.code_app} for task {task_name}. Disconnecting peer.')
                    self.disconnect_station(station_num)
                    continue
                task = tasks[c.code_app]
                if not task[0]:
                    self.logger.error(f'No handler defined for task {task[1]}/{task[2]}. Disconnecting peer.')
                    self.disconnect_station(station_num)
                    continue
                # Call the handler for this task
                task[0](self, c, station_num)


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