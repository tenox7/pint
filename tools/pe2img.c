/*
 * pe2img: the images that are not PE files, made from an ARM (Thumb-2) PE image
 * that link.exe linked.
 *
 * pe2img image.exe address out.bin [out.elf]
 *   The memory image of a ROM, as the Raspberry Pi firmware loads a kernel: the
 *   sections from the first one, relocated to run at the address, as a flat
 *   binary and, for QEMU's -kernel, as an ELF executable with one segment there.
 *   The word at offset 8 receives the size (the ARC firmware's FwImageSize).
 *
 * pe2img -version major.minor image.exe
 *   Set the subsystem version of an image (link.exe for ARM writes 6.02; NT 3.5
 *   takes 3.10 to 3.50), recomputing its checksum if it has one.
 *
 * pe2img -rom image.exe out.exe
 *   The ARC program image of the RISC linker's -rom (an image linked fixed): the
 *   file header at offset 0, a ROM optional header, absolute section addresses,
 *   STYP section types, the sections packed on 0x200 with their uninitialized
 *   data, and resource data entries giving file offsets (BOOT\LIB\BLRES.C).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FILE_ALIGN 0x200

static uint8_t *pe;
static long pesize;
static uint8_t *nt, *opt, *sec;
static uint32_t nsec, imagebase;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static uint32_t align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static void die(const char *m) { fprintf(stderr, "pe2img: %s\n", m); exit(1); }

static void readpe(const char *name)
{
    FILE *f;

    if (!(f = fopen(name, "rb"))) die("cannot open the image");
    fseek(f, 0, SEEK_END); pesize = ftell(f); fseek(f, 0, SEEK_SET);
    pe = malloc(pesize);
    if (fread(pe, 1, pesize, f) != (size_t)pesize) die("cannot read the image");
    fclose(f);
    nt = pe + rd32(pe + 0x3c);
    if (rd32(nt) != 0x4550) die("not a PE image");
    nsec = rd16(nt + 6);
    opt = nt + 24;
    if (rd16(opt) != 0x10b) die("not a PE32 image");
    imagebase = rd32(opt + 28);
    sec = opt + rd16(nt + 20);
}

static uint16_t movimm(const uint8_t *p)
{
    uint16_t hw1 = rd16(p), hw2 = rd16(p + 2);
    return (hw1 & 0xf) << 12 | ((hw1 >> 10) & 1) << 11 | ((hw2 >> 12) & 7) << 8 | (hw2 & 0xff);
}

static void setmovimm(uint8_t *p, uint16_t v)
{
    uint16_t hw1 = rd16(p), hw2 = rd16(p + 2);
    wr16(p, (hw1 & 0xfbf0) | (v >> 12 & 0xf) | (v >> 11 & 1) << 10);
    wr16(p + 2, (hw2 & 0x8f00) | (v >> 8 & 7) << 12 | (v & 0xff));
}

/* The resource data entries below a resource directory: their RVAs become file offsets. */
static void resfix(uint8_t *rsrc, uint32_t dir, uint32_t rva, uint32_t fileoff)
{
    uint32_t n = rd16(rsrc + dir + 12) + rd16(rsrc + dir + 14), i;

    for (i = 0; i < n; i++) {
        uint32_t off = rd32(rsrc + dir + 16 + i * 8 + 4);
        if (off & 0x80000000)
            resfix(rsrc, off & 0x7fffffff, rva, fileoff);
        else
            wr32(rsrc + off, rd32(rsrc + off) - rva + fileoff);
    }
}

static int rom(const char *in, const char *out)
{
    uint8_t *img, *h;
    uint32_t i, hdr, fileoff, len = 0, codesize = 0, codebase = 0, database = 0, end = 0, n = 0;
    FILE *f;

    readpe(in);
    hdr = align(20 + 56 + nsec * 40, FILE_ALIGN);
    for (i = 0; i < nsec; i++) {
        uint8_t *s = sec + i * 40;
        uint32_t vs = rd32(s + 8), rs = rd32(s + 16);
        if (!memcmp(s, ".reloc", 6)) continue;
        len += align(vs > rs ? vs : rs, FILE_ALIGN);
    }
    img = calloc(1, hdr + len);
    fileoff = hdr;
    for (i = 0; i < nsec; i++) {
        uint8_t *s = sec + i * 40, *d = img + 20 + 56 + n * 40;
        uint32_t va = rd32(s + 12), vs = rd32(s + 8), rs = rd32(s + 16), ptr = rd32(s + 20);
        uint32_t chars = rd32(s + 36), size = vs > rs ? vs : rs, raw = align(size, FILE_ALIGN);
        uint32_t type = chars & 0x20 ? 0x20 : chars & 0x80000000 ? 0x40 : 0x100;
        if (!memcmp(s, ".reloc", 6)) continue;
        memcpy(d, s, 8);
        wr32(d + 8, size);
        wr32(d + 12, imagebase + va);
        wr32(d + 16, raw);
        wr32(d + 20, fileoff);
        wr32(d + 36, type);
        memcpy(img + fileoff, pe + ptr, rs < size ? rs : size);
        if (!memcmp(s, ".rsrc", 5))
            resfix(img + fileoff, 0, va, fileoff);
        if (type == 0x20) {
            codesize += raw;
            if (!codebase) codebase = imagebase + va;
        } else if (!database) {
            database = imagebase + va;
        }
        if (imagebase + va + raw > end) end = imagebase + va + raw;
        fileoff += raw;
        n++;
    }
    if (!database) database = end;
    h = img;
    wr16(h + 0, 0x1c4);                         /* IMAGE_FILE_MACHINE_ARMNT */
    wr16(h + 2, n);
    wr16(h + 16, 56);
    wr16(h + 18, 0x010f);                       /* fixed, executable, stripped */
    wr16(h + 20, 0x107);                        /* IMAGE_ROM_OPTIONAL_HDR_MAGIC */
    h[22] = 2;
    wr32(h + 24, codesize);
    wr32(h + 28, end - database);               /* the data, to the image end */
    wr32(h + 32, 0);
    wr32(h + 36, imagebase + rd32(opt + 16));   /* the entry, Thumb */
    wr32(h + 40, codebase);
    wr32(h + 44, database);
    if (!(f = fopen(out, "wb")) || fwrite(img, 1, hdr + len, f) != hdr + len) die("cannot write the image");
    fclose(f);
    printf("pe2img: %s -> %s, ROM image at 0x%x, entry 0x%x, %u sections\n",
           in, out, codebase, imagebase + rd32(opt + 16), n);
    return 0;
}

static int version(const char *v, const char *name)
{
    unsigned major, minor;
    uint32_t sum = 0;
    long i;
    FILE *f;

    if (sscanf(v, "%u.%u", &major, &minor) != 2) die("the version is major.minor");
    readpe(name);
    wr16(opt + 48, major);
    wr16(opt + 50, minor);
    if (rd32(opt + 64)) {
        wr32(opt + 64, 0);
        for (i = 0; i + 1 < pesize; i += 2) {
            sum += rd16(pe + i);
            sum = (sum & 0xffff) + (sum >> 16);
        }
        if (pesize & 1) sum += pe[pesize - 1];
        sum = (sum & 0xffff) + (sum >> 16);
        wr32(opt + 64, sum + pesize);
    }
    if (!(f = fopen(name, "wb")) || fwrite(pe, 1, pesize, f) != (size_t)pesize) die("cannot write the image");
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *img;
    uint32_t base, first = ~0u, end = 0, delta, relrva, relsize, i;

    if (argc == 4 && !strcmp(argv[1], "-rom"))
        return rom(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "-version"))
        return version(argv[2], argv[3]);
    if (argc < 4) die("usage: pe2img image.exe address out.bin [out.elf] | pe2img -rom image.exe out.exe | pe2img -version major.minor image.exe");
    base = strtoul(argv[2], 0, 0);
    readpe(argv[1]);
    for (i = 0; i < nsec; i++) {
        uint8_t *s = sec + i * 40;
        uint32_t va = rd32(s + 12), vs = rd32(s + 8), rs = rd32(s + 16);
        if (va < first) first = va;
        if (va + (vs > rs ? vs : rs) > end) end = va + (vs > rs ? vs : rs);
    }
    end = (end + 3) & ~3u;
    img = calloc(1, end);
    for (i = 0; i < nsec; i++) {
        uint8_t *s = sec + i * 40;
        uint32_t va = rd32(s + 12), vs = rd32(s + 8), rs = rd32(s + 16), ptr = rd32(s + 20);
        uint32_t n = rs < vs || vs == 0 ? rs : vs;
        if (ptr + n > (uint32_t)pesize) die("section beyond the file");
        memcpy(img + va, pe + ptr, n);
    }

    /* The relocations: the image moves so that its first section is at base. */
    delta = base - first - imagebase;
    relrva = rd32(opt + 96 + 5 * 8);
    relsize = rd32(opt + 96 + 5 * 8 + 4);
    if (delta != 0 && relsize == 0) die("the image has no relocations");
    for (i = 0; i < relsize; ) {
        uint32_t page = rd32(img + relrva + i), size = rd32(img + relrva + i + 4), j;
        if (size < 8) break;
        for (j = 8; j < size; j += 2) {
            uint16_t e = rd16(img + relrva + i + j);
            uint8_t *p = img + page + (e & 0xfff);
            uint32_t v;
            switch (e >> 12) {
            case 0:
                break;
            case 3:                     /* IMAGE_REL_BASED_HIGHLOW */
                wr32(p, rd32(p) + delta);
                break;
            case 7:                     /* IMAGE_REL_BASED_THUMB_MOV32 */
                v = ((uint32_t)movimm(p + 4) << 16 | movimm(p)) + delta;
                setmovimm(p, v & 0xffff);
                setmovimm(p + 4, v >> 16);
                break;
            default:
                fprintf(stderr, "pe2img: relocation type %d\n", e >> 12);
                return 1;
            }
        }
        i += size;
    }
    wr32(img + first + 8, end - first);

    if (!(f = fopen(argv[3], "wb"))) die("cannot write the binary");
    fwrite(img + first, 1, end - first, f);
    fclose(f);

    if (argc > 4) {
        uint8_t h[52 + 32];
        memset(h, 0, sizeof(h));
        memcpy(h, "\177ELF\1\1\1", 7);
        wr16(h + 16, 2);                /* executable */
        wr16(h + 18, 40);               /* ARM */
        wr32(h + 20, 1);
        wr32(h + 24, base);             /* entry: the first instruction, ARM state */
        wr32(h + 28, 52);               /* program headers */
        wr32(h + 36, 0x05000000);       /* EABI 5 */
        wr16(h + 40, 52);
        wr16(h + 42, 32);
        wr16(h + 44, 1);
        wr16(h + 46, 40);
        wr32(h + 52, 1);                /* PT_LOAD */
        wr32(h + 56, sizeof(h));
        wr32(h + 60, base);
        wr32(h + 64, base);
        wr32(h + 68, end - first);
        wr32(h + 72, end - first);
        wr32(h + 76, 7);
        wr32(h + 80, 4);
        if (!(f = fopen(argv[4], "wb"))) die("cannot write the ELF");
        fwrite(h, 1, sizeof(h), f);
        fwrite(img + first, 1, end - first, f);
        fclose(f);
    }
    printf("pe2img: %s -> %s at 0x%x, %u bytes\n", argv[1], argv[3], base, end - first);
    return 0;
}
