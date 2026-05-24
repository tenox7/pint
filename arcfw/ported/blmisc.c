/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    blmisc.c

Abstract:

    This module contains miscellaneous routines for use by
    the boot loader and setupldr.

Author:

    David N. Cutler (davec) 10-May-1991

Revision History:

--*/

//
// Ported verbatim from PRIVATE/NTOS/BOOT/LIB/BLMISC.C. The original includes
// "bootlib.h" (the full NT boot-lib umbrella header, not part of the curated
// header shim set); we substitute "bldr.h" (the shim that osloader.c uses) plus
// "ctype.h" for toupper(). The function body is unchanged - including the quirk
// that Argc is only decremented inside the `String != NULL` arm, which would
// loop forever on a NULL Argv slot; the synthesized Argv (init.c) has none.
//
#include "bldr.h"
#include "ctype.h"


PCHAR
BlGetArgumentValue (
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR ArgumentName
    )

/*++

Routine Description:

    This routine scans the specified argument list for the named argument
    and returns the address of the argument value. Argument strings are
    specified as:

        ArgumentName=ArgumentValue

    Argument names are specified as:

        ArgumentName=

    The argument name match is case insensitive.

Arguments:

    Argc - Supplies the number of argument strings that are to be scanned.

    Argv - Supplies a pointer to a vector of pointers to null terminated
        argument strings.

    ArgumentName - Supplies a pointer to a null terminated argument name.

Return Value:

    If the specified argument name is located, then a pointer to the argument
    value is returned as the function value. Otherwise, a value of NULL is
    returned.

--*/

{

    PCHAR Name;
    PCHAR String;

    //
    // Scan the argument strings until either a match is found or all of
    // the strings have been scanned.
    //

    while (Argc > 0) {
        String = Argv[Argc - 1];
        if (String != NULL) {
            Name = ArgumentName;
            while ((*Name != 0) && (*String != 0)) {
                if (toupper(*Name) != toupper(*String)) {
                    break;
                }

                Name += 1;
                String += 1;
            }

            if ((*Name == 0) && (*String == '=')) {
                return String + 1;
            }

            Argc -= 1;
        }
    }

    return NULL;
}
