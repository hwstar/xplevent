# Makefile

PACKAGE = xplevent
VERSION = 0.0.1
CONTACT = <hwstar@rodgers.sdcoxmail.com>

CC = gcc
LIBS = -lm -lxPL -lsqlite3
#CFLAGS = -O2 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'
CFLAGS = -g3 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'

# Install paths for built executables

DAEMONDIR = /usr/local/bin

#.PHONY Targets

.PHONY: all, clean, install, dist

# Object file lists

OBJS = $(PACKAGE).o notify.o confread.o  

#Dependencies

all: $(PACKAGE) 

$(PACKAGE).o: Makefile $(PACKAGE).c notify.h confread.h types.h

#Rules

$(PACKAGE): $(OBJS)
	$(CC) $(CFLAGS) -o $(PACKAGE) $(OBJS) $(LIBS)

clean:
	-rm -f $(PACKAGE) *.o core

install:
	cp $(PACKAGE) $(DAEMONDIR)

dist:
	(cd ..; tar cvzf $(PACKAGE).tar.gz $(PACKAGE) --exclude *.o --exclude $(PACKAGE)/$(PACKAGE) --exclude .git --exclude .*.swp)

