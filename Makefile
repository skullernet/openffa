# this nice line comes from the linux kernel makefile
ARCH:=$(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/ -e s/alpha/axp/)

VERSION:=$(shell cat REVISION)
REVISION:=$(shell cat REVISION | tr -d -c [:digit:])
DEFINES:=-DVERSION=\"r$(VERSION)\" -DREVISION=$(REVISION)

ifdef USE_SQLITE
DEFINES+=-DUSE_SQLITE=1
endif

VIS:=$(shell ./vis.sh)

CFLAGS:=-pipe -ffloat-store $(VIS) -O2 -g -fPIC $(DEFINES) -Wall -Wstrict-prototypes
ifdef USE_SQLITE
LDFLAGS:=-lsqlite3
endif

SRCFILES=q_shared.c \
	g_chase.c   g_func.c   g_misc.c   g_svcmds.c   g_utils.c   p_hud.c \
	g_cmds.c    g_items.c  g_phys.c   g_target.c   g_weapon.c  p_view.c \
	g_combat.c  g_main.c   g_spawn.c  g_trigger.c  p_client.c  p_weapon.c \
	p_menu.c    g_vote.c   g_bans.c

ifdef USE_SQLITE
SRCFILES+=g_sqlite.c
endif

OBJFILES=$(SRCFILES:%.c=%.o)

TARGET=game$(ARCH).so

default: $(TARGET)

strip: $(TARGET)
	strip $^

clean:
	rm -f *.o $(TARGET)

.PHONY: clean

$(TARGET): $(OBJFILES)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
