#ifndef HOST_PROTOCOL_H
#define HOST_PROTOCOL_H

#include <stdint.h>

/* TRxSniffer ZBoss page-0 host protocol.
 * UART is 115200 8N1. A channel byte 11-26 starts capture.
 * 0xAA pauses and 0xBB stops. Records are at most 128 bytes:
 * length (includes these 4 header bytes), type, uint16 zero, payload.
 */

#define HOST_BAUD 115200

#define HOST_CMD_PAUSE 0xAAu
#define HOST_CMD_STOP 0xBBu

#define HOST_CHANNEL_MIN 11u
#define HOST_CHANNEL_MAX 26u

#define HOST_TYPE_COMPLETE 0u
#define HOST_TYPE_BIG_LENGTH 1u
#define HOST_TYPE_OVERFLOW 2u

#define HOST_RECORD_MAX 128u
#define HOST_HEADER_SIZE 4u

/* CC2531 RSSI register units. The host displays dBm as raw - 73. */
#define HOST_RSSI_OFFSET 73

#endif
