#!/usr/bin/env python3
import fcntl
import struct
import sys

DEV = sys.argv[1] if len(sys.argv) > 1 else "/dev/most0"
REG = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x87

IOC_NRBITS = 8
IOC_TYPEBITS = 8
IOC_SIZEBITS = 14
IOC_DIRBITS = 2

IOC_NRSHIFT = 0
IOC_TYPESHIFT = IOC_NRSHIFT + IOC_NRBITS
IOC_SIZESHIFT = IOC_TYPESHIFT + IOC_TYPEBITS
IOC_DIRSHIFT = IOC_SIZESHIFT + IOC_SIZEBITS

IOC_NONE = 0
IOC_WRITE = 1
IOC_READ = 2

def _IOC(direction, type_chr, nr, size):
    return (
        (direction << IOC_DIRSHIFT) |
        (ord(type_chr) << IOC_TYPESHIFT) |
        (nr << IOC_NRSHIFT) |
        (size << IOC_SIZESHIFT)
    )

def _IOWR(type_chr, nr, size):
    return _IOC(IOC_READ | IOC_WRITE, type_chr, nr, size)

# struct os8104_reg_io { uint16_t reg; uint16_t len; uint8_t data[62]; };
FMT = "<HH62s"
SIZE = struct.calcsize(FMT)

OS8104_IOC_REG_READ = _IOWR('m', 0x09, SIZE)

with open(DEV, "rb+", buffering=0) as f:
    buf = bytearray(struct.pack(FMT, REG, 1, bytes(62)))
    fcntl.ioctl(f, OS8104_IOC_REG_READ, buf, True)
    reg, length, data = struct.unpack(FMT, buf)
    print(f"reg 0x{reg:04X}, len={length}, value=0x{data[0]:02X} ({data[0]})")