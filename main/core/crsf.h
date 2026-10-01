#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "core/sys_state.h"

/**
 * CRSF Protocol Definitions for Aegis-TX
 * =================================
 * Based on ExpressLRS/Crossfire standard.
 * UART Speed: 420,000 bps
 * Frame Rate: 250Hz - 500Hz
 */

#define CRSF_ADDRESS_TX_MODULE      0xEE
#define CRSF_ADDRESS_RADIO_TRANSMITTER 0xEA
#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8
#define CRSF_SYNC_BYTE              CRSF_ADDRESS_TX_MODULE

#define CRSF_FRAMETYPE_RC_CHANNELS      0x16
#define CRSF_FRAMETYPE_LINK_STATISTICS  0x14
#define CRSF_FRAMETYPE_BATTERY_SENSOR   0x08
#define CRSF_FRAMETYPE_GPS              0x02
#define CRSF_FRAMETYPE_ATTITUDE         0x1E
#define CRSF_FRAMETYPE_FLIGHT_MODE      0x21

#define CRSF_PACKET_SIZE            26  // [Sync][Len][Type][Payload(22)][CRC]

// CRSF Internal Scaling (Standard ELRS)
// Maps 1000us -> 191, 1500us -> 992, 2000us -> 1792
#define CRSF_CHANNEL_VALUE_MIN      172
#define CRSF_CHANNEL_VALUE_1000     191
#define CRSF_CHANNEL_VALUE_MID      992
#define CRSF_CHANNEL_VALUE_2000     1792
#define CRSF_CHANNEL_VALUE_MAX      1811

/**
 * CRSF RC Channels are 11-bit values packed into 22 bytes.
 * We will manually pack them in crsf.c instead of using bitfields.
 */

/**
 * Functions for CRSF Protocol
 */
void crsf_init(void);
void crsf_set_power(bool enable);
void crsf_send_channels(const uint16_t channels[16]);
void crsf_receive_telemetry(TelemetryData_t *telemetry);
uint8_t crsf_compute_crc(const uint8_t *data, uint8_t len);
uint16_t us_to_crsf(uint16_t us);
