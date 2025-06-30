LOCAL_PATH=.
SRC = libtcmd.c  nl80211.c  os.c

OBJ = $(SRC:.c=.o)

OUT = libtcmd.a

default: $(OUT)

INCLUDES += -I. -I../libathtestcmd/libtlvutil/common

#CFLAGS+= -DWLAN_API_NL80211 -DLIBNL_2 -DCONFIG_AR6002_REV6
CFLAGS+= -DWLAN_API_NL80211 -DLIBNL_2 
CFLAGS += $(TOOL_EXTRA_CFLAGS)

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -lbsd -o $@ 

$(OUT): $(OBJ)
#	$(AR) rcs $(OUT) $(OBJ) 

	ar rcs $(OUT) $(OBJ) 
clean:
	rm -f $(OBJ) $(OUT) 

