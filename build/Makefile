CROSS   ?= arm-linux-gnueabihf-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy

ARMDIR    := ../arcfw/arm
PORTEDDIR := ../arcfw/ported
INCDIR    := ../arcfw/inc
OBJDIR    := ../obj

# Arc disk image source. 0 (default) embeds the FAT image as a blob for QEMU; 1 omits
# the blob and serves the image the Pi firmware stages in RAM via config.txt's
# "initramfs ramdisk.img 0x00800000" (real-hardware SD boot - no in-loader SD driver).
# Build the SD variant with: ./build.sh RAMDISK_INITRAMFS=1
RAMDISK_INITRAMFS ?= 0

# Freestanding Cortex-A7. NT-source flags: lowercase-named shim headers in
# arcfw/inc come first; _ARM_ selects the ARM branches added to the copied NT
# headers; _NTSYSTEM_ matches the original bldr build (NTSYSAPI -> empty);
# -fms-extensions allows the MS-style unnamed nested structs/unions NT uses;
# unknown-pragma warnings come from leftover MSVC #pragma warning(...) lines;
# enum-compare/enum-conversion are intentional in the verbatim NT source, which
# mixes MEMORY_TYPE and TYPE_OF_MEMORY (arc.h aliases LoaderFree=MemoryFree etc.,
# so the values match); unused-but-set-variable is dead-but-harmless 1994 code;
# unused-value silences NT's UNREFERENCED_PARAMETER(P) macro, which expands to a
# bare `(P)` expression-statement (used throughout BOOT/LIB, e.g. blio/fatboot).
CFLAGS  := -mcpu=cortex-a7 -marm -mfloat-abi=soft \
           -ffreestanding -fno-pic -fms-extensions -fshort-wchar \
           -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -fno-stack-protector -O2 -Wall -Wextra \
           -Wno-unknown-pragmas -Wno-unused-parameter \
           -Wno-missing-field-initializers -Wno-parentheses -Wno-unused-label \
           -Wno-enum-compare -Wno-enum-conversion -Wno-unused-but-set-variable \
           -Wno-unused-value \
           -MMD -MP \
           -I$(INCDIR) -D_ARM_ -D_NTSYSTEM_ -DRAMDISK_INITRAMFS=$(RAMDISK_INITRAMFS)
# Link base. 0x8000 is the Raspberry Pi firmware's native 32-bit kernel load
# address; QEMU honors it via the ELF LMA (the raw .bin is for SD boot). Override
# with `make TEXT_BASE=0x00010000` to reproduce the earlier QEMU raw -kernel layout.
TEXT_BASE ?= 0x00008000
LDFLAGS := -nostdlib -no-pie -Wl,-T,arcfw.ld -Wl,--defsym,TEXT_BASE=$(TEXT_BASE) \
           -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-z,noexecstack

ARM_OBJS    := $(OBJDIR)/start.o $(OBJDIR)/uart.o $(OBJDIR)/main.o \
               $(OBJDIR)/clib.o $(OBJDIR)/arcemul.o $(OBJDIR)/memory.o \
               $(OBJDIR)/fsstub.o $(OBJDIR)/stubs.o $(OBJDIR)/init.o \
               $(OBJDIR)/mailbox.o $(OBJDIR)/fb.o $(OBJDIR)/fbcon.o \
               $(OBJDIR)/console.o $(OBJDIR)/usb.o $(OBJDIR)/timer.o \
               $(OBJDIR)/arcdos_rt.o $(OBJDIR)/ramdisk.o \
               $(OBJDIR)/imageldr.o $(OBJDIR)/ntsetup.o $(OBJDIR)/config.o \
               $(OBJDIR)/blsvc.o $(OBJDIR)/ntrtlcm.o
PORTED_OBJS := $(OBJDIR)/osloader.o $(OBJDIR)/blmisc.o $(OBJDIR)/blmemory.o \
               $(OBJDIR)/blio.o $(OBJDIR)/fatboot.o $(OBJDIR)/arcdos.o \
               $(OBJDIR)/peldr.o $(OBJDIR)/blbind.o $(OBJDIR)/blconfig.o \
               $(OBJDIR)/arcdisk.o $(OBJDIR)/keconfig.o \
               $(OBJDIR)/regboot.o $(OBJDIR)/blload.o
# Boot-time Configuration Manager (NT's bconfig.lib: read-only hive engine +
# boot driver-list scan). 13 files ported verbatim from PRIVATE/NTOS/CONFIG into
# arcfw/ported/config/. Drives Tier B (load + scan the real SYSTEM hive).
CONFIG_OBJS := $(OBJDIR)/cmboot.o $(OBJDIR)/cmchek.o $(OBJDIR)/cmdat.o \
               $(OBJDIR)/cmdat2.o $(OBJDIR)/cmindex.o $(OBJDIR)/cmname.o \
               $(OBJDIR)/cmtree.o $(OBJDIR)/hivebin.o $(OBJDIR)/hivecell.o \
               $(OBJDIR)/hivechek.o $(OBJDIR)/hiveinit.o $(OBJDIR)/hivesum.o \
               $(OBJDIR)/hivemap.o
OBJS        := $(ARM_OBJS) $(PORTED_OBJS) $(CONFIG_OBJS) $(OBJDIR)/font_blob.o
# Embed the FAT image blob only for the QEMU build; the initramfs build serves the
# firmware-staged image from RAM, so the ~6 MiB blob is omitted and arcfw.bin is tiny.
ifeq ($(RAMDISK_INITRAMFS),0)
OBJS        += $(OBJDIR)/ramdisk_blob.o
endif

# arcdos.c is written against the ADK ARC API (plain Open/Read/Print, ADK arc.h
# names) which collide with the loader's NT headers. Compile it in isolation: the
# self-contained arcfw/arcdos-inc/ compat header ONLY, never -I$(INCDIR). The
# bridge (arcdos_rt.o, built with the normal CFLAGS) connects the two ABIs.
ARCDOS_CFLAGS := -mcpu=cortex-a7 -marm -mfloat-abi=soft -ffreestanding -fno-pic \
                 -fms-extensions -fshort-wchar -fno-stack-protector \
                 -fno-unwind-tables -fno-asynchronous-unwind-tables -O2 -Wall \
                 -Wno-unused-parameter -Wno-unused-variable \
                 -Wno-unused-but-set-variable -Wno-parentheses -Wno-pointer-sign \
                 -MMD -MP -I../arcfw/arcdos-inc -D_ARM_
$(OBJDIR)/arcdos.o: CFLAGS := $(ARCDOS_CFLAGS)

all: $(OBJDIR)/arcfw.bin

# Rebuild every object when RAMDISK_INITRAMFS flips. -MMD -MP tracks header includes but
# NOT -D flags, so switching the gate (e.g. `./build.sh` then `./build.sh RAMDISK_INITRAMFS=1`)
# without a clean would otherwise link .o files compiled under the old gate. The stamp's
# filename encodes the flag value; a different value names a file that does not exist, whose
# rule deletes the stale stamp and touches the new one, invalidating all objects. Defined
# AFTER `all:` so `all` stays the default goal (this target contains a slash, so make's
# leading-`.` default-goal exclusion would not protect it).
RAMDISK_STAMP := $(OBJDIR)/.ramdisk_initramfs_$(RAMDISK_INITRAMFS)
$(RAMDISK_STAMP):
	@rm -f $(OBJDIR)/.ramdisk_initramfs_*; touch $@
$(OBJS): $(RAMDISK_STAMP)

# Header dependency tracking. -MMD -MP (in CFLAGS above) emits a .d per object listing
# every header it includes, so editing a shared shim header (ntdef.h, bldr.h, fsboot.h,
# fatboot.h, ...) rebuilds the objects that include it. Without this, an object compiled
# against an old header layout stays stale and silently corrupts memory. Empty on a
# fresh tree (the wildcard matches nothing), which is correct - everything builds anyway.
-include $(wildcard $(OBJDIR)/*.d)

$(OBJDIR)/%.o: $(ARMDIR)/%.S
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(ARMDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(PORTEDDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(PORTEDDIR)/config/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- OEM font for the kernel-side HAL display (jxdisp.c) ---
# mkfont.py turns the 8x8 bitmaps in font8x8.h into an OEM_FONT_FILE_HEADER blob
# (the .FNT layout HalpInitializeDisplay0/HalpOutputCharacter read). The loader embeds
# it and hands the kernel a pointer via LoaderBlock->OemFontFile, exactly as the OS
# loader supplies the font on MIPS/Alpha. Wrapped in $(OBJDIR) for stable symbols
# _binary_font_fon_{start,end,size}. Needs python3 (present in arc-rpi-build).
$(OBJDIR)/font.fon: $(ARMDIR)/mkfont.py $(ARMDIR)/font8x8.h
	python3 $(ARMDIR)/mkfont.py $(ARMDIR)/font8x8.h $@

# --set-section-alignment 16: the kernel runs MMU-off, where ARMv7 treats memory as
# Strongly-ordered and faults EVERY unaligned LDRH/LDR regardless of SCTLR.A. The HAL
# reads the OEM header's USHORT fields (PixelWidth@86, PixelHeight@88, Map@118 - all
# even offsets) with LDRH, so the blob must start at an even address; a byte-aligned
# binary import lands it odd and the first font read data-aborts. 16 gives margin.
$(OBJDIR)/font_blob.o: $(OBJDIR)/font.fon
	cd $(OBJDIR) && $(OBJCOPY) -I binary -O elf32-littlearm -B arm \
		--set-section-alignment .data=16 font.fon font_blob.o

# --- RAM-disk image (the Arc block device under QEMU) ---
# obj/ramdisk.img is built OUT OF BAND by arcfw/ramdisk/make-ramdisk.sh (it needs
# mkfs.vfat/mtools, which the arc-rpi-build image lacks). The Makefile only wraps
# the prebuilt image into a linkable blob (_binary_ramdisk_img_{start,end,size}),
# run in $(OBJDIR) so no absolute path is baked into the symbol names.
$(OBJDIR)/ramdisk.img:
	@echo "ERROR: $@ is missing - build it first: (cd ../arcfw/ramdisk && ./make-ramdisk.sh)" >&2; exit 1

$(OBJDIR)/ramdisk_blob.o: $(OBJDIR)/ramdisk.img
	cd $(OBJDIR) && $(OBJCOPY) -I binary -O elf32-littlearm -B arm ramdisk.img ramdisk_blob.o

$(OBJDIR)/arcfw.elf: $(OBJS) arcfw.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(OBJDIR)/arcfw.bin: $(OBJDIR)/arcfw.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJDIR)/*.o $(OBJDIR)/*.elf $(OBJDIR)/*.bin $(OBJDIR)/*.d $(OBJDIR)/.ramdisk_initramfs_*
