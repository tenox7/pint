# The .def of the DLL: that of the Intel machine by the names of the RISC machines, without the
# functions of x86 and Alpha alone (GetLength64 of CFileFind and CGopherFileFind, afxinet.h).
# The functions of the RichEdit control take the structures of RichEdit 1.0 (CHARFORMATA,
# FINDTEXTEXA), which afxwin.h asks for (_RICHEDIT_VER 0x0100), where the Intel .def of MFC42U
# still has those of Unicode; their ordinals stay.
obj\$(TARGET_DIRECTORY)\$(TARGETNAME).def: ..\intel\$(TARGETNAME).def
    riscdef -x GetLength64 -r U_charformatw@@ U_charformat@@ -r U_findtextexw@@ U_findtextexa@@ ..\intel\$(TARGETNAME).def > $@
