OS = $(shell uname)
MAKEFILE = gnu

ifeq ($(OS), Darwin)
    MAKEFILE = osx
endif
ifeq ($(OS), Cygwin)
    MAKEFILE = cygwin
endif
ifeq ($(OS), Solaris)
    MAKEFILE = solaris
endif
ifeq ($(OS), windows32)
    MAKEFILE = mingw
endif
ifeq ($(OS), MINGW32_NT-5.1)
	MAKEFILE = mingw
endif

OBJDIR = ./o
SRCDIR = ./src

EXE = irc

ifeq ($(HOSTTYPE), x86_64)
LIBSELECT=64
endif

CFLAGS= -std=c++0x -pipe -Wall
LFLAGS= -lboost_date_time -lboost_system -lboost_thread

ifeq ($(MAKEFILE), mingw)
	CFLAGS += -D_WIN32_WINNT=0x0501 -DBOOST_THREAD_USE_LIB
	LFLAGS += pthreadGCE2.dll -lws2_32 -lmswsock
	OBJDIR := $(OBJDIR)/mingw
else
	OBJDIR := $(OBJDIR)/gnu
endif


ifeq ($(build_type), debug)
	OBJDIR := $(OBJDIR)/debug
	CFLAGS += -g
	LFLAGS += -g
else
	OBJDIR := $(OBJDIR)/release
	CFLAGS += -D NDEBUG -Os #-fno-rtti
	LFLAGS += -s #-fno-rtti
endif

CC = g++
DO_CC = $(CC) -MD $(CFLAGS) -o $@ -c $<

# Automatic dependency
DEPS := $(wildcard $(OBJDIR)/*.d)

# top-level rules
all : create_dir $(EXE)

create_dir :
	@test -d $(OBJDIR) || mkdir -p $(OBJDIR)


#############################################################################
# FILES
#############################################################################

#############################################################################
OBJ = \
	$(OBJDIR)/irc_client.o \
	$(OBJDIR)/main.o \

$(OBJDIR)/irc_client.o : $(SRCDIR)/irc_client.cpp; $(DO_CC)
$(OBJDIR)/main.o : $(SRCDIR)/main.cpp; $(DO_CC)


#############################################################################

# Automatic dependency
-include $(DEPS)

$(EXE) : $(OBJ)
	$(CC) $(OBJ) -o $(EXE) $(LFLAGS)
	$(DEBUG_INFO)

clean:
	rm $(OBJDIR)*.o
# DO NOT DELETE
