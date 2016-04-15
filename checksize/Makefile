# Makefile - checksize
#
# Copyright (C) 2000- The University of Tokyo.
#
# $Id: Makefile,v 1.1.1.1 2006/05/26 11:25:53 kgoda Exp $
#
# Time-stamp: <2008-05-08 13:45:02 kgoda>

CC      = gcc
CFLAGS  = -Wall -O2
LDFLAGS = 

# CFLAGS += -g

CP = cp -p
RM = rm -f
MKDIR = mkdir -p
CHMOD = chmod

PREFIX = /usr/local

BINS = checksize
HDRS = 

#
# Do not change below.
#

export PREFIX

#
# top target
# 

all: build

build: build-top

install: install-top

clean: clean-top

#
# this directory
#

build-top: $(BINS)

install-top: $(BINS)
	@if [ ! -d $(PREFIX)/bin ]; then \
		$(MKDIR) $(PREFIX)/bin;  \
	fi
	$(CP) $(BINS) $(PREFIX)/bin
	@if [ ! -d $(PREFIX)/include ]; then \
		$(MKDIR) $(PREFIX)/include;  \
	fi
	$(CP) $(HDRS) $(PREFIX)/include

clean-top:
	-$(RM) *.o $(BINS)

#
# commands
#

checksize: checksize.o
checksize.o: checksize.c

# end of Makefile