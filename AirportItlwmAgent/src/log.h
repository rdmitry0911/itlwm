/*
 * AirportItlwmAgent — logging macros.
 *
 * No raw key bytes ever appear in any log line. All log helpers
 * print scalar metadata only (lengths, return codes, NETWORK_NAME).
 *
 * NETWORK_NAME is printed in cleartext because it is not secret (visible
 * in any beacon scan); credential and PMK are NEVER printed.
 */
#ifndef AGENT_LOG_H
#define AGENT_LOG_H

#include <os/log.h>

#define AGENT_LOG(fmt, ...) \
    os_log(OS_LOG_DEFAULT, "AirportItlwmAgent: " fmt, ##__VA_ARGS__)

#define AGENT_ERR(fmt, ...) \
    os_log_error(OS_LOG_DEFAULT, "AirportItlwmAgent: " fmt, ##__VA_ARGS__)

#endif /* AGENT_LOG_H */
