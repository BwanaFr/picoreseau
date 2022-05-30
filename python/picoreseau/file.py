import struct
from datetime import date
import os

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
        if file_path:
            self.open_file(file_path)

    @staticmethod
    def get_date(b):
        year = int.from_bytes(b[0:1], 'little')
        if year < 80:
            year = 2000 + year
        else:
            year = 1900 + year
        return date(year,int.from_bytes(b[1:2], 'little'),int.from_bytes(b[2:3], 'little'))

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
        print(f'Actual file position {f.tell()}')
        print(f'Next byte is {int.from_bytes(f.read(1), "little")}')
    
    def __str__(self):
        return f'File ID : {self.identifier} v{self.file_version_major}.{self.file_version_minor} {self.creation_date}, modified {self.modification_date}'

if __name__ == "__main__":
    f = NanoreseauFile("MENU.MO5")
    print(str(f))
    print(f'File type : {f.get_file_type()} Status : {f.get_file_status()}')


