/*
 * A compile for the single-threaded C run-time. NT's build defines _MT only for
 * the multithreaded ones (libcmt, crtdll); the compiler of today predefines it,
 * its default run-time being multithreaded.
 */

#undef _MT
