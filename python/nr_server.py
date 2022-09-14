from genericpath import isfile
from turtle import st
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
from enum import IntEnum, Enum
from datetime import datetime, timezone

class Station:
    """
        This class represents a nanoreseau station
    """
    def __init__(self, station_id):
        self.id = station_id            # Station address (1..31)
        self.computer = None            # Type of computer (None if offline)
        self.clean()

    def clean(self):
        """
            Cleans the station identifier
        """
        self.identifier = '        '    # Station identifier (received from ID CTA)
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

class FileTasks(int, Enum):

    def __new__(cls, value, description):
        obj = int.__new__(cls, value)
        obj._value_ = value
        obj.description = description
        return obj

    OUVFL = (36, 'Open file for reading')
    OUVFE = (37, 'Open file for writing')
    LIRFI = (40, 'Read from file')
    ECRFI = (41, 'Write to file')
    FERFI = (42, 'Close file')
    CREFI = (39, 'Create file')
    SUPFI = (45, 'Delete file')
    RENFI = (46, 'Rename file')
    RESFI = (43, 'Reserve file')
    RELFI = (44, 'Release file')
    COPIE = (47, 'Copy file')
    CATP = (34, 'File catalog')
    CATS = (35, 'File catalog')
    DATE = (32, 'Request date and time')
    ID = (33, 'Declare an identifier to server')
    IMPRIM = (48, 'Use server printer')
    SYSINF = (49, 'Request system informations')
    SYSTEM = (50, 'Back to operating system')
    DSKF = (51, 'Request free space on a server disk')
    LIRATT = (52, 'Get file attributes')
    ECRATT = (53, 'Write file attributes')
    SETMODE = (54, 'Sets file creation mode')
    CHBIN = (55, 'Load binary file')
    CLEAR = (56, 'Clean slave descriptor on server')
    RBUFF = (57, 'Read common buffer')
    WBUFF = (58, 'Write common buffer')
    CHAENR = (65, 'Load an indexed file record')
    GARENR = (64, 'Park an indexed file record')
    SUPENR = (66, 'Delete an indexed file record')
    RESENR = (67, 'Reserve an indexed file record')
    RELENR = (68, 'Release an indexed file record')
    NES = (69, 'Request next indexed file record number')
    DNE = (70, 'Request last indexed file record number')
    PROG = (1, 'Exchange program')
    TELE = (16, 'Download program')

class FileException(Exception):

    def __init__(self, error : NRErrors):
        self.error = error

    def get_error_code(self) -> NRErrors:
        return self.error


class FileMetaData():
    """
        Nanoreseau file used by peers
    """
    def __init__(self, nr_file: files.ApplicationFile, logical_num: int):
        self.nr_file = nr_file          # ApplicationFile
        self.logical_nb = logical_num   # Logical number
        self.reserved_by = None         # Who reserved the file
        self.open_write_by = None       # Who opens the file for writing
        self.open_read_by = {}          # Who opens the file for reading

    def add_read_count(self, station_id: int):
        """
            Adds a station as reader
        """
        #Checks if the file is reserved
        if (self.reserved_by and self.reserved_by != station_id):
            raise FileException(NRErrors.FILE_ALREADY_RESERVED)
        elif self.open_write_by:    #Checks if the file is already open for writing
            if self.open_write_by == station_id:
                raise FileException(NRErrors.FILE_ALREADY_OPEN_FOR_WRITE)
            raise FileException(NRErrors.FILE_ALREADY_OPEN_FOR_WRITE_OTHER_STA)
        # No error
        self.open_read_by[station_id] = station_id        
    
    def set_write_lock(self, station_id: int):
        """
            Adds a station as writer
        """
        #Checks if the file is reserved
        if self.is_reserved(station_id):
            raise FileException(NRErrors.FILE_ALREADY_RESERVED)
        elif self.open_write_by and self.open_write_by == station_id:
            raise FileException(NRErrors.FILE_ALREADY_OPEN_FOR_WRITE_OTHER_STA)
        elif len(self.open_read_by) != 0:
            raise FileException(NRErrors.FILE_ALREADY_OPEN_FOR_READ)
        # No error
        self.open_write_by = station_id

    def reserve_file(self, station_id: int):
        """
            Reserves the file
        """
        #Checks if the file is reserved
        if self.is_reserved(station_id):
            raise FileException(NRErrors.FILE_ALREADY_RESERVED)
        elif self.open_write_by and self.open_write_by != station_id:
            raise FileException(NRErrors.FILE_OPEN)
        elif len(self.open_read_by) != 0:
            raise FileException(NRErrors.FILE_OPEN)
        # No error
        self.reserved_by = station_id

    def close_file(self, station_id: int):
        pass

    def is_reserved(self, station_id: int) -> bool:
        """
            Gets if the file is already reserved by another station
        """
        return (self.reserved_by and self.reserved_by != station_id)

    def is_open_for_write(self, station_id: int) -> bool:
        """
            Gets if the file is already open for write by another station
        """
        return (self.open_write_by and self.open_write_by != station_id)
        
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
            # TODO: Change code depending on target (MO5, TO7)
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
        # Save the stack (seems not needed)
        # self.send_execute_code_request(station_id,  b'\x34\x52\x11\x8C\x20\x80\x23\x06\x11\x8C\x20\xCC\x23\x14\x1A\xFF\xCE\x20\xAC\x86\x10\xAE\xE1\xAF\xC1\x4A\x26\xF9\x10\xCE\x20\xAC\x1C\x00\x35\xD2')
        bin_file = self.send_binary_file(nr_file, station_id)
        self.logger.info(f'CHBIN loaded file {str(nr_file)}')
        # Jump to execution address
        # TODO: Change code depending on target (MO5, TO7)
        exec_code = bytearray(b'\x10\xCE\x20\xCC\x7E\x50\x00')
        struct.pack_into('>H', exec_code, 5, bin_file.binary_data.exec_address)
        self.send_execute_code_request(station_id, exec_code, True)
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
        filter_file_name = Server.pad_file_name(file_filter.file_name, 8) + '.' + Server.pad_file_name(file_filter.extension, 3)
        self.logger.info(f'CATP request from {station_id}: using filter {str(filter_file_name)}')
        # List all files matching the regex        
        file_regex = filter_file_name.replace('?', '[a-zA-Z0-9\\s]')
        root_folder = Path(self.base_path).joinpath(file_filter.get_drive_name())
        self.stations[station_id].file_listing = []
        for f in os.scandir(root_folder):
            file_parts = f.name.rsplit('.', 1)
            file_name = file_parts[0]
            file_extension = ''
            if len(file_parts) > 1:
                file_extension = '.' + Server.pad_file_name(file_parts[1], 3)
            file_name = Server.pad_file_name(file_name, 8) + file_extension
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
        elif len(self.stations[station_id].file_listing) == 0:
            err_msg = NRErrors.FILE_NOT_EXISTING
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
            self.send_data(consigne.msg_addr, 0, buffer[:buf_len], station_id)

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
        self.send_report(cpt_rendu, station_id, delayed=True)
        self.disconnect_station(station_id)

    def _handle_ouvfl(self, consigne, station_id):
        """
            Opens a file for reading
        """
        app_file = files.ApplicationFile(bytes(consigne.ctx_data[:12]))
        self.logger.info(f'OUVFL request from {station_id} file : {str(app_file)}')
        cpt_rendu = None
        try:
            file_meta = self._get_file_metadata_by_file(app_file)
            # Try to add the station as reader
            file_meta.add_read_count(station_id)
            try:
                nr_file = files.NanoreseauFile(self._get_file_path(app_file))
                size_0 = nr_file.ms_dos_len & 0xFF
                size_1 = (nr_file.ms_dos_len >> 8) & 0xFF
                size_2 = (nr_file.ms_dos_len >> 16) & 0xFF
                cpt_rendu = struct.pack('>BBBBBBB', 
                        0,              # Error
                        file_meta.logical_nb,       # Logical file number
                        nr_file.type,               # File type
                        nr_file.file_mode,          # ASCII/binary
                        size_0,size_1,size_2        # File lenght
                        )
            except:
                raise FileException(NRErrors.BAD_DISK)

        except FileException as e:
            self.logger.info(f'OUVFL failed with error {str(e.error)}')
            cpt_rendu = struct.pack('>BBBBBBB', 
                        e.error, # Error
                        0,                          # Logical file number
                        0,                          # File type
                        0,                          # ASCII/binary
                        0,0,0                       # File lenght
                        )
        self.send_report(cpt_rendu, station_id, delayed=True)
        self.disconnect_station(station_id)

    def _handle_lirfi(self, consigne, station_id):
        logical_nb, offset_0, offset_1, offset_2 = struct.unpack_from('>BBBB', consigne.ctx_data)
        offset = (offset_0<<16) | (offset_1<<8) | offset_2
        self.logger.info(f'LIRFI request from {station_id} on file number {logical_nb} at offset {offset}, up to {consigne.msg_len} bytes')
        cpt_rendu = None
        try:
            file_meta = self._get_file_metadata_by_number(logical_nb)
            nr_file = files.NanoreseauFile(self._get_file_path(file_meta.nr_file))
            self.logger.info(f'Nanoreseau file is {nr_file.ms_dos_len} long. Binary size is {len(nr_file.binary_data)}')
            error = 0
            read_bytes = consigne.msg_len
            if consigne.msg_len > len(nr_file.binary_data):
                read_bytes = len(nr_file.binary_data)
                error = NRErrors.END_OF_FILE
            cpt_rendu = struct.pack('>BH', error, read_bytes)
            self.send_data(consigne.msg_addr, consigne.page, nr_file.binary_data[offset:read_bytes], station_id)
        except FileException as e:
            self.logger.info(f'LIRFI failed with error {str(e.error)}')
            cpt_rendu = struct.pack('>BH', e.error, 0)

        self.send_report(cpt_rendu, station_id, delayed=True)
        self.disconnect_station(station_id)

    def _handle_ferfi(self, consigne, station_id):
        logical_nb, = struct.unpack_from('>B', consigne.ctx_data)
        self.logger.info(f'FERFI request from {station_id} on file number {logical_nb}')
        error = 0
        try:
            file_meta = self._get_file_metadata_by_number(logical_nb)
            meta_key = file_meta.nr_file.get_virtual_location()
            #TODO: Properly close file
            del self.nr_files[meta_key]
        except FileException as e:
            self.logger.info(f'FERFI failed with error {str(e.error)}')
            error = e.error

        cpt_rendu = struct.pack('>B', error)
        self.send_report(cpt_rendu, station_id, delayed=True)
        self.disconnect_station(station_id)

    # Maps of tasks
    SERVER_TASKS = {
        Consigne.TC_INIT_CALL : {
            0 : _handle_init_call,
        },
        Consigne.TC_FILE : {
            # TODO : Handle file requests here
            FileTasks.OUVFL : _handle_ouvfl,
            FileTasks.OUVFE : None,
            FileTasks.LIRFI : _handle_lirfi,
            FileTasks.ECRFI : None,
            FileTasks.FERFI : _handle_ferfi,
            FileTasks.CREFI : None,
            FileTasks.SUPFI : None,
            FileTasks.RENFI : None,
            FileTasks.RESFI : None,
            FileTasks.RELFI : None,
            FileTasks.COPIE : None,
            FileTasks.CATP : _handle_catp,
            FileTasks.CATS : _handle_cats,
            FileTasks.DATE : _handle_date,
            FileTasks.ID : _handle_id,
            FileTasks.IMPRIM : None,
            FileTasks.SYSINF : _handle_sysinf,
            FileTasks.SYSTEM : None,
            FileTasks.DSKF : _handle_dskf,
            FileTasks.LIRATT : None,
            FileTasks.ECRATT : None,
            FileTasks.SETMODE : None,
            FileTasks.CHBIN : _handle_chbin,
            FileTasks.CLEAR : _handle_clear,
            FileTasks.RBUFF : None,
            FileTasks.WBUFF : None,
            FileTasks.CHAENR : None,
            FileTasks.GARENR : None,
            FileTasks.SUPENR : None,
            FileTasks.RESENR : None,
            FileTasks.RELENR : None,
            FileTasks.NES : None,
            FileTasks.DNE : None,
            FileTasks.PROG : None,
            FileTasks.TELE : None,
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
        self.device = None                  # USB device
        self.cfg_file = None                # Configuration file
        self.stations = {}                  # List of stations
        for i in range(1,32):
            self.stations[i] = Station(i)
        self.nr_files = {}                  # List of files

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
        """
            NOP consigne to change the address, page and lenght
            on station
        """
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
        cons = Consigne(60)
        cons.dest = station_id
        cons.computer = self.stations[station_id].computer
        cons.delayed = delayed
        cons.code_tache = Consigne.TC_EXEC_CODE
        cons.ctx_data = code
        cons.msg_len = len(code)
        self.device.send_consigne(cons)

    def send_binary_file(self, file, station_id):
        """
            Sends a binary file to specified station
        """
        bin_file_path = self._get_file_path(file)
        bin_file = files.NanoreseauFile(bin_file_path)
        self.logger.info(f'Will send the file {str(bin_file)}')
        if not bin_file.binary_data and type(bin_file.binary_data) != files.BinaryData:
            self.logger.error(f'File {bin_file.identifier} is not a valid binary file!')
            return
        #TODO: Current application change
        exec_code = bytearray(b'\x86\x01\xB7\x1F\xF7\x39')
        self.send_execute_code_request(station_id, exec_code, False)

        bin_data = bin_file.binary_data
        for c in bin_data.bin_code:
            self.send_data(c.code_address, c.code_page, c.data, station_id)
        return bin_file

    def send_data(self, code_address, code_page, data, station_id):
        # Send where to download the file
        self.send_new_address(code_address, len(data), code_page, station_id)
        # Send file to computer
        self.device.send_data(data, station_id)


    def disconnect_station(self, station_id):
        """
            Sends a disconnection request to station
        """
        self.device.disconnect_peer(station_id)
        self.stations[station_id].computer = None   # Disconnected
        self.stations[station_id].clean()

    def _get_file_path(self, file: files.ApplicationFile) -> Path:
        """
            Gets the file path on server
        """
        return Path(self.base_path).joinpath(file.get_drive_name()).joinpath(file.get_file_name())

    def _get_free_file_logical_id(self) -> int:
        """
            Gets a free logical number for a file
        """        
        if len(self.nr_files) < 255:
            for i in range(0, 256):
                found = False
                for f in self.nr_files.values():
                    if f.logical_nb == i:
                        found = True
                        break
                if not found:
                    return i
        else:
            return None

    def _get_file_metadata_by_file(self, nr_file : files.ApplicationFile, create=True):
        """
            Gets a file metadata in the collection

            Parameters:
            -----------
            nr_file : files.ApplicationFile
                File to be open
            create : bool
                If True, create the metadata and add it to list of files

            Returns:
            --------
            file_meta : FileMetaData
                File meta data, None if not found/error occured
        """
        # Checks if the file exists
        file_meta = None
        file_path = self._get_file_path(nr_file)
        if not file_path.is_file():
            self.logger.info(f'File {nr_file.get_virtual_location()} don\'t exists or is not a file!')
            raise FileException(NRErrors.FILE_NOT_EXISTING)
        else:
            # File exists
            if nr_file.get_virtual_location() in self.nr_files:
                # File already used by another station
                file_meta = self.nr_files[nr_file.get_virtual_location()]
            else:
                if create:
                    # File not used by any station, create a new one
                    # TODO: Do we open the file right-now?
                    file_meta = FileMetaData(nr_file, self._get_free_file_logical_id())
                    if file_meta.logical_nb is None:
                        raise FileException(NRErrors.FILE_TABLE_SATURATED)
                    self.nr_files[nr_file.get_virtual_location()] = file_meta
                else:
                    raise FileException(NRErrors.BAD_LOGIC_NUMBER)
        return file_meta

    def _get_file_metadata_by_number(self, logical_number: int):
        """
            Gets a file metadata in the collection using the logical number

            Parameters:
            -----------
            logical_number : int
                Logical number of file
            Returns:
            --------
            file_meta : FileMetaData
                File meta data, None if not found/error occured
        """
        # Checks if the file exists
        self.logger.info(f'_get_file_metadata_by_number -> Number of files : {len(self.nr_files)}')
        for f in self.nr_files.values():
            if f.logical_nb == logical_number:
                return f
        raise FileException(NRErrors.BAD_LOGIC_NUMBER)
        

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
                if not task:
                    value = c.code_app
                    description = 'unknown'
                    try:
                        description = FileTasks(value).description
                    except:
                        pass
                    self.logger.error(f'No handler defined for task {value}/{description}. Disconnecting peer.')
                    self.disconnect_station(station_num)
                    continue
                # print(f'Task : {task[1]}')
                # Call the handler for this task
                task(self, c, station_num)


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