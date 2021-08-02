include ../common.mak
include $(SRCBASE)/.config


CFLAGS += -idirafter$(LINUXDIR)/include

CFLAGS += -ffunction-sections -fdata-sections
CFLAGS	+= -I$(TOP)/shared -I$(SRCBASE)/include -I$(SRCBASE)/common/include
CFLAGS	+= $(if $(WLAN_ComponentIncPath),$(WLAN_ComponentIncPath),$(addprefix -I,$(wildcard $(SRCBASE)/shared/bcmwifi/include)))
CFLAGS	+= -s -O2
LDFLAGS += -L$(TOP_PLATFORM)/nvram$(BCMEX)${EX7} ${EXTRA_NV_LDFLAGS} -lnvram
LDFLAGS += -L$(TOP)/shared -L$(INSTALLDIR)/shared/usr/lib -lshared
ifeq ($(HND_ROUTER),y)
LDFLAGS += $(EXTRALDFLAGS)
endif

all:
	@$(CC) $(CFLAGS) $(LDFLAGS) fixjffs.c -o fixjffs
	@$(STRIP) fixjffs

