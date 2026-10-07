#include "rx_queue.h"

#define RX_HEADER_BYTES 8u

static uint8_t rxBuffer[RX_ENTRIES * (RX_HEADER_BYTES + RX_DATA_BYTES)]
    __attribute__((aligned(4)));

static rfc_dataEntryGeneral_t *readEntry;
static uint16_t entryStride;

static uint16_t strideFor(uint16_t dataBytes)
{
    uint16_t raw = (uint16_t)(RX_HEADER_BYTES + dataBytes);
    uint16_t pad = (uint16_t)((4u - (raw & 3u)) & 3u);
    return (uint16_t)(raw + pad);
}

static void writeEntry(uint8_t *entry, uint8_t *next)
{
    rfc_dataEntryGeneral_t *general = (rfc_dataEntryGeneral_t *)entry;

    general->pNextEntry = next;
    general->status = DATA_ENTRY_PENDING;
    general->config.type = DATA_ENTRY_TYPE_GEN;
    general->config.lenSz = 0;
    general->config.irqIntv = 0;
    general->length = RX_DATA_BYTES;
}

void rxQueueInit(dataQueue_t *queue)
{
    uint8_t i;

    entryStride = strideFor(RX_DATA_BYTES);
    for (i = 0; i < RX_ENTRIES; i++)
    {
        uint8_t *entry = &rxBuffer[i * entryStride];
        uint8_t *next = &rxBuffer[((i + 1u) % RX_ENTRIES) * entryStride];
        writeEntry(entry, next);
    }

    queue->pCurrEntry = rxBuffer;
    queue->pLastEntry = 0;
    readEntry = (rfc_dataEntryGeneral_t *)rxBuffer;
}

void rxQueueReset(dataQueue_t *queue)
{
    rxQueueInit(queue);
}

uint8_t rxEntryStatus(const rfc_dataEntryGeneral_t *entry)
{
    return ((const volatile rfc_dataEntryGeneral_t *)entry)->status;
}

rfc_dataEntryGeneral_t *rxQueueCurrent(void)
{
    return readEntry;
}

void rxQueueRelease(void)
{
    volatile rfc_dataEntryGeneral_t *entry = (volatile rfc_dataEntryGeneral_t *)readEntry;

    entry->status = DATA_ENTRY_PENDING;
    readEntry = (rfc_dataEntryGeneral_t *)entry->pNextEntry;
}
