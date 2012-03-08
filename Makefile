UNAME:=$(shell uname)
ifeq ($(UNAME), CYGWIN_NT-6.1-WOW64)
PROG=g.exe
LIBS=-L. -lSDLmain -lSDL -lglu32 -lglut32 -lopengl32
else
PROG=g
LIBS=-L. -lSDLmain -lSDL -lGLU -lglut
INCLUDE=-I/usr/include/SDL
endif

SRC=g.c

# Note: The following was used to make it run on vanilla (except for maybe gcc) Ubuntu
#  apt-get install mesa-common-dev
#  apt-get install freeglut3-dev
#  apt-get install libsdl1.2-dev
#  apt-get install ctags

all:
	gcc -Wall -O2 -o ${PROG} ${SRC} ${LIBS} ${INCLUDE}

run: all
	./${PROG} -fs

as:
	gcc -S -O1 -I /usr/include/SDL ${SRC}

commit: clean
	hg commit

push: clean
	hg push

clean:
	rm -f ${PROG} ${PROG}.stackdump

debug: clean
	gcc -g -Wall -o ${PROG} ${SRC} ${LIBS} ${INCLUDE}
	gdb ./${PROG}
#ccdebug ./${PROG}

prof: clean
	gcc -pg -Wall -o ${PROG} ${SRC} ${LIBS} ${INCLUDE}
	./${PROG}
	gprof ./${PROG}
#ccdebug ./${PROG}

tags:
	ctags ${SRC}
