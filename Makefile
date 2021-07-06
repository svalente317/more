CC	= gcc -Wall
CFLAGS	= -O
LDFLAGS	= 

PROG	= more
OBJS	= more.o morefile.o morehelp.o magic.o
LIBS	= -ltermcap

all: ${PROG}

${PROG}: ${OBJS}
	${CC} ${LDFLAGS} -o $@ ${OBJS} $(LIBS)

clean:
	rm -f ${PROG} *.o *~

install:
	install -c -s more /bin/
	install -c -m 644 more.1 /usr/share/man/man1/

more.o: more.c morefile.h
