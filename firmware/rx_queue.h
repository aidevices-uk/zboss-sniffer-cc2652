#ifndef RX_QUEUE_H
#define RX_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/rf_data_entry.h)
#include DeviceFamily_constructPath(driverlib/rf_mailbox.h)

/* PHY header + MPDU without FCS (125) + RSSI + correlation/CRC byte. */
#define RX_DATA_BYTES 128u
#define RX_ENTRIES 8u

void rxQueueInit(dataQueue_t *queue);
void rxQueueReset(dataQueue_t *queue);
uint8_t rxEntryStatus(const rfc_dataEntryGeneral_t *entry);
rfc_dataEntryGeneral_t *rxQueueCurrent(void);
void rxQueueRelease(void);

#endif
