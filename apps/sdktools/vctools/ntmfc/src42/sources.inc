# The MFC 4.2 DLLs, MFC42.DLL and MFC42U.DLL (UNICODE_MFC), as mfcdll.mak makes the retail ones
# (LIBNAME=MFC42 RELEASE=1, MONOLITHIC): the core, OLE, database and Internet classes in one DLL,
# which loads the DLLs of OLE and the common dialogs itself. Its exports are those of the Intel
# machine's .def, by the names of the RISC machines (makefile.inc).

!ifndef UNICODE_MFC
UNICODE_MFC=0
!endif

!if $(UNICODE_MFC)
TARGETNAME=mfc42u
DLLBASE=0x75270000
!else
TARGETNAME=mfc42
DLLBASE=0x754F0000
!endif
TARGETPATH=obj
TARGETPATHLIB=$(BASEDIR)\public\sdk\lib
TARGETTYPE=DYNLINK
UMTYPE=windows

USE_MSVCRT=1
USE_NATIVE_EH=1
NO_NTDLL=1
NOT_LEAN_AND_MEAN=1
COMPILER_WARNINGS=

DLLENTRY=_DllMainCRTStartup
DLLDEF=obj\$(TARGET_DIRECTORY)\$(TARGETNAME).def
NTTARGETFILE0=$(DLLDEF)

INCLUDES=$(BASEDIR)\public\sdk\inc\mfc42;..

# The defines of the compiles; the resources, mfcdll.rc, get _UNICODE, _AFX_MONOLITHIC and RELEASE
# alone, as mfcdll.mak compiles them (with _AFXDLL, afxres.rc has none of the strings and dialogs of
# MFC). (_OLEAUT32_: the functions of OLEPRO32 are not imported, as the olectl.h of Visual C++ 4.2,
# with which MFC was built, declares them; the versions of MFC, olepro32.cpp, are then no exports)
USER_C_FLAGS=-D_AFXDLL -D_WINDLL -D_AFX_CORE_IMPL -D_AFX_OLE_IMPL -D_AFX_DB_IMPL -D_AFX_NET_IMPL -D_OLEAUT32_
!if $(UNICODE_MFC)
C_DEFINES=-D_UNICODE
!else
USER_C_FLAGS=$(USER_C_FLAGS) -D_MBCS
!endif
RCOPTIONS=-D_AFX_MONOLITHIC -DRELEASE

TARGETLIBS=$(BASEDIR)\public\sdk\lib\*\kernel32.lib \
           $(BASEDIR)\public\sdk\lib\*\gdi32.lib    \
           $(BASEDIR)\public\sdk\lib\*\user32.lib   \
           $(BASEDIR)\public\sdk\lib\*\uuid.lib     \
           $(BASEDIR)\public\sdk\lib\*\uuid2.lib

SOURCES=..\mfcdll.rc   \
        ..\objcore.cpp  \
        ..\except.cpp   \
        ..\validadd.cpp \
        ..\dumpcont.cpp \
        ..\dumpflt.cpp  \
        ..\arccore.cpp  \
        ..\arcobj.cpp   \
        ..\arcex.cpp    \
        ..\arcstrm.cpp  \
        ..\dumpinit.cpp \
        ..\dumpout.cpp  \
        ..\afxasert.cpp \
        ..\afxmem.cpp   \
        ..\afxabort.cpp \
        ..\filecore.cpp \
        ..\filetxt.cpp  \
        ..\filemem.cpp  \
        ..\fileshrd.cpp \
        ..\filex.cpp    \
        ..\filest.cpp   \
        ..\array_b.cpp  \
        ..\array_d.cpp  \
        ..\array_p.cpp  \
        ..\array_o.cpp  \
        ..\array_s.cpp  \
        ..\array_u.cpp  \
        ..\array_w.cpp  \
        ..\list_o.cpp   \
        ..\list_p.cpp   \
        ..\list_s.cpp   \
        ..\map_pp.cpp   \
        ..\map_pw.cpp   \
        ..\map_so.cpp   \
        ..\map_sp.cpp   \
        ..\map_ss.cpp   \
        ..\map_wo.cpp   \
        ..\map_wp.cpp   \
        ..\plex.cpp     \
        ..\strcore.cpp  \
        ..\strex.cpp    \
        ..\timecore.cpp \
        ..\afxdbcs.cpp  \
        ..\afxstate.cpp \
        ..\afxtls.cpp   \
        ..\mtcore.cpp   \
        ..\mtex.cpp     \
        ..\wincore.cpp  \
        ..\winfrm.cpp   \
        ..\winfrm2.cpp  \
        ..\winfrmx.cpp  \
        ..\winmdi.cpp   \
        ..\tooltip.cpp  \
        ..\winmini.cpp  \
        ..\winhand.cpp  \
        ..\winmain.cpp  \
        ..\barcore.cpp  \
        ..\bartool.cpp  \
        ..\bardlg.cpp   \
        ..\barstat.cpp  \
        ..\bardock.cpp  \
        ..\dockcont.cpp \
        ..\dockstat.cpp \
        ..\dcprev.cpp   \
        ..\dcmeta.cpp   \
        ..\trckrect.cpp \
        ..\winctrl1.cpp \
        ..\winctrl2.cpp \
        ..\winctrl3.cpp \
        ..\winctrl4.cpp \
        ..\winbtn.cpp   \
        ..\dlgcore.cpp  \
        ..\dlgdata.cpp  \
        ..\dlgfloat.cpp \
        ..\dlgprop.cpp  \
        ..\dlgcomm.cpp  \
        ..\dlgfile.cpp  \
        ..\dlgprnt.cpp  \
        ..\dlgclr.cpp   \
        ..\dlgfnt.cpp   \
        ..\dlgfr.cpp    \
        ..\ccdata.cpp   \
        ..\dlgtempl.cpp \
        ..\wingdi.cpp   \
        ..\wingdix.cpp  \
        ..\winstr.cpp   \
        ..\winmenu.cpp  \
        ..\auxdata.cpp  \
        ..\afxcrit.cpp  \
        ..\afxtrace.cpp \
        ..\winutil.cpp  \
        ..\winocc.cpp   \
        ..\cmdtarg.cpp  \
        ..\doccore.cpp  \
        ..\doctempl.cpp \
        ..\docsingl.cpp \
        ..\docmulti.cpp \
        ..\docmgr.cpp   \
        ..\viewcore.cpp \
        ..\viewprnt.cpp \
        ..\winsplit.cpp \
        ..\viewscrl.cpp \
        ..\viewform.cpp \
        ..\viewedit.cpp \
        ..\viewprev.cpp \
        ..\viewcmn.cpp  \
        ..\docmapi.cpp  \
        ..\thrdcore.cpp \
        ..\appcore.cpp  \
        ..\appinit.cpp  \
        ..\appterm.cpp  \
        ..\appui.cpp    \
        ..\appui2.cpp   \
        ..\appui3.cpp   \
        ..\appgray.cpp  \
        ..\appdlg.cpp   \
        ..\app3d.cpp    \
        ..\appprnt.cpp  \
        ..\apphelp.cpp  \
        ..\apphelpx.cpp \
        ..\filelist.cpp \
        ..\olelock.cpp  \
        ..\inet.cpp     \
        ..\filefind.cpp \
        ..\inetimpl.cpp \
        ..\sockcore.cpp \
        ..\oleinit.cpp  \
        ..\olecli1.cpp  \
        ..\olecli2.cpp  \
        ..\olecli3.cpp  \
        ..\olecnvrt.cpp \
        ..\oledobj1.cpp \
        ..\oledobj2.cpp \
        ..\oledisp1.cpp \
        ..\oledisp2.cpp \
        ..\oledlgs1.cpp \
        ..\oledlgs2.cpp \
        ..\oledlgs3.cpp \
        ..\oledata.cpp  \
        ..\oledoc1.cpp  \
        ..\oledoc2.cpp  \
        ..\oledrop1.cpp \
        ..\oledrop2.cpp \
        ..\olemsgf.cpp  \
        ..\oleenum.cpp  \
        ..\olefact.cpp  \
        ..\oleipfrm.cpp \
        ..\olelink.cpp  \
        ..\olemisc.cpp  \
        ..\olestrm.cpp  \
        ..\olesvr1.cpp  \
        ..\olesvr2.cpp  \
        ..\olereg.cpp   \
        ..\oletsvr.cpp  \
        ..\oleui1.cpp   \
        ..\oleui2.cpp   \
        ..\oleunk.cpp   \
        ..\oleverb.cpp  \
        ..\olecall.cpp  \
        ..\olevar.cpp   \
        ..\viewrich.cpp \
        ..\oledll.cpp   \
        ..\oletyplb.cpp \
        ..\olemon.cpp   \
        ..\occmgr.cpp   \
        ..\occevent.cpp \
        ..\occcont.cpp  \
        ..\occsite.cpp  \
        ..\occlock.cpp  \
        ..\occddx.cpp   \
        ..\occddxf.cpp  \
        ..\occdlg.cpp   \
        ..\oledocvw.cpp \
        ..\oledocob.cpp \
        ..\oledoctg.cpp \
        ..\oledocip.cpp \
        ..\oleasmon.cpp \
        ..\olebar.cpp   \
        ..\ctlcache.cpp \
        ..\ctlcore.cpp  \
        ..\ctlconn.cpp  \
        ..\ctldata.cpp  \
        ..\ctlevent.cpp \
        ..\ctlmodul.cpp \
        ..\ctlframe.cpp \
        ..\ctlfont.cpp  \
        ..\ctlinplc.cpp \
        ..\ctllic.cpp   \
        ..\oleconn.cpp  \
        ..\ctlobj.cpp   \
        ..\ctlpict.cpp  \
        ..\ctlpropx.cpp \
        ..\ctlppg.cpp   \
        ..\ctlprop.cpp  \
        ..\ctlpset.cpp  \
        ..\ctlpstg.cpp  \
        ..\ctlpstm.cpp  \
        ..\ctlrefl.cpp  \
        ..\ctlreg.cpp   \
        ..\ctltrack.cpp \
        ..\ctlview.cpp  \
        ..\olepset.cpp  \
        ..\ctlpbag.cpp  \
        ..\olepro32.cpp \
        ..\ctlquick.cpp \
        ..\ctlnownd.cpp \
        ..\ppgcolor.cpp \
        ..\ppgfont.cpp  \
        ..\ppgpict.cpp  \
        ..\ppgstock.cpp \
        ..\dbcore.cpp   \
        ..\dbrfx.cpp    \
        ..\dbview.cpp   \
        ..\dbflt.cpp    \
        ..\dblong.cpp   \
        ..\dbvar.cpp    \
        ..\daocore.cpp  \
        ..\daodfx.cpp   \
        ..\daoview.cpp  \
        ..\dllinit.cpp
