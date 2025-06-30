CC := $(ATH_CROSS_COMPILE_TYPE)gcc
TARGET_TYPE ?= AR9888
TARGET_VERS ?= v2

all:
ifeq ($(GENL_LOG),y)
	$(CC) -DCONFIG_CLD80211_LIB -I/usr/include/libnl3 -g3 -Wall  \
		-I ../uapi/linux \
		-I ../target/inc \
		cld-fwlog-netlink.c parser.c nan-parser.c cld-diag-parser.c cld80211_lib.c -o cnss_diag_lite -lnl-genl-3 -lnl-3 -lpthread
else
	$(CC) -g3 -Wall  \
		-I ../uapi/linux \
		-I ../target/inc \
		cld-fwlog-netlink.c parser.c nan-parser.c cld-diag-parser.c -o cnss_diag_lite -static -lpthread
endif

clean:
	rm -f cnss_diag_lite
