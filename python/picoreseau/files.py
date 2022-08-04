import struct
from datetime import date
import os
import sys
from tabnanny import NannyNag
import re

class BinaryCode :
    """
        Represents a binary code segment
    """

    def __init__(self, code_address, data, code_page=None):
        """
            Constructor

            Parameters
            ----------
            code_address : int
                Memory address of the code location
            data : bytesarray
                Code data
            code_page : int
                Memory page of the code location
        """
        self.code_address = code_address
        self.data = data
        self.code_page = code_page

    def __str__(self):
        ret = f'Binary code of {len(self.data)} bytes at ${self.code_address:04x}'
        if self.code_page is not None:
            if self.code_page == 0:
                ret += ' (no page change)'
            else:
                ret += f' on page {self.code_page & ~0x80}'
        return ret

class BinaryData :
    """
        Binary data
    """

    REC_SIMPLE_CODE = 0x0       # Simple binary record
    REC_SIMPLE_EXEC_ADDR = 0xff        # Simple execution address binary record
    REC_EXTENDED = 0x1          # Extended binary record
    REC_EXT_CODE = 0x2          # Extended binary code
    REC_EXT_EXEC_ADDR = 0x3     # Extended binary execution address

    # Structures definition
    REC_SIMPLE_CODE_STRUCT = '>HH'
    REC_SIMPLE_CODE_STRUCT_LEN = struct.calcsize(REC_SIMPLE_CODE_STRUCT)
    REC_SIMPLE_EXEC_ADDR_STRUCT = '>2xH'
    REC_SIMPLE_EXEC_ADDR_STRUCT_LEN = struct.calcsize(REC_SIMPLE_EXEC_ADDR_STRUCT)
    REC_EXTENDED_STRUCT = '>BBBH32s'
    REC_EXTENDED_STRUCT_LEN = struct.calcsize(REC_EXTENDED_STRUCT)
    REC_EXT_CODE_STRUCT = '>HHB'
    REC_EXT_CODE_STRUCT_LEN = struct.calcsize(REC_EXT_CODE_STRUCT)
    REC_EXT_ADDR_STRUCT = '>HHB'
    REC_EXT_ADDR_STRUCT_LEN = struct.calcsize(REC_EXT_ADDR_STRUCT)

    def __init__(self, file=None):
        self.machine_type = None
        self.code_language = None
        self.loading_byte = None
        self.app_name = None
        self.bin_code = []
        self.exec_address = None
        self.exec_page = None
        if file:
            self.__read_binary(file)

    def __add_binary_code(self, code : BinaryCode):
        self.bin_code.append(code)

    def __read_binary(self, f):
        while True:
            reg_type = f.read(1)
            if not reg_type:
                #EOF reached
                break
            # Gets record type
            reg_type = int.from_bytes(reg_type, 'little')
            if reg_type == BinaryData.REC_SIMPLE_CODE:
                # Simple binary code record
                code_len, code_addr = struct.unpack_from(BinaryData.REC_SIMPLE_CODE_STRUCT, f.read(BinaryData.REC_SIMPLE_CODE_STRUCT_LEN))
                code = BinaryCode(code_addr, f.read(code_len))
                self.__add_binary_code(code)
            elif reg_type == BinaryData.REC_SIMPLE_EXEC_ADDR:
                # Simple binary code execution address
                self.exec_address, = struct.unpack_from(BinaryData.REC_SIMPLE_EXEC_ADDR_STRUCT, f.read(BinaryData.REC_SIMPLE_EXEC_ADDR_STRUCT_LEN))
                # This is the last record in the binary file
                break
            elif reg_type == BinaryData.REC_EXTENDED:
                # Binary extended file description
                self.machine_type, self.code_language, self.loading_byte, nul_bytes, app_name = \
                                struct.unpack_from(BinaryData.REC_EXTENDED_STRUCT, f.read(BinaryData.REC_EXTENDED_STRUCT_LEN))
                self.app_name = app_name.decode('utf-8')
            elif reg_type == BinaryData.REC_EXT_CODE:
                # Binary extended code
                code_len, code_addr, code_page = struct.unpack_from(BinaryData.REC_EXT_CODE_STRUCT, f.read(BinaryData.REC_EXT_CODE_STRUCT_LEN))
                code = BinaryCode(code_addr, f.read(code_len), code_page)
                self.__add_binary_code(code)
            elif reg_type == BinaryData.REC_EXT_EXEC_ADDR:
                # Binary extended execution address
                nul_bytes, exec_addr, exec_page = struct.unpack_from(BinaryData.REC_EXT_ADDR_STRUCT, f.read(BinaryData.REC_EXT_ADDR_STRUCT_LEN))
                if nul_bytes != 0x0:
                    print("Null bytes expected!")
                self.exec_address = exec_addr
                self.exec_page = exec_page
                # This is the last record in the binary file
                break

    def __str__(self):
        ret =  f'File binary'
        if self.app_name:
            ret += f' (extended) application {self.app_name}'
        else:
            ret += ' (simple)'
        ret += f' starting at ${self.exec_address:04x} '
        if self.exec_page is not None:
            if self.exec_page == 0:
                ret += '(no page change)'
            else:
                ret += f'on page {self.exec_page & ~0x80}'

        for c in self.bin_code:
            ret += '\n' + str(c)
        return ret


class NanoreseauFile:
    """
        Class for opening Nanoreseau files

    """
    FILE_TYPE_BASIC_PRG = 0
    FILE_TYPE_BASIC_DATA = 1
    FILE_TYPE_MACHINE = 2
    FILE_TYPE_SOURCE = 3
    FILE_TYPE_INDEXED = 5

    FILE_MODE_BINARY = 0
    FILE_MODE_ASCII = 0xff

    FILE_STATUS_RW = 0
    FILE_STATUS_RO = 0xff

    def __init__(self, file_path=None):
        self.identifier = None          # File identifier, ID of the peer who created the file
        self.type = None                # File type : 0 BASIC software, 1 BASIC data, 2 Machine language, 3 source file, 5 indexed file
        self.file_mode = None           # File mode : 0 Binary, 0xff ASCII
        self.ms_dos_len = None          # File length in for MS-DOS, for compatibility reasons
        self.file_status = None         # File status : 0 read/write, 0xff read-only
        self.file_version_major = None  # File version : 2 bytes, minor, major
        self.file_version_minor = None  # File version : 2 bytes, minor, major
        self.creation_date = None       # File creation date (on 3 bytes) aa/mm/jj
        self.modification_date = None   # File modification date (on 3 bytes) aa/mm/jj
        self.created_on = None          # File creation computer (0 : TO7, 1 : MO5, 2 : TO7/70)
        self.creation_language = None   # File creation language (0 : N.C., 1 : BASIC, 2 : LOGO, 3 : LSE)
        self.app_bytes = bytearray(48)  # Applications bytes
        self.binary_data = None         # File binary data
        if file_path:
            self.open_file(file_path)

    @staticmethod
    def get_date(b):
        year = int.from_bytes(b[0:1], 'little')
        if year < 80:
            year = 2000 + year
        else:
            year = 1900 + year
        month = int.from_bytes(b[1:2], 'little')
        day = int.from_bytes(b[2:3], 'little')
        if month == 0 or day == 0:
            return None
        return date(year, month, day)

    def get_file_type(self):
        """
            Gets file type in a string representation
        """
        if self.type == self.FILE_TYPE_BASIC_PRG:
            return 'BASIC program'
        elif self.type == self.FILE_TYPE_BASIC_DATA:
            return 'BASIC data'
        elif self.type == self.FILE_TYPE_MACHINE:
            return 'Machine code'
        elif self.type == self.FILE_TYPE_SOURCE:
            return 'Source'
        elif self.type == self.FILE_TYPE_INDEXED:
            return 'Indexed file'
        else:
            return 'unknown'

    def get_file_status(self):
        if self.file_status == self.FILE_STATUS_RO:
            return 'read-only'
        elif self.file_status == self.FILE_STATUS_RW:
            return 'read/write'
        else:
            return 'unknown'

    def open_file(self, file_path):
        f = open(file_path, 'rb')
        # First 8 bytes are file header containing '*USTL*'
        if f.read(8).decode('utf-8') != '*NRUSTL*':
            raise Exception('Not a nanoreseau file (bad header)!')
        # Next 8 bytes are file identifier
        self.identifier = f.read(8).decode('utf-8')
        # Next byte is always 1
        b = f.read(1)
        if b != b'\x01':
            raise Exception('Bad nanoreseau file (at offset 16)')
        self.type = int.from_bytes(f.read(1), 'little')
        self.file_mode = int.from_bytes(f.read(1), 'little')
        self.ms_dos_len = int.from_bytes(f.read(3), 'little')
        if f.read(1) != b'\x00':
            raise Exception('Bad nanoreseau file (at offset 22)')
        self.file_status = int.from_bytes(f.read(1), 'little')
        self.file_version_major = int.from_bytes(f.read(1), 'little')
        self.file_version_minor = int.from_bytes(f.read(1), 'little')
        self.creation_date = self.get_date(f.read(3))
        self.modification_date = self.get_date(f.read(3))
        self.created_on = int.from_bytes(f.read(1), 'little')
        self.creation_language = int.from_bytes(f.read(1), 'little')
        # Skip 46 bytes
        f.seek(46, os.SEEK_CUR)
        f.readinto(self.app_bytes)
        if self.file_mode == NanoreseauFile.FILE_MODE_BINARY and self.type == NanoreseauFile.FILE_TYPE_MACHINE:
            self.binary_data = BinaryData(f)
        elif self.type == NanoreseauFile.FILE_TYPE_INDEXED:
            # Indexed files are not yet supported
            pass
        else:
            # Other data type, read all bytes
            self.binary_data = f.read()

    def __str__(self):
        ret = f'File ID : {self.identifier} v{self.file_version_major}.{self.file_version_minor} {self.creation_date}, modified {self.modification_date}' \
           f'\nFile type : {self.get_file_type()} Status : {self.get_file_status()}'
        if self.binary_data:
            ret += f'\n' + str(self.binary_data)
        return ret

class ApplicationFile:
    """
        Defines an application file to be loaded on the drive
    """

    def __init__(self, data):
        self.drive = bytes([0x40 + data[0]]).decode('utf-8')
        #Remove spaces in name and append extension
        self.file_name = data[1:-3].decode('utf-8').strip()
        self.file_name += '.'
        self.file_name += data[-3:].decode('utf-8').strip()

    def __str__(self):
        return f'ApplicationFile on drive {self.drive} with name {self.file_name}'

class NRConfigurationFile:
    """
        Class for loading a NR3.DATA configuration file
    """
    def __init__(self, file_path = None):
        self.version = None
        self.exit_file_name = None
        self.printers = []
        self.logical_disks = {}
        self.listing_disk = None
        self.system_byte = None
        self.identifiers = {}
        if file_path is not None:
            self.open_file(file_path)

    def __get_file_name(self, f, count):
        return ApplicationFile(f.read(count))

    def open_file(self, file_path):
        f = open(file_path, 'rb')
        self.version = f'{int.from_bytes(f.read(1), "little")}.{int.from_bytes(f.read(1), "little")}'
        print(f'Configuration file version : {self.version}')
        self.exit_file_name = self.__get_file_name(f, 9)
        for i in range(0, 4):
            self.printers.append(int.from_bytes(f.read(1), "little"))
        for i in range(0, 10):
            self.logical_disks[i] = int.from_bytes(f.read(1), "little")
        self.listing_disk = int.from_bytes(f.read(1), "little")
        self.system_byte = int.from_bytes(f.read(1), "little")
        id_count = int.from_bytes(f.read(1), "little")
        print(f'Got {id_count} identifiers')        
        for i in range(0, id_count):
            id = f.read(32)
            id_str = ''.join(f'{letter:02x}' for letter in id)
            file = self.__get_file_name(f, 12)
            self.identifiers[id_str] = file

    def __str__(self):
        ret = f'Configuration file v{self.version} exit file: {self.exit_file_name.file_name}\n' \
                'Identifiers:'
        for id in self.identifiers:
            ret += f'\n{id} -> {str(self.identifiers[id])}'
        return ret


# For testing
if __name__ == "__main__":
    f = None
    if len(sys.argv) > 1:
        f = NRConfigurationFile(sys.argv[1])
    else:
        f = NRConfigurationFile("MENU.MO5")
    print(str(f))


