#!/usr/bin/env python3
#
# Wrap a flat ARM binary in a minimal PE32 so the NT OS loader's real PE loader
# (BOOT/LIB/PELDR.C BlLoadImage) can load it. The arm-linux-gnueabihf binutils
# emits ELF only - no PE/COFF backend exists - so we hand-build the headers. We
# control both ends (this tool and the ported peldr.c), so the PE need contain
# only the fields peldr actually reads.
#
# Layout produced (one RWX section, no relocations, no imports, no .bss):
#   file 0x000              DOS header (e_lfanew -> 0x40)
#        0x040              PE\0\0 + COFF FileHeader + OptionalHeader(PE32) + 1 section header
#        0x200 (SizeOfHeaders, PointerToRawData)
#                           the flat binary (section RVA 0x1000), padded to FileAlignment
#
# peldr maps the section at ImageBase + section-RVA. We set ImageBase so that
# equals the kernel's link address; BlAllocateDescriptor honors the exact base
# (blmemory.c), so NewImageBase == ImageBase and peldr never relocates.
#
import argparse
import struct

FILE_ALIGN = 0x200
SECT_ALIGN = 0x1000

IMAGE_FILE_EXECUTABLE_IMAGE   = 0x0002
IMAGE_FILE_LINE_NUMS_STRIPPED = 0x0004
IMAGE_FILE_LOCAL_SYMS_STRIPPED = 0x0008
IMAGE_FILE_32BIT_MACHINE      = 0x0100

IMAGE_SCN_CNT_CODE   = 0x00000020
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ   = 0x40000000
IMAGE_SCN_MEM_WRITE  = 0x80000000

PE32_MAGIC      = 0x10b
SUBSYSTEM_NATIVE = 1


def roundup(v, a):
    return (v + a - 1) & ~(a - 1)


def build(data, image_base, section_rva, entry_va, machine):
    raw_size = roundup(len(data), FILE_ALIGN)
    virt_size = len(data)                       # no .bss: VirtualSize == file data length
    entry_rva = entry_va - image_base
    size_of_headers = FILE_ALIGN
    size_of_image = section_rva + roundup(virt_size, SECT_ALIGN)
    ptr_to_raw = size_of_headers

    dos = bytearray(0x40)
    dos[0:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3c, 0x40)     # e_lfanew

    pe_sig = b'PE\0\0'

    characteristics = (IMAGE_FILE_EXECUTABLE_IMAGE |
                       IMAGE_FILE_LINE_NUMS_STRIPPED |
                       IMAGE_FILE_LOCAL_SYMS_STRIPPED |
                       IMAGE_FILE_32BIT_MACHINE)
    opt_size = 0xE0                             # PE32 optional header incl. 16 data dirs
    coff = struct.pack('<HHIIIHH', machine, 1, 0, 0, 0, opt_size, characteristics)

    opt = b''.join([
        struct.pack('<H', PE32_MAGIC),
        struct.pack('<BB', 0, 0),               # Major/MinorLinkerVersion
        struct.pack('<I', raw_size),            # SizeOfCode
        struct.pack('<I', 0),                   # SizeOfInitializedData
        struct.pack('<I', 0),                   # SizeOfUninitializedData
        struct.pack('<I', entry_rva),           # AddressOfEntryPoint
        struct.pack('<I', section_rva),         # BaseOfCode
        struct.pack('<I', section_rva),         # BaseOfData
        struct.pack('<I', image_base),          # ImageBase
        struct.pack('<I', SECT_ALIGN),          # SectionAlignment
        struct.pack('<I', FILE_ALIGN),          # FileAlignment
        struct.pack('<HH', 4, 0),               # Major/MinorOperatingSystemVersion
        struct.pack('<HH', 1, 0),               # Major/MinorImageVersion
        struct.pack('<HH', 4, 0),               # Major/MinorSubsystemVersion
        struct.pack('<I', 0),                   # Win32VersionValue
        struct.pack('<I', size_of_image),       # SizeOfImage
        struct.pack('<I', size_of_headers),     # SizeOfHeaders
        struct.pack('<I', 0),                   # CheckSum (0; peldr skips unless DEBUG_STRIPPED)
        struct.pack('<H', SUBSYSTEM_NATIVE),    # Subsystem
        struct.pack('<H', 0),                   # DllCharacteristics
        struct.pack('<I', 0x40000),             # SizeOfStackReserve
        struct.pack('<I', 0x1000),              # SizeOfStackCommit
        struct.pack('<I', 0x100000),            # SizeOfHeapReserve
        struct.pack('<I', 0x1000),              # SizeOfHeapCommit
        struct.pack('<I', 0),                   # LoaderFlags
        struct.pack('<I', 16),                  # NumberOfRvaAndSizes
    ])
    opt += b'\0' * (16 * 8)                     # 16 empty data directories (no reloc/import dir)
    assert len(opt) == opt_size, len(opt)

    sect_chars = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                  IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)
    section = struct.pack('<8sIIIIIIHHI',
                          b'.text\0\0\0',
                          virt_size,            # Misc.VirtualSize
                          section_rva,          # VirtualAddress
                          raw_size,             # SizeOfRawData
                          ptr_to_raw,           # PointerToRawData
                          0, 0, 0, 0,           # relocs/linenums (none)
                          sect_chars)

    headers = bytes(dos) + pe_sig + coff + opt + section
    assert len(headers) <= size_of_headers, len(headers)
    headers = headers.ljust(size_of_headers, b'\0')
    body = data.ljust(raw_size, b'\0')
    return headers + body


def verify(pe, image_base, section_rva, entry_va, machine):
    # Re-parse exactly as RtlImageNtHeader + peldr.c BlLoadImage do.
    assert pe[0:2] == b'MZ', 'DOS signature'
    e_lfanew = struct.unpack_from('<I', pe, 0x3c)[0]
    assert pe[e_lfanew:e_lfanew + 4] == b'PE\0\0', 'PE signature'
    fh = e_lfanew + 4
    mach, nsec, _, _, _, optsz, chars = struct.unpack_from('<HHIIIHH', pe, fh)
    assert mach == machine, 'machine'
    assert chars & IMAGE_FILE_EXECUTABLE_IMAGE, 'not executable'
    oh = fh + 20
    assert struct.unpack_from('<H', pe, oh)[0] == PE32_MAGIC, 'opt magic'
    entry_rva = struct.unpack_from('<I', pe, oh + 16)[0]
    ib = struct.unpack_from('<I', pe, oh + 28)[0]
    assert ib == image_base, 'image base'
    assert ib + entry_rva == entry_va, 'entry'
    # base reloc data dir (index 5) must be empty so peldr skips relocation fixups
    breloc = struct.unpack_from('<II', pe, oh + 96 + 5 * 8)
    assert breloc == (0, 0), 'base reloc dir not empty'


def main():
    ap = argparse.ArgumentParser(description='wrap a flat ARM binary as a minimal PE32')
    ap.add_argument('infile')
    ap.add_argument('outfile')
    ap.add_argument('--image-base', type=lambda x: int(x, 0), default=0x01000000)
    ap.add_argument('--section-rva', type=lambda x: int(x, 0), default=0x1000)
    ap.add_argument('--entry', type=lambda x: int(x, 0), required=True,
                    help='linked entry-point virtual address (from the ELF)')
    ap.add_argument('--machine', type=lambda x: int(x, 0), default=0x1c0,
                    help='IMAGE_FILE_MACHINE_ARM = 0x1c0')
    a = ap.parse_args()

    with open(a.infile, 'rb') as f:
        data = f.read()

    pe = build(data, a.image_base, a.section_rva, a.entry, a.machine)
    verify(pe, a.image_base, a.section_rva, a.entry, a.machine)

    with open(a.outfile, 'wb') as f:
        f.write(pe)

    print('mkpe: %s -> %s  (%d bytes flat, %d bytes PE, entry rva 0x%x, base 0x%x)' %
          (a.infile, a.outfile, len(data), len(pe), a.entry - a.image_base, a.image_base))


if __name__ == '__main__':
    main()
