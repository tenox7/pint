/*++

Copyright (c) 1994  Digital Equipment Corporation

Module Name:

    arcdos.c

Abstract:

    This provides primitive file command functionality for the ARC firmware.

Author:

    John DeRosa         25-August-1993

Environment:


Revision History:

    Iain Grant          27-July-1994    Added Make Directory command.

      "    "            01-August-1994  Doubled size of Buffer_move_amount
                                        to speed up file copying.

      "    "            10-August-1994  Added simple Directory Copy routine
                                        XCopy.

      "    "            17-August-1994  Split up XCopy so that it can call
                                        itself when it comes across
                                        sub-directorys.

                                        Added error message for Create 			
                                        Directory.

    Janet Schneider     12-October-1995 Added a check for an illegal filename.
                                        (For instance, when someone types:
                                        "copy a:\foo.bar .")

    Eric Nelson         18-December-1997 Allow single argument copy

--*/

//
// ARM loader port: the ADK headers above (arc.h/vendor.h/errno.h/stdlib.h/stdio.h/
// string.h) are replaced by one self-contained compat header. arcdos.c is otherwise
// verbatim. See arcfw/arcdos-inc/arcdos_compat.h and arcfw/arm/arcdos_rt.c.
//
#include "arcdos_compat.h"

//
// Number of command lines for up-arrow and down-arrow.
//

#define NUMBER_COMMAND_LINES	10

//
// Unit of file operations.
//

#define BUFFER_MOVE_AMOUNT	(1024 * 4)

//
// Status codes for the get-line function
//

typedef enum _GETLINE_STATUS {
    GetLineSuccess,
    GetLineUpArrow,
    GetLineDownArrow,
    GetLineError,
    GetLineMaximum
} GETLINE_STATUS, *PGETLINE_STATUS;

//
// General return codes
//

typedef enum _ARCDOS_STATUS {
    ADSUCCESS,                  // Success
    ADILLEGALFILENAME,          // Illegal filename
    ADUNDEFEV,                  // Undefined environment variable
    ADUNDEFCOMMAND,             // Undefined command
    ADNODEFAULT,                // No default location set yet
    ADBIGTOKEN,                 // Token too large
    ADBADDIR,                   // Problem creating directory
    ADEXITARCDOS,               // Please exit ARCDos now.
    ADERROR,                    // General error
    ADBADFILESPEC,              // Environment variable and ARC device present
                                // and/or .. present.
    ADINCORRECTARGS,            // Incorrect number of command arguments.
    ADFILEIO,                   // File I/O failed.
    ADMAXIMUM
} ARCDOS_STATUS, *PARCDOS_STATUS;

//
// Function prototypes
//

ARCDOS_STATUS
ARCDosCdCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosMdCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosAttribCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosCopyCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosDeleteCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosDirCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosDummyCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosExitCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosHelpCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosMoreCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosTypeCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
ARCDosXCopyCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    );

ARCDOS_STATUS
XCopy_Routine(
    IN PCHAR SourceDir,
    IN PCHAR DestinDir,
    IN ULONG Message_Flag
    );

ARCDOS_STATUS
Copy_Routine(
    IN PCHAR SourceFile,
    IN PCHAR DestinFile,
    IN ULONG Message_Flag
    );

ARCDOS_STATUS
Create_Dir_Routine(
    IN PCHAR Directory_Name
    );

//
// Command dispatch table
//

typedef ARCDOS_STATUS (*PCOMMAND_FUNCTION) (IN ULONG Argc, IN PCHAR Argv[]);

typedef struct _COMMAND_TABLE {
    PCHAR Verb;
    PCOMMAND_FUNCTION Function;
} COMMAND_TABLE, *PCOMMAND_TABLE;

#define COMMAND_TABLE_LENGTH	15

COMMAND_TABLE CommandTable [COMMAND_TABLE_LENGTH] = {
    { "?",	ARCDosHelpCommand },
    { "attrib", ARCDosAttribCommand },
    { "cd", 	ARCDosCdCommand },
    { "copy",	ARCDosCopyCommand },
    { "xcopy",  ARCDosXCopyCommand },
    { "del",	ARCDosDeleteCommand },
    { "dir",	ARCDosDirCommand },
    { "exit", 	ARCDosExitCommand },
    { "ex", 	ARCDosExitCommand },
    { "help", 	ARCDosHelpCommand },
    { "more", 	ARCDosMoreCommand },
    { "md",     ARCDosMdCommand },
    { "type", 	ARCDosTypeCommand },
    { "q", 	ARCDosExitCommand },
    { "quit", 	ARCDosExitCommand },

};

//
// The parsed command line.
//

#define MAXIMUM_USER_COMMAND_LINE	128
#define MAXIMUM_TOKEN_LENGTH		 80
#define MAXIMUM_TOKENS			  3

CHAR Token1[MAXIMUM_TOKEN_LENGTH];
CHAR Token2[MAXIMUM_TOKEN_LENGTH];
CHAR Token3[MAXIMUM_TOKEN_LENGTH];
PCHAR Argv[] = {Token1, Token2, Token3};

//
// The default ARC path
//

#define MAXIMUM_LENGTH_DEFAULT_ARC_PATH	80

UCHAR DefaultARCPath[MAXIMUM_LENGTH_DEFAULT_ARC_PATH];


ARC_STATUS
OpenFile(
    IN PCHAR File,
    IN ULONG OpenMode,
    OUT PULONG FileId
    )
/*++

Routine Description:

    This routine opens the file in the specified mode.

Arguments:

    File                A pointer to the filename.

    OpenMode            The desired mode.

    FileId              A pointer to the returned FileId.

Return Value:

    ESUCCESS or an error code.

--*/
{
    ARC_STATUS Status;

    Status = Open (File, OpenMode, FileId);

    if (Status != ESUCCESS) {
        Print("? Open failed on %s.\r\n", File);
        Print("  Status returned = %x.\r\n", Status);
        return (ENOTTY);
    } else {
        return (ESUCCESS);
    }
}


ARC_STATUS
CheckFileIOResults(
    IN ARC_STATUS Status,
    IN ULONG WantedToMove,
    IN ULONG DidMove
    )
/*++

Routine Description:


    This routine checks the results of a file I/O operation.

Arguments:

    Status              The status returned from the I/O operation.

    WantedToMove        The number of bytes we tried to move.

    DidMove	            The number of bytes that were moved.

Return Value:

    ESUCCESS if the file I/O was OK.

    Otherwise, an error code.

--*/
{
    if (Status != ESUCCESS) {
        Print("? File I/O failure, code = %x\r\n", Status);
    }

    if (DidMove != WantedToMove) {
        Print("? Incorrect number of bytes were read or written.\r\n");
        Print("  Tried to move 0x%x and only did 0x%x.\r\n", WantedToMove, DidMove);
    }

    if ((Status != ESUCCESS) || (DidMove != WantedToMove)) {
        return (EIO);
    } else {
        return (ESUCCESS);
    }
}


ARCDOS_STATUS
Copy_Routine(
    IN PCHAR SourceFile,
    IN PCHAR DestinFile,
    IN ULONG Message_Flag
    )
/*++

Routine Description:

    This code is used by both ARCDosCopyCommand and ARCDosXCopyCommand to
    copy files.

Arguments:

    SourceFile              The name of the source file.

    DestinFile              The name of the destination file.

    Message_Flag            If 0 then Don't print copying countdown
                            If 1 print copying countdown

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;
    CHAR Buffer[BUFFER_MOVE_AMOUNT];
    ULONG Count;
    ULONG SrcFileId;
    ULONG DstFileId;
    FILE_INFORMATION Finfo;
    ULONGLONG SrcLength;
    ULONG CountdownValue;
    ARC_DISPLAY_STATUS *DisplayStatus;
    ULONG ScreenColumn;
    ULONG ScreenRow;
    ULONG ReadAmount;

    if (OpenFile(SourceFile, OpenReadOnly, &SrcFileId) != ESUCCESS) {
        return (ADFILEIO);
    }

    if (OpenFile(DestinFile, SupersedeWriteOnly, &DstFileId) != ESUCCESS) {
        Close(SrcFileId);
        return (ADFILEIO);
    }

    //
    // Get the file size
    //

    Status = GetFileInformation(SrcFileId, &Finfo);
    if ((Status != ESUCCESS) || (Finfo.StartingAddress.QuadPart != 0)) {
        Print("? Error on retreival of file information, status = %x\r\n", Status);
        Close(SrcFileId);
        Close(DstFileId);
        return (ADFILEIO);
    }
    SrcLength = Finfo.EndingAddress.QuadPart;
    CountdownValue = (ULONG)(SrcLength / BUFFER_MOVE_AMOUNT);

    //
    // Copy from source to destination, counting down to give the user
    // an indicate of progress.
    //

    if(Message_Flag == 1)
    {
       Print("Copying... ");
       DisplayStatus = GetDisplayStatus(StandardIn);
       ScreenColumn = DisplayStatus->CursorXPosition;
       ScreenRow = DisplayStatus->CursorYPosition;
    }

    while (SrcLength > 0) {

        if(Message_Flag == 1)
        {
           VenSetPosition(ScreenRow, ScreenColumn);
           Print("%cK", ASCII_CSI);		// Clear to end of line
           Print("%d", CountdownValue);
        }

        if (SrcLength > BUFFER_MOVE_AMOUNT) {
            ReadAmount = BUFFER_MOVE_AMOUNT;
        } else {
            ReadAmount = (ULONG)SrcLength;
        }

        Status = Read (SrcFileId, Buffer, ReadAmount, &Count);

        if (CheckFileIOResults(Status, ReadAmount, Count) != ESUCCESS) {
            Close(SrcFileId);
            Close(DstFileId);
            return (ADFILEIO);
        }

        Status = Write (DstFileId, Buffer, ReadAmount, &Count);

        if (CheckFileIOResults(Status, ReadAmount, Count) != ESUCCESS) {
            Close(SrcFileId);
            Close(DstFileId);
            return (ADFILEIO);
        }

        SrcLength -= ReadAmount;
        --CountdownValue;
    }

    if(Message_Flag == 1)
    {
       VenSetPosition(ScreenRow, ScreenColumn);
       Print("%cK", ASCII_CSI);		// Clear to end of line
       Print("...done.\r\n");
    }

    Close (SrcFileId);
    Status = Close (DstFileId);

    if (Status != ESUCCESS) {
        Print("? Cannot close output file, status = %x.\r\n", Status);
        Print("  File and media state may be inconsistent.\r\n");
        return (ADFILEIO);
    }
}


ARCDOS_STATUS
ARCDosAttribCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the attrib command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
   ULONG FileId;
   ULONG Message_Type;
   FILE_INFORMATION Finfo;
   UCHAR FatString[8];
   ARCDOS_STATUS Status;

    if (Argc < 2) {
        return (ADINCORRECTARGS);
    }

    //
    // open the file
    //

    Status = Open (Argv[Argc-1], OpenReadOnly, &FileId);

    if (Status != ESUCCESS) {
        Print("? Open failed on %s.\r\n", Argv[1]);
        Print("  Status returned = %x.\r\n", Status);
        Print ("  Is this a directory?  Does it exist?\r\n");
        return (ADERROR);
    }

    if (Argc < 3) {

        //
        // 2 args -- assume final arg is filename, get attrs
        //

        Status = GetFileInformation(FileId, &Finfo);

        if (Status != ESUCCESS) {
            Print("? Cannot get file attributes.\r\n");
            Print("  File = %s, Status returned = %x.\r\n", Argv[Argc-1], Status);
            return (ADERROR);
        }

        strcpy(FatString, "    ");

        if ((Finfo.Attributes & ArchiveFile) != 0)  FatString[0] = 'A';
        if ((Finfo.Attributes & SystemFile) != 0)   FatString[1] = 'S';
        if ((Finfo.Attributes & HiddenFile) != 0)   FatString[2] = 'H';
        if ((Finfo.Attributes & ReadOnlyFile) != 0) FatString[3] = 'R';

        Print(" %s    %s\r\n",FatString,Argv[Argc-1]);

    } else {

        //
        // 3 or more args -- assume final arg is filename
        // and remaining args are +<attr> or -<attr>
        //
        // [wemfix] Only one attribute arg at a time currently works.
        // Symptom is "Incorrect number of command arguments message"
        // This has not been investigated, but one likely cause is the
        // a command buffer overflowing due to the path info being added
        // and causing Argc to be zero or negative.
        //

        int ArcMask;
        int ArcSettings = 0;
        int i, j;
        int Attr, AttrLen;
        BOOLEAN SetAttr;
        BOOLEAN PlusMinusSeen;

        for (i=1;i<(int)(Argc-1);i++) {

            //
            // parse next arg
            //
            // Note that caller has added cd path string, so 
            // look at the end for <+-><ashrASHR>
            //

            AttrLen = strlen(Argv[i]);

            if (AttrLen<2) {
                Print("? Illegal command.\r\n");     // arg string is too short
                return (ADERROR);
            }
                        
            ArcMask = 0;
            PlusMinusSeen = FALSE;
            
            for (j=AttrLen - 1; j>=0; j--) {

                switch ((Argv[i])[j]) {                   // parse <+-><ashrASHR>

                    case '+':
                        SetAttr = TRUE;
                        PlusMinusSeen = TRUE;
                        Attr = 0;
                        j = 0;                           // + seen, terminate loop
                        break;

                    case '-':
                        SetAttr = FALSE;
                        PlusMinusSeen = TRUE;
                        Attr = 0;
                        j = 0;                          // - seen, terminate loop
                        break;

                    case 'a':
                    case 'A':

                        Attr = ArchiveFile;
                        break;

                    case 's':
                    case 'S':

                        Attr = SystemFile;
                        break;

                    case 'h':
                    case 'H':

                        Attr = HiddenFile;
                        break;

                    case 'r':
                    case 'R':

                        Attr = ReadOnlyFile;
                        break;

                    default:
                        Print("? Illegal command.\r\n");
                        return (ADERROR);
                }

                ArcMask |= Attr;                                    // update mask
            }

            if (PlusMinusSeen) {
                if (SetAttr) {

                    ArcSettings |= ArcMask;                         // set attribute

                } else {

                    ArcSettings &= ~ArcMask;                        // clear attribute

                }
            } else {

                Print("? Illegal command.\r\n");
                return (ADERROR);
            }

        }

        //
        // apply changes
        //

        Status = SetFileInformation(FileId, ArcSettings, ArcMask);

        if (Status != ESUCCESS) {
            Print("? Cannot set file's attributes.\r\n");
            Print("  File = %s, Status returned = %x.\r\n", Argv[1], Status);
            Print ("  Is the device writable?\r\n");
            return (ADERROR);
        }
    }

    //
    // done with file
    //

    Status = Close (FileId);

    if (Status != ESUCCESS) {
        Print("? Cannot close file.  It may be left in a bad state.\r\n");
        Print("  File = %s, Status returned = %x.\r\n", Argv[1], Status);
        Print ("  Is the device writable?\r\n");
        return (ADERROR);
    }

    return (ADSUCCESS);

}



ARCDOS_STATUS
ARCDosCopyCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the copy command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
   ULONG Message_Type;
   ARCDOS_STATUS Status;
   CHAR Buffer[80] = { '\0', };

    if ((Argc < 2) || (Argc > 3)) {
        return (ADINCORRECTARGS);
    }

    //
    // We'll allow a single '.' for destination, but we're not gonna parse
    // ..\..\..\., etc.
    //
    if (Argc == 3) {
        strcpy(Buffer, DefaultARCPath);
        strcat(Buffer, "\\.");

        if (strcmp(Argv[2], Buffer) == 0) {
            Argc = 2;

        } else if (strstr(Argv[2], "\\.") != 0) {
            Print("? Illegal destination = %s\r\n", Argv[2] );
            return (ADILLEGALFILENAME);
            
        } else {                
            strcpy(Buffer, Argv[2]);
        }
    }

    if (Argc == 2) {
        int Len;
        char *pCh;
        
        //
        // If the default path is not set, then no single argument copy!
        //
        if (DefaultARCPath[0] == 0) {
            Print(" No default ARC path set.\r\n");
            return (ADSUCCESS);
        }

        //
        // If source and destination are the same, just return
        //
        if (strstr(Argv[1], DefaultARCPath) != NULL) {
            return (ADSUCCESS);
        }

        //
        // Strip filename from end of Argv[1]
        //
        Len = strlen(Argv[1]) - 1;
        pCh = &Argv[1][Len];
        
        while ((Len > 0) && (*pCh != ')') && (*pCh != '\\')) {
            Len--;
            pCh--;
        }

        if (Len == 0) {
            Print("Bad file name = %s\r\n", Argv[1]);
            return (ADSUCCESS);
        }

        strcpy(Buffer, DefaultARCPath);
        if (*pCh != '\\') {
            strcat(Buffer, "\\");
            strcat(Buffer, pCh + 1);
        } else {
            strcat(Buffer, pCh);
        }
    }

    Message_Type = 1;
    Status = Copy_Routine(Argv[1], Buffer, Message_Type);
    return (Status);
}

ARCDOS_STATUS
Create_Dir_Routine(
    IN PCHAR DirName
    )
/*++

Routine Description:

    This code is used by both ARCDosMdCommand and ARCDosXCopyCommand to
    create directories.

Arguments:

    DirName             The name of the directory to create.

Return Value:

    A code reflecting success or failure.

--*/
{
   ARC_STATUS Exist_Status;
   ARC_STATUS Create_Status;
   ULONG FileID;
   CHAR Location[100];


    strcpy (Location, DirName);

    // Check to see if destination directory exists

    Exist_Status = Open (Location, OpenDirectory, &FileID);

    if (Exist_Status != ESUCCESS) {
        // Try appending a directory slash.
        Exist_Status = Open (strcat(Location, "\\"), OpenDirectory, &FileID);
    }
    if (Exist_Status != ESUCCESS) {
       Close(FileID);


       // Directory needs to be created
       Create_Status = Open (DirName, CreateDirectory, &FileID);

       if (Create_Status != ESUCCESS) {
          Print( "Problem creating directory %s. \r\n",Location);
          Close(FileID);
          return (ADBADDIR);
       }
       else
       if (Create_Status == ESUCCESS) {
          Print( "Created Directory %s.\r\n",Location);
          Close(FileID);
          return (ADSUCCESS);
       }
    }
    else
    if (Exist_Status == ESUCCESS) {
       Print( "Directory already exists %s.\r\n",Location);
       return (ADSUCCESS);
    }
}



ARCDOS_STATUS
XCopy_Routine(
    IN PCHAR SourceDir,
    IN PCHAR DestinDir,
    IN ULONG Message_Flag
    )
/*++

Routine Description:

    This implements the xcopy command. It is used to copy directory
    structures from one disk to another.

    Example of command entered :

    	xcopy scsi(0)disk(5)rdisk(0)partition(2)\alpha
    		scsi(0)disk(0)rdisk(0)partition(2)\alpha

    The command has been split over two lines. It would copy the alpha
    directory from scsi disk 5 partition 2 to scsi disk 0 partition 2.

Arguments:

    SourceDir               The name of the source directory.

    DestinDir               The name of the destination directory.

    Message_Flag            If 0 then Don't print XCOPY to from message
                            If 1 print the XCOPY to from message

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;
    ARCDOS_STATUS Copy_Status;
    BOOLEAN AtLeastOneDir;
    CHAR SourceBuffer[80];
    CHAR DestinBuffer[80];
    CHAR Location[80];
    CHAR OutputBuffer[MAXIMUM_TOKEN_LENGTH];
    CHAR FileName[MAXIMUM_TOKEN_LENGTH];
    CHAR SourceFile[MAXIMUM_TOKEN_LENGTH];
    CHAR DestinFile[MAXIMUM_TOKEN_LENGTH];
    DIRECTORY_ENTRY DirectoryEntry;
    ULONG Count;
    ULONG FileId;
    ULONG Message_Type;

    // Move Directory Source and Destination to appropriate buffers
    strcpy (SourceBuffer, SourceDir);
    strcpy (DestinBuffer, DestinDir);

    // If we have been passed the message flag of 1 then print the
    // XCOPY to from message
    if (Message_Flag == 1) {
       Print("XCOPY of %s.\r\n", SourceBuffer);
       Print("      to %s.\r\n", DestinBuffer);
    }

    // Check that Destination Directory does not already exist
    strcpy (Location, DestinBuffer);

    Status = Open (DestinBuffer, OpenDirectory, &FileId);

    if (Status != ESUCCESS) {
       // The destination directory was not found, so let's create it
       Status = Create_Dir_Routine(DestinBuffer);
       // If at this point we still can't create the directory display an error.
       if(Status != ADSUCCESS) {
          return (ADERROR);
       }
    }

    // Get the File list from the source Directory

    Status = Open (SourceBuffer, OpenDirectory, &FileId);

    if (Status != ESUCCESS) {
        // Try appending a directory slash.
        Status = Open (strcat(SourceBuffer, "\\"), OpenDirectory, &FileId);
    }
    if (Status != ESUCCESS) {
        Print("? Open failed on %s.\r\n", SourceBuffer);
        Print("  Status returned = %x.\r\n", Status);
        Print ("  Is this a directory?  Does it exist?\r\n");
        return (ADERROR);
    }

    // Get list of files and copy them one by one.

    AtLeastOneDir = FALSE;
    Copy_Status = ADSUCCESS;
    Message_Type = 0;

    while ((Status == ESUCCESS) & (Copy_Status == ADSUCCESS)) {

        Status = GetDirectoryEntry(FileId, &DirectoryEntry, 1, &Count);

        if (Status == ESUCCESS) {

            AtLeastOneDir = TRUE;

            //
            // Call the Copy Command.
            //

            strcpy(FileName,DirectoryEntry.FileName);

            //
            // If this is a Directory, re-call yourself to copy it to the
            // destination.
            //

            if (DirectoryEntry.FileAttribute & DirectoryFile) {
                if(FileName[0] != '.')
                {
                    strcpy(SourceFile,SourceBuffer);
                    strcat(SourceFile,"\\");
                    strcat(SourceFile,FileName);
                    strcat(SourceFile,"\0");
                    strcpy(DestinFile,DestinBuffer);
                    strcat(DestinFile,"\\");
                    strcat(DestinFile,FileName);
                    strcat(DestinFile,"\0");
                    Copy_Status =
                        XCopy_Routine(SourceFile,DestinFile,Message_Type);
                }
            }
            else
            {
                //
                // It's a file so call the Copy_Routine to copy it
                //

                strcpy(SourceFile,SourceBuffer);
                strcat(SourceFile,"\\");
                strcat(SourceFile,FileName);
                strcat(SourceFile,"\0");
                strcpy(DestinFile,DestinBuffer);
                strcat(DestinFile,"\\");
                strcat(DestinFile,FileName);
                strcat(DestinFile,"\0");
                Print("Copying %s to %s.\r\n",FileName,DestinFile);
                Copy_Status = Copy_Routine(SourceFile,DestinFile,Message_Type);
            }
        }
    }

    //
    // Process the last status code.
    //

    Close (FileId);

    if (Status == ENOTDIR) {
        if (AtLeastOneDir == FALSE) {
            Print("  <None>\r\n");
        }
        return (ADSUCCESS);
    }

    if (Status == EBADF) {
        Print("? Directory operations are not supported.\r\n");
        return (ADERROR);
    }

    //
    // If we get here, there was some error condition returned by
    // ArcGetDirectoryEntry.
    //

    Print("? Error on directory entry.  Status = %x\r\n", Status);

    return (ADERROR);
}


ARCDOS_STATUS
ARCDosXCopyCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the Xcopy command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
   ULONG Message_Type;
   ARCDOS_STATUS Status;

    if (Argc != 3) {
        return (ADINCORRECTARGS);
    }

    Message_Type = 1;
    Status = XCopy_Routine(Argv[1],Argv[2],Message_Type);
    return (Status);
}





ARCDOS_STATUS
ARCDosDeleteCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the delete command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;
    ULONG FileId;

    if (Argc != 2) {
        return (ADINCORRECTARGS);
    }

    Status = Open (Argv[1], OpenReadOnly, &FileId);

    if (Status != ESUCCESS) {
        Print("? Open failed on %s.\r\n", Argv[1]);
        Print("  Status returned = %x.\r\n", Status);
        Print ("  Is this a directory?  Does it exist?\r\n");
        return (ADERROR);
    }

    Status = SetFileInformation(FileId, DeleteFile, DeleteFile);

    if (Status != ESUCCESS) {
        Print("? Cannot mark file for deletion.\r\n");
        Print("  File = %s, Status returned = %x.\r\n", Argv[1], Status);
        Print ("  Is the device writable?\r\n");
        return (ADERROR);
    }

    Status = Close (FileId);

    if (Status != ESUCCESS) {
        Print("? Cannot close file.  It may be left in a bad state.\r\n");
        Print("  File = %s, Status returned = %x.\r\n", Argv[1], Status);
        Print ("  Is the device writable?\r\n");
        return (ADERROR);
    }

    return (ADSUCCESS);
}


ARCDOS_STATUS
ARCDosDirCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the dir command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;
    BOOLEAN AtLeastOneDir;
    CHAR TempBuffer[MAXIMUM_TOKEN_LENGTH];
    DIRECTORY_ENTRY DirectoryEntry;
    ULONG Count;
    ULONG FileId;
    ULONG Index;

    switch (Argc) {

      //
      // Directory of current location.
      //

      case 1:

        if (DefaultARCPath[0] == 0) {
            return (ADNODEFAULT);
        }
        strcpy (TempBuffer, DefaultARCPath);
        break;

      //
      // Directory of specified location.
      //

      case 2:

        strcpy (TempBuffer, Argv[1]);
        break;

      //
      // Incorrect number of arguments.
      //

      default:
         return (ADINCORRECTARGS);

    }

    Status = Open (TempBuffer, OpenDirectory, &FileId);

    if (Status != ESUCCESS) {
        // Try appending a directory slash.
        Status = Open (strcat(TempBuffer, "\\"), OpenDirectory, &FileId);
    }
    if (Status != ESUCCESS) {
        Print("? Open failed on %s.\r\n", TempBuffer);
        Print("  Status returned = %x.\r\n", Status);
        Print ("  Is this a directory?  Does it exist?\r\n");
        return (ADERROR);
    }

    Print("Directory of %s.\r\n", TempBuffer);
    AtLeastOneDir = FALSE;

    while (Status == ESUCCESS) {

        Status = GetDirectoryEntry(FileId, &DirectoryEntry, 1, &Count);

        if (Status == ESUCCESS) {

            AtLeastOneDir = TRUE;

            //
            // Print the filename.
            //

            for (Index = 0; Index < DirectoryEntry.FileNameLength; Index++) {
                Print("%c", DirectoryEntry.FileName[Index]);
            }

            //
            // Space over and print <DIR> if it is a directory.
            //

            while (Index < 15) {
                Print(" ");
                Index++;
            }
            if (DirectoryEntry.FileAttribute & DirectoryFile) {
                Print("<DIR>");
                Index = Index + 5;
            }

            //
            // Space over and print the attribute.
            //

            while (Index < 22) {
                Print(" ");
                Index++;
            }

            if (DirectoryEntry.FileAttribute & ReadOnlyFile) {
                Print("R");
            }
            if (DirectoryEntry.FileAttribute & HiddenFile) {
                Print("H");
            }
            if (DirectoryEntry.FileAttribute & SystemFile) {
                Print("S");
            }
            if (DirectoryEntry.FileAttribute & ArchiveFile) {
                Print("A");
            }
            if (DirectoryEntry.FileAttribute & DeleteFile) {
                Print(" <DEL>");
            }
            Print("\r\n");
        }
    }

    //
    // Process the last status code.
    //

    Close (FileId);

    if (Status == ENOTDIR) {
        if (AtLeastOneDir == FALSE) {
            Print("  <None>\r\n");
        }
        return (ADSUCCESS);
    }

    if (Status == EBADF) {
        Print("? Directory operations are not supported.\r\n");
        return (ADERROR);
    }

    //
    // If we get here, there was some error condition returned by
    // ArcGetDirectoryEntry.
    //

    Print("? Error on directory entry.  Status = %x\r\n", Status);

    return (ADERROR);
}


ARCDOS_STATUS
ARCDosExitCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the exit command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    return (ADEXITARCDOS);
}


ARCDOS_STATUS
ARCDosHelpCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This prints out help text.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    Print("This provides simple file handling commands to help you set up\r\n");
    Print("the system for booting.\r\n\n");
    Print("   ?                                exit\r\n");
    Print("   cd devicepath                    help\r\n");
    Print("   copy src dest (optional)         md devicepath\r\n");
    Print("   xcopy sourcepath destinpath      more file\r\n");
    Print("   del file                         type file\r\n");
    Print("   dir [directory]                  attrib file\r\n");
    Print("   quit\r\n\n");
    Print("File, directory, and device names must be an ARC string or an environment\r\n");
    Print("variable.  Environment variables can be defined with the NT firmware.\r\n");
    Print("Example:   cd eisa()disk()fdisk()\\alpha\r\n");
    Print("           copy setupldr c:\\winnt\\setupldr.sav\r\n");
    return (ADSUCCESS);
}


ARCDOS_STATUS
ARCDosCdCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the cd command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    if (Argc > 2) {
        return (ADINCORRECTARGS);
    }

    //
    // cd command with no arguments = print current default.
    //

    if ((Argc == 1) && (DefaultARCPath[0] == 0)) {
        Print(" No default ARC path set.\r\n");
        return (ADSUCCESS);
    }

    if (Argc == 1) {
        Print(" Default ARC path = %s.\r\n", DefaultARCPath);
        return (ADSUCCESS);
    }

    //
    // cd <target>
    //

    if (strlen(Argv[1]) > MAXIMUM_LENGTH_DEFAULT_ARC_PATH) {
        return (ADBIGTOKEN);
    }

    strcpy (DefaultARCPath, Argv[1]);

    Print(" Default ARC path = %s.\r\n", DefaultARCPath);

    return (ADSUCCESS);
}


ARCDOS_STATUS
ARCDosMdCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the md command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;

    if (Argc > 2) {
        return (ADINCORRECTARGS);
    }

    //
    // md command with no arguments = print current default.
    //

    if ((Argc == 1) && (DefaultARCPath[0] == 0)) {
        Print(" No directory ARC path to create.\r\n");
        return (ADSUCCESS);
    }

    if (Argc == 1) {
        Print(" No directory to create.\r\n");
        return (ADSUCCESS);
    }

    //
    // md <target>
    //

    Status = Create_Dir_Routine(Argv[1]);
    return (Status);

}



ARCDOS_STATUS
ARCDosMoreCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the type command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;
    UCHAR Character;
    CHAR Buffer[BUFFER_MOVE_AMOUNT];
    PCHAR BufferPointer;
    ULONG Count;
    ULONG SrcFileId;
    ULONG DstFileId;
    FILE_INFORMATION Finfo;
    ULONGLONG SrcLength;
    ULONG ReadAmount;
    ULONG ScreenLineCount;
    ULONG BufferIndex;

#define	MORE_PAUSE_AMOUNT	24

    if (Argc != 2) {
        return (ADINCORRECTARGS);
    }

    if (OpenFile(Argv[1], OpenReadOnly, &SrcFileId) != ESUCCESS) {
        return (ADFILEIO);
    }

    DstFileId = StandardOut;

    //
    // Get the file size
    //

    Status = GetFileInformation(SrcFileId, &Finfo);

    if ((Status != ESUCCESS) || (Finfo.StartingAddress.QuadPart != 0)) {
        Print("? Error on retreival of file information, status = %x\r\n", Status);
        Close(SrcFileId);
        return (ADFILEIO);
    }

    SrcLength = Finfo.EndingAddress.QuadPart;

    //
    // Copy from source to destination
    //

    ScreenLineCount = 0;

    while (SrcLength > 0) {

        if (SrcLength > BUFFER_MOVE_AMOUNT) {
            ReadAmount = BUFFER_MOVE_AMOUNT;
        } else {
            ReadAmount = (ULONG)SrcLength;
        }

        Status = Read (SrcFileId, Buffer, ReadAmount, &Count);

        if (CheckFileIOResults(Status, ReadAmount, Count) != ESUCCESS) {
            Close(SrcFileId);
            return (ADFILEIO);
        }

        //
        // We have the next buffer from the input file.  Write it to the
        // output device, pausing every screenful.
        //

        BufferIndex = 0;
        BufferPointer = Buffer;

        while (BufferIndex < ReadAmount) {

            //
            // Print a line.
            //

            do {
                Status = Write (DstFileId, BufferPointer, 1, &Count);

                if (CheckFileIOResults(Status, 1, Count) != ESUCCESS) {
                    Close(SrcFileId);
                    return (ADFILEIO);
                }

                BufferIndex++;

                if (*BufferPointer++ == '\n') {
                    ScreenLineCount++;
                    break;
                }

            } while (BufferIndex < ReadAmount);

            //
            // One line has been printed, and/or we have printed the
            // entire input buffer.
            //

            if (ScreenLineCount >= MORE_PAUSE_AMOUNT) {

	        Print("--more-- ");

                Read(StandardIn, &Character, 1, &Count);
                Print("%c\r\n", Character);

                switch (Character) {

                    //
                    // Type next screenful.
                    //

                    case ' ':
                        ScreenLineCount = 0;
                        break;


                    //
                    // Quit now.
                    //

                    case 'q':
                    case 'Q':
                    default:

                        Close (SrcFileId);
                        return (ADSUCCESS);

 	            }
            }
        }

        //
        // The input buffer has been exhausted.  Loop to read the next
        // buffer amount from the input file.  ScreenLineCount has the
        // current screen line count.
        //

        SrcLength -= ReadAmount;

    }

    Close (SrcFileId);

    return (ADSUCCESS);

}


ARCDOS_STATUS
ARCDosTypeCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This implements the type command.

Arguments:

    Argc                The number of command line arguments.

    Argv[]              The command line tokens in standard C format.

Return Value:

    A code reflecting success or failure.

--*/
{
    ARC_STATUS Status;
    CHAR Buffer[BUFFER_MOVE_AMOUNT];
    ULONG Count;
    ULONG SrcFileId;
    ULONG DstFileId;
    FILE_INFORMATION Finfo;
    ULONGLONG SrcLength;
    ULONG ReadAmount;

    if (Argc != 2) {
        return (ADINCORRECTARGS);
    }

    if (OpenFile(Argv[1], OpenReadOnly, &SrcFileId) != ESUCCESS) {
        return (ADFILEIO);
    }

    DstFileId = StandardOut;

    //
    // Get the file size
    //

    Status = GetFileInformation(SrcFileId, &Finfo);

    if ((Status != ESUCCESS) || (Finfo.StartingAddress.QuadPart != 0)) {
        Print("? Error on retreival of file information, status = %x\r\n", Status);
        Close(SrcFileId);
        return (ADFILEIO);
    }

    SrcLength = Finfo.EndingAddress.QuadPart;

    //
    // Copy from source to destination.
    //

    while (SrcLength > 0) {

        if (SrcLength > BUFFER_MOVE_AMOUNT) {
            ReadAmount = BUFFER_MOVE_AMOUNT;
        } else {
            ReadAmount = (ULONG)SrcLength;
        }

        Status = Read (SrcFileId, Buffer, ReadAmount, &Count);

        if (CheckFileIOResults(Status, ReadAmount, Count) != ESUCCESS) {
            Close(SrcFileId);
            return (ADFILEIO);
        }

        Status = Write (DstFileId, Buffer, ReadAmount, &Count);

        if (CheckFileIOResults(Status, ReadAmount, Count) != ESUCCESS) {
            Close(SrcFileId);
            return (ADFILEIO);
        }

        SrcLength -= ReadAmount;
    }

    Close (SrcFileId);

    return (ADSUCCESS);

}


#if 0

ARCDOS_STATUS
ARCDosDummyCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This is a dummy routine for debugging the command line parser.

Arguments:

    Argc

    Argv[]

Return Value:

    A code reflecting success or failure.

--*/
{
    ULONG Index;

    Print("DummyCommand, Argc = %x\r\n", Argc);

    if (Argc > MAXIMUM_TOKENS) {
        Print("    ?? Greater than %x.\r\n", MAXIMUM_TOKENS);
    }

    for (Index = 0; Index < MAXIMUM_TOKENS; Index++) {

        // Remember, Index is zero-based and Argc is one-based.
        if (Index == Argc) {
            break;
        }

        Print("    strlen(Argv[%x])=%x\r\n",
                  Index,
                  strlen(Argv[Index]));

        Print("    Argv[%x]=%s\r\n", Index, Argv[Index]);
    }

    return (ADSUCCESS);
}

#endif


GETLINE_STATUS
GetCommandLine (
    IN PUCHAR Prompt,
    IN BOOLEAN UseExistingCommand,
    IN ULONG BufferLength,
    OUT PUCHAR Buffer
    )
/*++

Routine Description:

    This routine gets a command line from the console input device,
    starting at the current screen position.

    "Enter", "Escape", or Buffer length exceeded will terminate the command
    line.
 
    The input line is terminated with a NULL.

Arguments:

    Prompt              A pointer to the prompt string.

    UseExistingCommand  TRUE if this should start with the current value
                        of Buffer.  This is used to implement command
                        recall.

    BufferLength        The length of the buffer where the input stream
                        is to be stored.

    Buffer              A pointer to the buffer where the input stream is
                        to be stored.

Return Value:

    A code reflecting success or failure, or an up or down arrow.

--*/
{
    GETLINE_STATUS GetLineStatus;
    ARC_DISPLAY_STATUS *DisplayStatus;
    ULONG ScreenColumn;
    ULONG ScreenRow;
    ULONG PromptLength;
    ARC_STATUS Status;
    UCHAR Character;
    ULONG Count;
    PUCHAR EndOfBuffer;
    PUCHAR Cursor;
    PUCHAR CopyPointer;

    //
    // Get our current screen position.
    //

    DisplayStatus = GetDisplayStatus(StandardIn);
    ScreenColumn = DisplayStatus->CursorXPosition;
    ScreenRow = DisplayStatus->CursorYPosition;

    PromptLength = strlen(Prompt);

    //
    // Start with either a zero-terminated null command line, or the
    // current value of the retrieved command.
    //

    if (UseExistingCommand == TRUE) {
        EndOfBuffer = strchr(Buffer, '\0');
    } else {
        *Buffer = 0;
        EndOfBuffer = Buffer;
    }

    Cursor = EndOfBuffer;

    while (TRUE) {

        //
        // Clear the prompt region and reprint the prompt and current
        // command line, with the cursor reverse-videoed.
        //

        VenSetPosition(ScreenRow, ScreenColumn);
        Print("%cK", ASCII_CSI);		// Clear to end of line
        Print("%s%s", Prompt, Buffer);

        VenSetScreenAttributes(TRUE,FALSE,TRUE);
        VenSetPosition(ScreenRow, (Cursor - Buffer) + PromptLength + ScreenColumn);

        if (Cursor >= EndOfBuffer) {
            Print(" ");
        } else {
            Write(StandardOut,Cursor,1,&Count);
        }

        VenSetScreenAttributes(TRUE,FALSE,FALSE);

        //
        // Get a character.
        //

        Status = Read(StandardIn, &Character, 1, &Count);

        if (Status != ESUCCESS) {

            //
            // There was a problem with reading from the console.
            // Take an error return.
            //

            *Buffer = 0;
            GetLineStatus = GetLineError;
            goto ExitRoutine;
        }

        if ((ULONG)(EndOfBuffer - Buffer) == BufferLength) {

            //
            // The Buffer is about to overflow.  Return.
            //

            GetLineStatus = GetLineSuccess;
            goto ExitRoutine;
        }

        switch (Character) {

        case ASCII_ESC:

            //
            // If there is another character available, look to see if
            // this a control sequence, and fall through to ASCII_CSI.
            // This is an attempt to make escape sequences from a terminal work.
            //

            StallExecution(10000);

            if (GetReadStatus(StandardIn) == ESUCCESS) {
                Read(StandardIn, &Character, 1, &Count);
                if (Character != '[') {
                    GetLineStatus = GetLineSuccess;
                    goto ExitRoutine;
                }
            } else {
                GetLineStatus = GetLineSuccess;
                goto ExitRoutine;
            }

        case ASCII_CSI:

            Read(StandardIn, &Character, 1, &Count);

            switch (Character) {

                //
                // Up arrow.
                //

                case 'A':
                    GetLineStatus = GetLineUpArrow;
                    goto ExitRoutine;

                //
                // Left arrow.
                //

                case 'D':
                    if (Cursor != Buffer) {
                        Cursor--;
                    }
                    continue;

                //
                // Down arrow.
                //

                case 'B':
                    GetLineStatus = GetLineDownArrow;
                    goto ExitRoutine;

                //
                // Right arrow.
                //

                case 'C':
                    if (Cursor != EndOfBuffer) {
                        Cursor++;
                    }
                    continue;

                //
                // Home key
                //

                case 'H':
                    Cursor = Buffer;
                    continue;

                //
                // End key
                //

                case 'K':
                    Cursor = EndOfBuffer;
                    continue;

                //
                // Delete key
                //

                case 'P':
                    CopyPointer = Cursor;
                    while (*CopyPointer) {
                        *CopyPointer = *(CopyPointer + 1);
                        CopyPointer++;
                    }
                    if (EndOfBuffer != Buffer) {
                        EndOfBuffer--;
                    }
                    continue;

                //
                // Some other CSI key.  Ignore it.
                //

                default:
                    break;

            }

            break;

        case '\r':
        case '\n':

            GetLineStatus = GetLineSuccess;
            goto ExitRoutine;

        //
        // Bug: Tab is not handled properly.
        //

        case '\t' :

            GetLineStatus = GetLineSuccess;
            goto ExitRoutine;


        //
        // Backspace
        //

        case '\b':

            if (Cursor != Buffer) {
                Cursor--;
            }

            CopyPointer = Cursor;
            while (*CopyPointer) {
                *CopyPointer = *(CopyPointer + 1);
                CopyPointer++;
            }
            if (EndOfBuffer != Buffer) {
                EndOfBuffer--;
            }
            break;

        //
        // A printing character.  Store it in the buffer.
        //

        default:

            CopyPointer = ++EndOfBuffer;
            if (CopyPointer > Cursor) {
                while (CopyPointer != Cursor) {
                    *CopyPointer = *(CopyPointer - 1);
                    CopyPointer--;
                }
            }
            *Cursor++ = Character;

            break;
        }

    }	// while...


ExitRoutine:

    VenSetPosition(ScreenRow, (Cursor - Buffer) + PromptLength + ScreenColumn);

    if (Cursor >= Buffer) {
        Print(" ");
    } else {
        Write(StandardOut, Cursor, 1, &Count);
    }

    Print("\r\n");

    return (GetLineStatus);
}


ARCDOS_STATUS
ParseCommandLine (
    IN PCHAR Buffer,
    OUT PULONG Argc
    )
/*++

Routine Description:

    This routine does a simple parse of the command line.

    The first token is returned in the Argv[0] position.

    The second and third tokens, if they exist are returned in the
    Argv[1] and Argv[2] positions, with the following mutually
    exclusive expansions applied:

            Expand environment variables at the beginning of the token,
            of the form "<name>:".

            If the token does not begin with an ARC pathname,
            apply the default device name.

    The results are stored in the global variable Argv.

Arguments:

    Buffer              A pointer to the buffer containing the raw command
                        line, terminated with a NULL.

    Argc                A pointer to a variable that will be set to the
                        number of tokens in Argv for this command line.

Return Value:

    A code reflecting success or failure.

--*/
{
    ULONG CurrentTokenPosition;
    ULONG BufferIndex;
    ULONG Index;
    BOOLEAN ExpandToken;
    ULONG InputTokenBegin;
    ULONG InputTokenEnd;
    PCHAR NextColon;
    PCHAR NextLeftParens;
    PCHAR NextDotDot;
    PCHAR EnvironmentVariableValue;
    BOOLEAN EnvironmentVariablePresent;
    BOOLEAN ARCDevicePathPresent;
    BOOLEAN DotDotPresent;
    UCHAR TempBuffer[MAXIMUM_USER_COMMAND_LINE];
    PCHAR LastSlashLocation;
    PCHAR NextSlashLocation;

    //
    // Do not expand the first command token
    //

    ExpandToken = FALSE;

    //
    // Start at the beginning of the line.
    //

    BufferIndex = 0;

    for (*Argc = 0; *Argc < MAXIMUM_TOKENS; (*Argc)++) {

        Argv[*Argc][0] = 0;

        //
        // Advance to the beginning of the first/next token
        //

        while ((Buffer[BufferIndex] != 0) &&
               (Buffer[BufferIndex] == ' ')) {
            BufferIndex++;
        }

        if (Buffer[BufferIndex] == 0) {

            //
            // We have reached the end of the command line.
            //

            return (ADSUCCESS);
        }

        //
        // Find the beginning and end of the input token.
        //

        InputTokenBegin = BufferIndex;
        InputTokenEnd = BufferIndex + 1;

        while ((Buffer[InputTokenEnd] != 0) &&
               (Buffer[InputTokenEnd] != ' ')) {
            InputTokenEnd++;
        }

        //
        // We have identified a token in the input stream.
        //
        // The first character is pointed to by InputTokenBegin.
        //
        // The first position *after* the last character is pointed
        // to by InputTokenEnd.
        //

        //
        // If this token should not be expanded, do a simple copy.
        //

        if (ExpandToken == FALSE) {

            //
            // Expand all tokens after this one.
            //

            ExpandToken = TRUE;

            for (Index = 0; Index < MAXIMUM_TOKEN_LENGTH; Index++) {

                *(Argv[*Argc]+Index) = Buffer[InputTokenBegin++];

                if (InputTokenBegin == InputTokenEnd) {
                    break;
                }
            }

            if (Index == MAXIMUM_TOKEN_LENGTH) {
                // Token too large, error return.
                return (ADBIGTOKEN);
            } else {
                // Terminate token.
                *(Argv[*Argc]+Index+1) = 0;
            }

        } else {

            //
            // Apply any appropriate expansions to this token.
            //

            NextColon =      strchr( (Buffer + InputTokenBegin), ':' );
            NextLeftParens = strchr( (Buffer + InputTokenBegin), '(' );
            NextDotDot =     strstr( (Buffer + InputTokenBegin), ".." );

            CurrentTokenPosition = 0;

            if ((NextColon == 0) || (NextColon >= (Buffer+InputTokenEnd))) {
                EnvironmentVariablePresent = FALSE;
            } else {
                EnvironmentVariablePresent = TRUE;
            }

            if ((NextLeftParens == 0) ||
                (NextLeftParens >= (Buffer+InputTokenEnd))) {
                ARCDevicePathPresent = FALSE;
            } else {
                ARCDevicePathPresent = TRUE;
            }

            if ((NextDotDot == 0) ||
                (NextDotDot >= (Buffer+InputTokenEnd))) {
                DotDotPresent = FALSE;
            } else {
                DotDotPresent = TRUE;
            }

            //
            // The boolean flags have been set for the current token.
            // These must be mutually exclusive, so check for any two
            // that are set.
            //

            if ((EnvironmentVariablePresent && ARCDevicePathPresent) ||
                (EnvironmentVariablePresent && DotDotPresent) ||
                (ARCDevicePathPresent && DotDotPresent)) {
                return (ADBADFILESPEC);
            }

            if (DotDotPresent) {

                //
                // This token is ..
                //

                //
                // There must be at least one slash in the default path.
                //

                LastSlashLocation = NULL;
                NextSlashLocation = strchr(DefaultARCPath, '\\');

                while (NextSlashLocation != NULL) {
                    LastSlashLocation = NextSlashLocation;
                    NextSlashLocation = strchr(NextSlashLocation+1, '\\');
                }

                if (LastSlashLocation == NULL) {

                    //
                    // There is no slash in the current default.  Error.
                    //

                    return (ADBADFILESPEC);

                } else {

                    //
                    // Copy everything in the current default up to but not
                    // including the last slash.
                    //

                    strncpy(Argv[*Argc],
                            DefaultARCPath,
                            (LastSlashLocation - (PCHAR)DefaultARCPath));

                    //
                    // Now put a NULL at the end of whatever we copied.
                    //

                    *(Argv[*Argc]+(LastSlashLocation - (PCHAR)DefaultARCPath)) = 0;

                    InputTokenBegin = InputTokenEnd;

                }

            } else if (EnvironmentVariablePresent) {

                //
                // The environment variable is from the start of the token
                // up to and including the colon.
                //

                for (Index = 0; Index < MAXIMUM_USER_COMMAND_LINE; Index++) {
                    TempBuffer[Index] = Buffer[InputTokenBegin++];
                    if (TempBuffer[Index] == ':') {
                        break;
                    }
                }

                if (Index < MAXIMUM_USER_COMMAND_LINE) {
                    TempBuffer[Index + 1] = 0;
                } else {
                    TempBuffer[MAXIMUM_USER_COMMAND_LINE - 1] = 0;
                }

                EnvironmentVariableValue = GetEnvironmentVariable(TempBuffer);

                if ((EnvironmentVariableValue == 0) ||
                    (strlen(EnvironmentVariableValue) >= MAXIMUM_TOKEN_LENGTH)) {

                    //
                    // The environment value does not exist or is too long.
                    //

                    return (ADUNDEFEV);
                }

                strcpy(Argv[*Argc],  EnvironmentVariableValue);
                CurrentTokenPosition = strlen(EnvironmentVariableValue);

            } else if (!ARCDevicePathPresent) {

                //
                // Apply the default string to the beginning of this token.
                // To keep the semantics simple, a \ is appended to the end
                // of the default ARC path.
                //

                if (DefaultARCPath[0] == 0) {
                    // There is no default yet.
                    return (ADNODEFAULT);
                }

                strcpy (Argv[*Argc], DefaultARCPath);
                CurrentTokenPosition = strlen(DefaultARCPath);
                *(Argv[*Argc] + CurrentTokenPosition) = '\\';
                CurrentTokenPosition++;
            }

            //
            // All expansions have been applied.  Now append the remainder
            // of the input token to the partially built output token.
            //
            // InputTokenBegin		This character up to but not including
            //				the [InputTokenEnd] character will be
            // 				copied to the output token.
            //
            // InputTokenEnd		The first input position *after* the
            //				last character of the token.
            //
            // Argv[*Argc]		The work-in-progress output token,
            //				terminated with a NULL.
            //
            // CurrentTokenPosition	The first position in the output
            //				token to be filled.  This points at
            //				the NULL.
            //

            if (InputTokenBegin < InputTokenEnd) {

                for (Index = CurrentTokenPosition;
                     Index < MAXIMUM_TOKEN_LENGTH;
                     Index++) {

                    *(Argv[*Argc]+Index) = Buffer[InputTokenBegin++];
                    if (InputTokenBegin == InputTokenEnd) {
                        break;
                    }
                }

                if (Index == MAXIMUM_TOKEN_LENGTH) {
                    // Token too large, error return.
                    return (ADBIGTOKEN);
                } else {
                    // Terminate token.
                    *(Argv[*Argc]+Index+1) = 0;
                }
            }
        }


        //
        // Advance past the end of this token.
        //

        BufferIndex = InputTokenEnd;

    }

    //
    // We have three tokens.  If there are more non-whitespace
    // characters on the line, we have an error condition.
    //

    while ((Buffer[BufferIndex] != 0) &&
          (Buffer[BufferIndex] == ' ')) {
        BufferIndex++;
    }

    if (Buffer[BufferIndex] != 0) {
        return (ADINCORRECTARGS);
    } else {
        return (ADSUCCESS);
    }

}



ARCDOS_STATUS
DoCommand(
    IN ULONG Argc,
    IN PCHAR Argv[]
    )
/*++

Routine Description:

    This calls the appropriate routines for the user command.

Arguments:

    Argc                The number of command line arguments.

    Argv                An array of pointers to command line tokens.

Return Value:

    A code reflecting success or failure.

--*/
{
    ULONG Index;

    if (Argc == 0) {
        return (ADSUCCESS);
    }

    for (Index = 0; Index < COMMAND_TABLE_LENGTH; Index++) {
        if (strcmp(CommandTable[Index].Verb, Argv[0]) == 0) {
            break;
        }
    }

    if (Index == COMMAND_TABLE_LENGTH) {
        return (ADUNDEFCOMMAND);
    }

    return ((*CommandTable[Index].Function)(Argc, Argv));
}


VOID
ARCDosErrorMessage(
    IN ARCDOS_STATUS Status
    )
/*++

Routine Description:

    This prints error messages to the console output device.

Arguments:

    Status              The ARCDos status to be translated.

Return Value:

    None.

--*/
{
    switch (Status) {

      //
      // Success
      //

      case ADSUCCESS:
        return;

      //
      // Illegal filename
      //

      case ADILLEGALFILENAME:
        Print("? Illegal filename.\r\n");
        return;

      //
      // Undefined environment variable
      //

      case ADUNDEFEV:
        Print("? Undefined environment variable.\r\n");
        return;

      //
      // Undefined command
      //

      case ADUNDEFCOMMAND:
        Print("? Undefined command.\r\n");
        return;

      //
      // No default location set yet
      //

      case ADNODEFAULT:
        Print("? Use cd to set a default ARC devicepath.\r\n");
        return;

      //
      // Token too large
      //

      case ADBIGTOKEN:
        Print("? Command or filespec is too long.\r\n");
        return;

      //
      // Please exit ARCDos.
      //

      case ADEXITARCDOS:
        return;

      //
      // General error
      //

      case ADERROR:
        Print("? Error.\r\n");
        return;

      //
      // Bad filespec
      //

      case ADBADFILESPEC:
        Print("? Bad filespec.\r\n");
        return;

      //
      // Incorrect number of command arguments.
      //

      case ADINCORRECTARGS:
        Print("? Incorrect number of command arguments.\r\n");
        return;

      //
      // File I/O failed.
      //

      case ADFILEIO:
        Print("? File I/O failed.\r\n");
        return;

      //
      // An illegal status value.
      //

      default:
        Print("??? Internal ARCDos error, bad status = 0x%x\r\n", Status);
        return;
    }
}

 
int
ArcDosMain (void)


/*++

Routine Description:

    This is the top-level routine that gets control when this
    program is invoked.

Arguments:

    None.

Return Value:

    None.

--*/

{
    GETLINE_STATUS GetLineStatus;
    ARCDOS_STATUS  ARCDosStatus;
    CHAR CommandBuffer[NUMBER_COMMAND_LINES][MAXIMUM_USER_COMMAND_LINE];
    LONG CommandNumber;
    ULONG Argc;
    ULONG Index;
    BOOLEAN UseExistingCommand;

   // FwRtlStackPanic = 0;
    CommandNumber = 0;
    // ARM port: default to the loader's one FAT partition so file commands (dir/type)
    // resolve immediately. The original starts empty (DefaultARCPath[0] = 0), which on
    // a real ARC machine requires an explicit `cd device(...)` first; cd still works to
    // change it.
    strcpy(DefaultARCPath, "multi(0)disk(0)rdisk(0)partition(1)");
    UseExistingCommand = TRUE;

    //
    // ARM port: terminate the first recall buffer. The original relies on the
    // stack happening to hold a NUL here (UseExistingCommand starts TRUE, so the
    // first GetCommandLine does strchr(CommandBuffer[0], 0)); on bare metal the
    // C stack is not zeroed, so make it explicit.
    //
    CommandBuffer[0][0] = 0;

    //
    // Initialize Argv.
    //

    for (Index = 0; Index < MAXIMUM_TOKENS; Index++) {
        Argv[Index][0] = 0;
    }

    printf("ArcDos version 1.0\r\n\n");      
    Print ("Copyright(c) 1993  Digital Equipment Corporation\r\n");
    Print ("Type help or ? for help.\r\n");

    #if DBG
       DbgPrint("start debug for the arcdos\r\n");
       DbgBreakPoint();
    #endif //DBG
   
    while (TRUE) {

        //
        // Read a command into the next entry in the circular command buffer.
        //

        if (UseExistingCommand == FALSE) {
            CommandNumber++;
            if (CommandNumber == NUMBER_COMMAND_LINES) {
                CommandNumber = 0;
            }
        }

        GetLineStatus = GetCommandLine("> ",
                                    UseExistingCommand,
                                    MAXIMUM_USER_COMMAND_LINE,
                                    CommandBuffer[CommandNumber]);
        UseExistingCommand = FALSE;

        switch (GetLineStatus) {

            //
            // Successful translation.
            //

            case GetLineSuccess:
                break;

            //
            // Up arrow.
            //

            case GetLineUpArrow:

                --CommandNumber;

                if (CommandNumber < 0) {
                    CommandNumber = NUMBER_COMMAND_LINES - 1;
                }

                UseExistingCommand = TRUE;

                continue;

            //
            // Down arrow.
            //

            case GetLineDownArrow:

                CommandNumber++;

                if (CommandNumber == NUMBER_COMMAND_LINES) {
                    CommandNumber = 0;
                }

                UseExistingCommand = TRUE;

                continue;

            //
            // Error on input device.
            //

            case GetLineError:
                Print ("?\r\n\n Error on input device.  Exiting ARCDos.\r\n");
                StallExecution(4 * 1000 * 1000);
                return 0;

            //
            // An error code was returned that is not a legal status code.
            //

            default:
                Print("?\r\n\n Bad error code returned from GetCommandLine: 0x%x\r\n",
                            GetLineStatus);
                Print("Exiting ARCDos.\r\n");
                StallExecution(4 * 1000 * 1000);
                return -1;
        }

        //
        // Parse the command line.
        //

        ARCDosStatus = ParseCommandLine(CommandBuffer[CommandNumber], &Argc);

        if (ARCDosStatus != ADSUCCESS) {
            ARCDosErrorMessage(ARCDosStatus);
            continue;
        }

        //
        // Execute the command line.
        //

        ARCDosStatus = DoCommand(Argc, Argv);

        if (ARCDosStatus != ADSUCCESS) {

            ARCDosErrorMessage(ARCDosStatus);

            if (ARCDosStatus == ADEXITARCDOS) {
                return 0;
            } else {
                continue;
            }
        }
    }
	return 0;
}




































