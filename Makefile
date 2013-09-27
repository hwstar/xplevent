# Makefile

PACKAGE = xplevent
VERSION = 0.0.1
CONTACT = <hwstar@rodgers.sdcoxmail.com>

CC = gcc
LIBS = -lm -lxPL -lsqlite3 -ltalloc
#CFLAGS = -O2 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'
CFLAGS = -g3 -Wall  -D'PACKAGE="$(PACKAGE)"' -D'VERSION="$(VERSION)"' -D'EMAIL="$(CONTACT)"'
LEX=flex
ACC=lemon

# Install paths for built executables

DAEMONDIR = /usr/local/bin

#.PHONY Targets

.PHONY: all, clean, install, dist

# Object file lists

OBJS = notify.o confread.o parser.o lex.o grammar.o db.o monitor.o util.o socket.o scheduler.o sunriset.o

PACKAGE_OBJS = $(PACKAGE).o $(OBJS)


#Dependencies

all: $(PACKAGE)



$(PACKAGE).o: Makefile $(PACKAGE).c


#Rules


grammar.c grammar.h:	grammar.y
	$(ACC) grammar.y

lex.c:	lex.l lex.h grammar.c grammar.y 
	$(LEX) -o lex.c lex.l
	
parser.o:	parser.c grammar.c lex.c lex.l grammar.y 


$(PACKAGE): $(PACKAGE_OBJS)
	$(CC) $(CFLAGS) -o $(PACKAGE) $(PACKAGE_OBJS) $(LIBS)
	

clean:
	-rm -f $(PACKAGE)  *.o lex.c grammar.c grammar.h grammar.out core

install:
	cp $(PACKAGE) $(DAEMONDIR)

dist:
	(cd ..; tar cvzf $(PACKAGE).tar.gz $(PACKAGE) --exclude *.o --exclude $(PACKAGE)/$(PACKAGE) --exclude .git --exclude .*.swp)

