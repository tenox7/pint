# config\utils predates the Configuration Manager it is built on: 3.5 added the
# MarkAsSystemHive argument of CmpOpenHiveFiles ahead of ClusterSize; 4.0 dropped the
# signature of the key control block and keeps in it the key node of its cell,
# CmSetValueKey takes the value name by reference, without a title index, a subkey is
# found by CmpFindSubKeyByName and CmpFindSubKeyByNumber (CmpFindChildByName and
# ByNumber are gone), a key node is sized by its name, and the registry lock is taken
# exclusive by CmpLockRegistryExclusive (a user mode lock as the others in CMWRAPER).
undef $/;
$_ = <STDIN>;
s/(CmpOpenHiveFiles\s*\([^;]*?\bTRUE,)(\s*)(NULL\s*\))/$1$2FALSE,$2$3/gs;
s/(CmSetValueKey\s*\(\s*&kcb,\s*)\*ValueName,(\s*)TitleIndex,\s*/$1ValueName,$2/gs;
s/CmpFindChildByName\(\s*([^,]+?),\s*\(HCELL_INDEX\)CellHandle,\s*\*Name,\s*KeyBodyNode,\s*\(PHCELL_INDEX\)ChildCell,\s*&Index\)/((*(PHCELL_INDEX)ChildCell = CmpFindSubKeyByName($1, (PCM_KEY_NODE)HvGetCell($1, (HCELL_INDEX)CellHandle), Name)) != HCELL_NIL ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND)/gs;
s/CmpFindChildByNumber\(\s*([^,]+?),\s*\(HCELL_INDEX\)CellHandle,\s*Index,\s*Type,\s*\(PHCELL_INDEX\)ChildCell\)/((*(PHCELL_INDEX)ChildCell = CmpFindSubKeyByNumber($1, (PCM_KEY_NODE)HvGetCell($1, (HCELL_INDEX)CellHandle), Index)) != HCELL_NIL ? STATUS_SUCCESS : STATUS_NO_MORE_ENTRIES)/gs;
s/CmpHKeyNodeSize\(Hive,\s*Name->Length\)/CmpHKeyNodeSize(Hive, Name)/g;
s/(\nCmpLockRegistry\(VOID\)\n\{\n\s*return;\n\}\n)/$1\nVOID\nCmpLockRegistryExclusive(VOID)\n{\n    return;\n}\n/;
s/[ \t]*\w+\.Signature\s*=\s*CM_KEY_CONTROL_BLOCK_SIGNATURE;[ \t]*\n//g;
s/^([ \t]*)(\w+)(\.KeyCell\s*=\s*\(HCELL_INDEX\)\w+;)/$1$2$3\n$1$2.KeyNode = (PCM_KEY_NODE)HvGetCell($2.KeyHive, $2.KeyCell);/gm;
print;
