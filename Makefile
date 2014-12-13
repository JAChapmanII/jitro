SRC=src
LIB=lib
OBJ=obj
BIN=.

BINS=${BIN}/jitro

OBJS=
OBJS+=${OBJ}/util.o ${OBJ}/config.o ${OBJ}/ircsock.o
OBJS+=${OBJ}/bufreader.o ${OBJ}/subprocess.o

CXXFLAGS=-std=c++0x -I${LIB} -D_DEFAULT_SOURCE
LDFLAGS=-lpthread

# release/NA flags
ifndef release
CXXFLAGS+=-g
else
CXXFLAGS+=-O3 -Os
endif

# nowall NA flags
ifndef nowall
CXXFLAGS+=-Wall -Wextra -pedantic -Wmain -Weffc++ -Wswitch-default -Wswitch-enum
CXXFLAGS+=-Wmissing-include-dirs -Wmissing-declarations -Wunreachable-code
CXXFLAGS+=-Winline -Wfloat-equal -Wundef -Wcast-align -Wredundant-decls
CXXFLAGS+=-Winit-self -Wshadow
endif

# profile/NA flags
ifdef profile
CXXFLAGS+=-pg
LDFLAGS+=-pg
endif

all: dir ${BINS}
dir:
	mkdir -p ${SRC} ${LIB} ${OBJ} ${BIN}

# main project binary rules
${BIN}/jitro: ${OBJ}/jitro.o ${OBJS}
	${CXX}    -o $@ $^ ${LDFLAGS}

# standard directory object rules
${OBJ}/%.o: ${SRC}/%.cpp
	${CXX} -c -o $@ $^ ${CXXFLAGS}
${OBJ}/%.o: ${LIB}/%.cpp
	${CXX} -c -o $@ $^ ${CXXFLAGS}

clean:
	rm -rf ${OBJ}/*.o ${BINS}

