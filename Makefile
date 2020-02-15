### OpenFFA Makefile ###

CONFIG_FILE ?= .config

-include $(CONFIG_FILE)

ifndef CPU
    CPU := $(shell uname -m | sed -e s/i.86/i386/ -e s/amd64/x86_64/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/ -e s/alpha/axp/)
endif

ifndef REV
    REV := $(shell git rev-list --count HEAD)
endif

ifndef VER
    VER := r$(REV)~$(shell git rev-parse --short HEAD)
endif

CC ?= gcc
WINDRES ?= windres
STRIP ?= strip
RM ?= rm -f

CFLAGS ?= -std=gnu99 -O2 -fno-strict-aliasing -g -Wall -MMD $(INCLUDES)
LDFLAGS ?= -shared
LIBS ?=

ifdef CONFIG_WINDOWS
    LDFLAGS += -mconsole
    LDFLAGS += -Wl,--nxcompat,--dynamicbase
else
    CFLAGS += -fPIC -fvisibility=hidden
    LDFLAGS += -Wl,--no-undefined
endif

ifndef CONFIG_X86_NO_SSE_MATH
    ifeq ($(CPU),x86)
        CFLAGS += -msse -mfpmath=sse
    endif
    ifeq ($(CPU),i386)
        CFLAGS += -msse -mfpmath=sse
    endif
endif

CFLAGS += -DOPENFFA_VERSION='"$(VER)"' -DOPENFFA_REVISION=$(REV)
RCFLAGS += -DOPENFFA_VERSION='\"$(VER)\"' -DOPENFFA_REVISION=$(REV)

OBJS := g_bans.o g_chase.o g_cmds.o g_combat.o g_func.o g_items.o g_main.o \
g_misc.o g_phys.o g_spawn.o g_svcmds.o g_target.o g_trigger.o g_utils.o \
g_vote.o g_weapon.o p_client.o p_hud.o p_menu.o p_view.o p_weapon.o q_shared.o

ifdef CONFIG_VARIABLE_SERVER_FPS
    CFLAGS += -DUSE_FPS=1
endif

ifdef CONFIG_SQLITE
    SQLITE_CFLAGS ?=
    SQLITE_LIBS ?= -lsqlite3
    CFLAGS += -DUSE_SQLITE=1 $(SQLITE_CFLAGS)
    LIBS += $(SQLITE_LIBS)
    OBJS += g_sqlite.o
endif

ifdef CONFIG_CURL
    CURL_CFLAGS ?= $(shell curl-config --cflags)
    CURL_LIBS ?= $(shell curl-config --libs)
    CFLAGS += -DUSE_CURL=1 $(CURL_CFLAGS)
    LIBS += $(CURL_LIBS)
    OBJS += g_curl.o
endif

ifdef CONFIG_UDP
    CFLAGS += -DUSE_UDP=1
    OBJS += g_udp.o
endif

ifdef CONFIG_WINDOWS
    OBJS += openffa.o
    TARGET := game$(CPU).dll
else
    LIBS += -lm
    TARGET := game$(CPU).so
endif

all: $(TARGET)

default: all

.PHONY: all default clean strip

# Define V=1 to show command line.
ifdef V
    Q :=
    E := @true
else
    Q := @
    E := @echo
endif

-include $(OBJS:.o=.d)

%.o: %.c
	$(E) [CC] $@
	$(Q)$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.rc
	$(E) [RC] $@
	$(Q)$(WINDRES) $(RCFLAGS) -o $@ $<

$(TARGET): $(OBJS)
	$(E) [LD] $@
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(E) [CLEAN]
	$(Q)$(RM) *.o *.d $(TARGET)

strip: $(TARGET)
	$(E) [STRIP]
	$(Q)$(STRIP) $(TARGET)
