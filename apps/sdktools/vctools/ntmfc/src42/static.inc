# MFCS42.LIB and MFCS42U.LIB (UNICODE_MFC), the part of MFC 4.2 an application or DLL of the MFC
# DLL links (mfcdll.mak, TARG_STATIC): its entry points and the symbols that pull in the rest.

!ifndef UNICODE_MFC
UNICODE_MFC=0
!endif

!if $(UNICODE_MFC)
TARGETNAME=mfcs42u
!else
TARGETNAME=mfcs42
!endif
TARGETPATH=$(BASEDIR)\public\sdk\lib
TARGETTYPE=LIBRARY

USE_MSVCRT=1
USE_NATIVE_EH=1
NOT_LEAN_AND_MEAN=1
COMPILER_WARNINGS=

INCLUDES=$(BASEDIR)\public\sdk\inc\mfc42;..

C_DEFINES=-D_AFXDLL -D_WINDLL -D_AFX_CORE_IMPL -D_AFX_OLE_IMPL -D_AFX_DB_IMPL -D_AFX_NET_IMPL
!if $(UNICODE_MFC)
C_DEFINES=$(C_DEFINES) -D_UNICODE
!else
C_DEFINES=$(C_DEFINES) -D_MBCS
!endif

SOURCES=..\stdafx.cpp   \
        ..\nolib.cpp    \
        ..\appmodul.cpp \
        ..\dllmodul.cpp \
        ..\oleexp.cpp
