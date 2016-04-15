/* iotest.h 
 *
 * Copyright (C) 2000- GODA Kazuo (The University of Tokyo)
 *
 * $Id: iotest.h,v 1.3 2007/09/27 05:37:01 kgoda Exp $
 *
 * Time-stamp: <2007-09-29 17:03:03 kgoda>
 */

#ifdef __linux__
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <pthread.h>

#include <errno.h>

#ifdef __sun__
#ifdef SUNOS5
#include <sys/sysmacros.h>
#include <sys/dkio.h>
#endif
#ifdef SUNOS4
#include <sun/dkio.h>
#endif
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define _KERNEL
#include <sys/disklabel.h>
#undef _KERNEL
#endif

#ifdef __linux__
#include <linux/fs.h>
#include <sys/time.h>
#include <time.h>
#endif

#ifdef __linux__
#include <libaio.h>
#endif

#define VERSION "1.20"
#define NAME    "iotest"
#define AUTHOR  "GODA Kazuo"
#define CONTACT "<kgoda@tkl.iis.u-tokyo.ac.jp>"

/* iotest.h */


