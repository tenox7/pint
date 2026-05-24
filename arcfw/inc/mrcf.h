#ifndef _MRCF_SHIM_
#define _MRCF_SHIM_
//
// Stub for the MRCF (DoubleSpace) compression decompressor header. FATBOOT.C includes
// it unconditionally, but every MRCF_* symbol is used only inside the
// `#ifdef DBLSPACE_LEGAL` block (FATBOOT.C lines >3988), which this port never
// defines. So this just needs to exist.
//
#endif // _MRCF_SHIM_
