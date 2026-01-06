#
#	FILE
#	    makefile - makefile del nucli del sistema.
#	PROJECT
#	    TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
#	DESCRIPTION
#	    Makefile per compilar el nucli del sistema.
#	AUTHOR
#	    Sergio Abate
#	OPERATING SYSTEM
#	    Linux

# variables
SRCS = $(wildcard *.cpp)
SRCS_JSON_CODEC = $(wildcard ../json_codec/*.c)
SRCS_LIB_WS = $(wildcard ../lib_ws/*.c)
OBJS = $(SRCS:.cpp=.o)
OBJS_JSON_CODEC = $(SRCS_JSON_CODEC:.c=.o)
OBJS_LIB_WS = $(SRCS_LIB_WS:.c=.o)
DEPS = $(SRCS:.cpp=.d)
DEPS_JSON_CODEC = $(SRCS_JSON_CODEC:.c=.d)
DEPS_LIB_WS = $(SRCS_LIB_WS:.c=.d)
NAMELIB = libjsoncodec.a
OBJS_APP = $(OBJS) $(OBJS_LIB_WS)

# compilador i linker
CC = gcc
CC_CPP = g++

# flag per al preprocessador de cc durant la creació dels fitxers objecte
CPPFLAGS = -I. -I../json_codec -I/usr/include/cjson -I/usr/local/include/wsserver -pthread -O2 -Wall -Wno-unused-function -MMD -MP
CPPFLAGS_JSON_CODEC = -I../lib -I/usr/include/cjson -g -pthread -O2 -Wall -Wno-unused-function -MMD -MP

# flag pel linker ld durant la creació del programa executable
LDFLAGS = ./libjsoncodec.a -lcjson -llist -lsqlite3 ../lib_ws/libws.a

# creació de l'executable
ocpp_cs: $(OBJS_APP)
	$(CC_CPP) $^ -o $@ $(LDFLAGS)

# creació de la llibreria
libjsoncodec: $(OBJS_JSON_CODEC)
	ar rcs $(NAMELIB) $(OBJS_JSON_CODEC)

# creació dels fitxers objecte
$(OBJS_APP): %.o: %.cpp
	$(CC_CPP) $(CPPFLAGS) -c $< -o $@

# creació dels fitxers objecte de la llibreria
$(OBJS_JSON_CODEC): %.o: %.c
	$(CC) $(CPPFLAGS_JSON_CODEC) -c $< -o $@

.PHONY: clean

clean:
	$(RM) $(OBJS) $(OBJS_JSON_CODEC) $(OBJS_LIB_WS) $(DEPS) $(DEPS_JSON_CODEC) $(DEPS_LIB_WS) ocpp_cs libjsoncodec.a

-include $(DEPS) $(DEPS_JSON_CODEC) $(DEPS_LIB_WS) $(DEPS_OCPP_REQUESTS)
