#
# AirportItlwmAgent — standalone Makefile (rev8 DIAGNOSTIC_INSTRUMENTATION).
#
# Build:     make
# Install:   sudo make install
# Uninstall: sudo make uninstall
#
# This rev8 supersedes the REJECTED rev7 SYSTEM_CONTRACT_FIX request
# (REJECTED at Stage 1: airportd producer-side contract not closed,
# cited decomp artifacts absent). The helper is now a behavior-neutral
# CWWiFiClient delegate observer. No keychain access, no PBKDF2, no
# kext IOUserClient, no PMK delivery.
#

DAEMON_BIN := AirportItlwmAgent
PLIST := com.zxystd.airportitlwmagent.plist

INSTALL_BIN_DIR   := /usr/local/libexec
INSTALL_PLIST_DIR := /Library/LaunchDaemons

CC := clang
OBJCFLAGS := -Wall -Wextra -O2 -fobjc-arc
LDFLAGS := -framework Foundation \
           -framework CoreWLAN

DAEMON_M_SRCS := src/main.m
DAEMON_M_OBJS := $(DAEMON_M_SRCS:.m=.o)
DAEMON_OBJS := $(DAEMON_M_OBJS)

all: $(DAEMON_BIN)

$(DAEMON_BIN): $(DAEMON_OBJS)
	$(CC) $(OBJCFLAGS) $(DAEMON_OBJS) $(LDFLAGS) -o $(DAEMON_BIN)
	codesign --force --sign - --identifier com.zxystd.airportitlwmagent $(DAEMON_BIN)

%.o: %.m
	$(CC) $(OBJCFLAGS) -c $< -o $@

clean:
	rm -f $(DAEMON_M_OBJS) $(DAEMON_BIN)

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
