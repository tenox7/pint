#ifndef _CVF_SHIM_
#define _CVF_SHIM_
//
// Stub for the DoubleSpace compressed-volume header (BOOT/INC/CVF.H). fatboot.h
// includes it unconditionally, but every CVF type (CVF_HEADER, CVF_LAYOUT, ...) is
// referenced only inside `#ifdef DBLSPACE_LEGAL`, which this port never defines (the
// FAT engine is built DBLSPACE-off, ~3988 lines). So this just needs to exist.
//
#endif // _CVF_SHIM_
