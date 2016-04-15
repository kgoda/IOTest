/* checksize.c 
 *
 * Copyright (C) 2000- GODA Kazuo (The University of Tokyo)
 *
 * $Id: iotest.c,v 1.8 2007/09/27 10:04:59 kgoda Exp $
 *
 * Time-stamp: <2008-05-08 14:24:29 kgoda>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#define CHECK(var) \
  printf("  %18s: %8ld bytes long, %8s\n", \
    #var, \
    sizeof(var), \
    (var)-1 < (var)0 ? "signed" : "unsigned"\
  );

int main(int argc, char **argv)
{
    CHECK(char);
    CHECK(unsigned char);
    CHECK(short);
    CHECK(unsigned short);
    CHECK(int);
    CHECK(unsigned int);
    CHECK(long);
    CHECK(unsigned long);
    CHECK(long long);
    CHECK(unsigned long long);
    CHECK(char *);
    CHECK(float);
    CHECK(double);
    puts("");
    CHECK(int8_t);
    CHECK(uint8_t);
    CHECK(int16_t);
    CHECK(uint16_t);
    CHECK(int32_t);
    CHECK(uint32_t);
    CHECK(int64_t);
    CHECK(uint64_t);
    puts("");
#ifdef __size_t
    CHECK(size_t);
#endif
#ifdef __ssize_t_defined
    CHECK(ssize_t);
#endif
#ifdef __off_t_defined
    CHECK(off_t);
#endif
#ifdef __off64_t_defined
    CHECK(__off64_t);
#endif
    CHECK(wchar_t);

    return(0);
}

/* iotest.c */
