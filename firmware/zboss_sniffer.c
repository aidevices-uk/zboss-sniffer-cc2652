#include "host_protocol.h"
#include "radio_ieee.h"
#include "rx_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ti/drivers/GPIO.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/UART2.h>
#include <ti/drivers/dpl/ClockP.h>
#include <ti/drivers/power/PowerCC26XX.h>

#include "ti_drivers_config.h"

#define TX_RING_SIZE 2048u
#define TX_RING_MASK (TX_RING_SIZE - 1u)
#define BLINK_US 250000u

static UART2_Handle uart;
static uint8_t txRing[TX_RING_SIZE];
static uint16_t txHead;
static uint16_t txTail;
static uint16_t txCount;
static bool overflowMarked;
static bool sniffing;
static bool radioUp;
static uint8_t channel;
static uint32_t lastBlinkTick;
static bool greenOn;

static uint16_t txFree(void)
{
    return (uint16_t)(TX_RING_SIZE - txCount);
}

static bool txPut(const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (length > txFree())
    {
        return false;
    }
    for (i = 0; i < length; i++)
    {
        txRing[txHead] = data[i];
        txHead = (uint16_t)((txHead + 1u) & TX_RING_MASK);
    }
    txCount = (uint16_t)(txCount + length);
    return true;
}

static void txFlushSome(void)
{
    uint16_t contig;
    size_t written = 0;

    if (uart == 0 || txCount == 0)
    {
        if (txCount == 0)
        {
            overflowMarked = false;
        }
        return;
    }

    contig = (uint16_t)(TX_RING_SIZE - txTail);
    if (contig > txCount)
    {
        contig = txCount;
    }
    if (contig > 64u)
    {
        contig = 64u;
    }

    UART2_write(uart, &txRing[txTail], contig, &written);
    if (written > txCount)
    {
        written = txCount;
    }
    txTail = (uint16_t)((txTail + written) & TX_RING_MASK);
    txCount = (uint16_t)(txCount - written);
    if (txCount == 0)
    {
        overflowMarked = false;
    }
}

static void markOverflow(void)
{
    static const uint8_t marker[HOST_HEADER_SIZE] = {
        HOST_HEADER_SIZE, HOST_TYPE_OVERFLOW, 0, 0};

    if (!overflowMarked && txPut(marker, HOST_HEADER_SIZE))
    {
        overflowMarked = true;
    }
}

static uint8_t hostRssi(int8_t dbm)
{
    int raw = (int)dbm + HOST_RSSI_OFFSET;

    /* The host subtracts 73 from an unsigned byte and clamps to a signed
     * byte. Values below -73 dBm would wrap and look strong, so clamp.
     */
    if (raw < 0)
    {
        raw = 0;
    }
    if (raw > 200)
    {
        raw = 200;
    }
    return (uint8_t)raw;
}

static uint8_t hostStatus(uint8_t corrCrc)
{
    uint8_t corr = (uint8_t)(corrCrc & 0x3Fu);

    /* Radio bit 7 means CRC error. The host treats bit 7 as CRC OK. */
    if ((corrCrc & 0x80u) != 0)
    {
        return corr;
    }
    return (uint8_t)(corr | 0x80u);
}

static void enqueueFrame(const uint8_t *raw, uint8_t phr)
{
    uint8_t record[HOST_RECORD_MAX];
    uint16_t payloadLen = (uint16_t)phr + 1u;
    uint16_t recordLen = (uint16_t)(payloadLen + HOST_HEADER_SIZE);
    uint16_t i;
    int8_t dbm;
    uint8_t corr;

    if (recordLen > HOST_RECORD_MAX)
    {
        markOverflow();
        return;
    }

    dbm = (int8_t)raw[phr - 1u];
    corr = raw[phr];
    record[0] = (uint8_t)recordLen;
    record[1] = HOST_TYPE_COMPLETE;
    record[2] = 0;
    record[3] = 0;
    for (i = 0; i < payloadLen; i++)
    {
        record[HOST_HEADER_SIZE + i] = raw[i];
    }
    record[HOST_HEADER_SIZE + phr - 1u] = hostRssi(dbm);
    record[HOST_HEADER_SIZE + phr] = hostStatus(corr);

    if (!txPut(record, recordLen))
    {
        markOverflow();
    }
}

static void drainRadio(void)
{
    uint8_t frame[RX_DATA_BYTES];
    uint8_t phr;

    if (!radioUp)
    {
        return;
    }
    while (radioTakeFrame(frame, &phr))
    {
        enqueueFrame(frame, phr);
    }
}

static void handleByte(uint8_t byte)
{
    if (!radioUp)
    {
        return;
    }

    if (byte == HOST_CMD_PAUSE || byte == HOST_CMD_STOP)
    {
        drainRadio();
        radioStop();
        sniffing = false;
        return;
    }

    if (byte < HOST_CHANNEL_MIN || byte > HOST_CHANNEL_MAX)
    {
        return;
    }

    drainRadio();
    if (sniffing && channel == byte && radioIsRunning() && !radioFailed())
    {
        return;
    }
    channel = byte;
    sniffing = radioStart(byte);
}

static void readUart(void)
{
    uint8_t buf[32];
    size_t n = 0;
    int32_t status;
    size_t i;

    if (uart == 0)
    {
        return;
    }

    status = UART2_read(uart, buf, sizeof(buf), &n);
    if (status != UART2_STATUS_SUCCESS || n == 0)
    {
        return;
    }
    for (i = 0; i < n; i++)
    {
        handleByte(buf[i]);
    }
}

static void setGreen(bool on)
{
    GPIO_write(CONFIG_GPIO_GLED, on ? CONFIG_GPIO_LED_ON : CONFIG_GPIO_LED_OFF);
    greenOn = on;
}

static void updateLeds(void)
{
    bool fault;
    bool overflow;
    uint32_t period;
    uint32_t need;
    uint32_t now;

    if (sniffing && radioFailed())
    {
        sniffing = false;
    }
    fault = !radioUp || radioFailed();
    if (sniffing && txCount < (TX_RING_SIZE / 2u) && !overflowMarked)
    {
        radioClearOverflow();
    }
    overflow = radioOverflowed() || overflowMarked;
    GPIO_write(CONFIG_GPIO_RLED, (fault || overflow) ? CONFIG_GPIO_LED_ON : CONFIG_GPIO_LED_OFF);

    if (fault)
    {
        setGreen(false);
        return;
    }
    if (sniffing)
    {
        setGreen(true);
        return;
    }

    period = ClockP_getSystemTickPeriod();
    if (period == 0)
    {
        period = 1;
    }
    need = BLINK_US / period;
    if (need == 0)
    {
        need = 1;
    }
    now = ClockP_getSystemTicks();
    if ((uint32_t)(now - lastBlinkTick) >= need)
    {
        lastBlinkTick = now;
        setGreen(!greenOn);
    }
}

void *mainThread(void *arg0)
{
    UART2_Params params;

    (void)arg0;

    Power_setConstraint(PowerCC26XX_DISALLOW_STANDBY);
    GPIO_setConfig(CONFIG_GPIO_GLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_RLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

    UART2_Params_init(&params);
    params.baudRate = HOST_BAUD;
    params.readMode = UART2_Mode_NONBLOCKING;
    params.writeMode = UART2_Mode_BLOCKING;
    params.readReturnMode = UART2_ReadReturnMode_PARTIAL;
    uart = UART2_open(CONFIG_UART2_0, &params);

    radioUp = (uart != 0) && radioOpen();
    lastBlinkTick = ClockP_getSystemTicks();

    for (;;)
    {
        readUart();
        if (sniffing)
        {
            drainRadio();
        }
        txFlushSome();
        updateLeds();
    }
}
