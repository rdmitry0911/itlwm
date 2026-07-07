#
# AirportItlwmAgent — standalone Makefile for the project-owned
# PLTI PMK producer daemon.
#
# Build:     make
# Install:   sudo make install
# Uninstall: sudo make uninstall
#
# The helper is a root LaunchDaemon producer for the
# AirportItlwm PLTI PMK ingress carrier. It opens the 'PLTI'
# user client on the kext, blocks in WaitAssociationTarget until
# the kext's PSK association-start edge publishes a target,
# looks up the matching project-owned
# /Library/Keychains/AirportItlwm.keychain password item by SSID,
# derives the 32-byte WPA2 PMK with PBKDF2-HMAC-SHA1 (4096
# iterations, ssid as salt), and delivers it back through
# DeliverPMK with the kext-assigned generation echoed as the scalar
# argument for the target-identity replay guard.
#
# CoreWLAN is intentionally NOT linked: prior decomp evidence
# proved the CWWiFiClient join-event path is unreachable for
# project helpers (HARD entitlement gate at airportd's
# __verifyEntitlementForEventType: for eventType 0x6d). The
# trigger now comes from the kext itself via the PLTI user
# client.
#

DAEMON_BIN := AirportItlwmAgent
PLIST := com.zxystd.airportitlwmagent.plist

INSTALL_BIN_DIR   := /usr/local/libexec
INSTALL_PLIST_DIR := /Library/LaunchDaemons

CC := clang

# The helper pins the apple80211_key carrier ABI by including the
# project-shipped MacKernelSDK header through an explicit relative
# path from src/userclient.c (NOT through a global -I search path).
# This keeps the system Availability.h / os/availability.h on the
# default search path so Foundation/CoreFoundation continue to
# parse correctly while still guaranteeing the helper's struct
# layout is byte-for-byte identical to what the kext dispatch
# validates via checkStructureInputSize == sizeof(struct apple80211_key).

# -fobjc-arc only affects .m sources; the .c sources compile as
# plain C. Common flags are listed in CFLAGS_COMMON; ObjC-only
# additions live in OBJCFLAGS. -D__IO80211_TARGET=__MAC_26_0 is
# required by MacKernelSDK/Headers/IOKit/80211/apple80211_var.h
# (the same gate the Tahoe kext build uses) so the struct
# apple80211_key carrier layout we pin matches exactly what the
# kext PMK sink validates with checkStructureInputSize.
CFLAGS_COMMON := -Wall -Wextra -O2 -Wno-deprecated-declarations \
                 -D__IO80211_TARGET=__MAC_26_0
OBJCFLAGS     := $(CFLAGS_COMMON) -fobjc-arc
CFLAGS        := $(CFLAGS_COMMON)

LDFLAGS := -framework Foundation \
           -framework IOKit \
           -framework Security \
           -framework CoreFoundation

DAEMON_M_SRCS := src/main.m
DAEMON_C_SRCS := src/userclient.c \
                 src/keychain.c \
                 src/wpa_key.c

DAEMON_M_OBJS := $(DAEMON_M_SRCS:.m=.o)
DAEMON_C_OBJS := $(DAEMON_C_SRCS:.c=.o)
DAEMON_OBJS   := $(DAEMON_M_OBJS) $(DAEMON_C_OBJS)

all: $(DAEMON_BIN)

$(DAEMON_BIN): $(DAEMON_OBJS)
	$(CC) $(OBJCFLAGS) $(DAEMON_OBJS) $(LDFLAGS) -o $(DAEMON_BIN)
	codesign --force --sign - --identifier com.zxystd.airportitlwmagent $(DAEMON_BIN)

%.o: %.m
	$(CC) $(OBJCFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(DAEMON_OBJS) $(DAEMON_BIN)

install: $(DAEMON_BIN)
	install -d $(INSTALL_BIN_DIR)
	install -m 0755 -o root -g wheel $(DAEMON_BIN) $(INSTALL_BIN_DIR)/$(DAEMON_BIN)
	install -d $(INSTALL_PLIST_DIR)
	install -m 0644 -o root -g wheel $(PLIST) $(INSTALL_PLIST_DIR)/$(PLIST)
	launchctl bootstrap system $(INSTALL_PLIST_DIR)/$(PLIST) || true
	launchctl enable system/com.zxystd.airportitlwmagent || true
	launchctl kickstart -k system/com.zxystd.airportitlwmagent || true

uninstall:
	launchctl bootout system $(INSTALL_PLIST_DIR)/$(PLIST) 2>/dev/null || true
	rm -f $(INSTALL_BIN_DIR)/$(DAEMON_BIN)
	rm -f $(INSTALL_PLIST_DIR)/$(PLIST)

.PHONY: all clean install uninstall
