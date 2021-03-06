# Makefile

PACKAGE = xplevent
VERSION = 0.1.0
CONTACT = <hwstar@rodgers.sdcoxmail.com>

CC = gcc
LIBS = -lm -lsqlite3 -ltalloc

#DBGFLAGS ?= -O3 -Wall -pthread
DBGFLAGS ?= -g3 -Wall -pthread

CFLAGS = $(DBGFLAGS)  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'
LEX=flex
ACC=lemon

# Install paths for built executables

DAEMONDIR = /usr/local/bin

#.PHONY Targets

.PHONY: all, clean, install, dist

# Object file lists

OBJS = monitor.o xplrx.o xplcore.o notify.o confread.o parser.o lex.o grammar.o db.o poll.o util.o socket.o scheduler.o sunriset.o 

PACKAGE_OBJS = $(PACKAGE).o $(OBJS)


#Dependencies

all: $(PACKAGE)



$(PACKAGE).o: Makefile $(PACKAGE).c

# pull in dependency info for *existing* .o files
-include $(OBJS:.o=.d)

#Rules

%.o: %.c
	$(CC)  -c $(CFLAGS) $*.c -o $*.o
	@$(CC) -MM $(CFLAGS) $*.c > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' < $*.d.tmp > $*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

grammar.c grammar.h:	grammar.y
	$(ACC) grammar.y

lex.c:	lex.l lex.h grammar.c grammar.y 
	$(LEX) -o lex.c lex.l
	
parser.o:	parser.c grammar.c lex.c lex.l grammar.y 


$(PACKAGE): $(PACKAGE_OBJS)
	$(CC) $(CFLAGS) -o $(PACKAGE) $(PACKAGE_OBJS) $(LIBS)
	

clean:
	-rm -f $(PACKAGE)  *.o *.d lex.c grammar.c grammar.h grammar.out core

install:
	cp $(PACKAGE) $(DAEMONDIR)

dist:
	(cd ..; tar cvzf $(PACKAGE).tar.gz $(PACKAGE) --exclude *.o --exclude $(PACKAGE)/$(PACKAGE)\
	 --exclude .git --exclude .*.swp --exclude *.d --exclude *.conf --exclude *.sqlite3)

