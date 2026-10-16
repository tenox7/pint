# CONFIG\UTILS predates the NT 3.5 Configuration Manager it is built on: 3.5 added the
# MarkAsSystemHive argument of CmpOpenHiveFiles ahead of ClusterSize.
undef $/;
$_ = <STDIN>;
s/(CmpOpenHiveFiles\s*\([^;]*?\bTRUE,)(\s*)(NULL\s*\))/$1$2FALSE,$2$3/gs;
print;
