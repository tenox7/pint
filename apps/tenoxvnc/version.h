/*
 *  Copyright (C) 2026 TenoxVNC.  All Rights Reserved.
 */

/*
 * version.h - the release version, taken from the git tag.
 *
 * Stamped by "make version", which every build target runs first.  It is
 * checked in rather than generated because the build hosts that have no git
 * (OpenVMS, SunOS 4, HP-UX ...) build straight out of this tree over NFS and
 * have to compile whatever the tagging host last wrote here.
 *
 * The string is rewritten in place by sed, so keep it on one line.
 */

#ifndef TENOXVNC_VERSION
#define TENOXVNC_VERSION "1.2"
#endif
