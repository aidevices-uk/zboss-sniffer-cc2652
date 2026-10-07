#include "radio_ieee.h"

#include "rx_queue.h"

#include <stddef.h>

#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/rf_common_cmd.h)
#include DeviceFamily_constructPath(driverlib/rf_data_entry.h)
#include DeviceFamily_constructPath(driverlib/rf_ieee_cmd.h)
#include DeviceFamily_constructPath(driverlib/rf_ieee_mailbox.h)
#include DeviceFamily_constructPath(driverlib/rf_mailbox.h)
#include DeviceFamily_constructPath(rf_patches/rf_patch_cpe_ieee_802_15_4.h)
#include <ti/drivers/rf/RF.h>

static RF_Object rfObject;
static RF_Handle rfHandle;
static RF_CmdHandle rxHandle = RF_ALLOC_ERROR;
static dataQueue_t rxQueue;
static rfc_ieeeRxOutput_t rxOutput;
static uint8_t lastBufFull;
static bool overflowLatched;
static bool failed;
static bool running;

static RF_Mode ieeeMode = {
    .rfMode = RF_MODE_IEEE_15_4,
    .cpePatchFxn = &rf_patch_cpe_ieee_802_15_4,
    .mcePatchFxn = 0,
    .rfePatchFxn = 0,
};

/* CC2652R1 is 2.4 GHz only. Differential front end, internal bias.
 * txPower 0x7217 is the CC26x2 IEEE setup value. Receive does not use it.
 */
static rfc_CMD_RADIO_SETUP_t radioSetup = {
    .commandNo = 0x0802,
    .status = IDLE,
    .pNextOp = 0,
    .startTime = 0,
    .startTrigger.triggerType = TRIG_NOW,
    .startTrigger.bEnaCmd = 0,
    .startTrigger.triggerNo = 0,
    .startTrigger.pastTrig = 1,
    .condition.rule = COND_NEVER,
    .condition.nSkip = 0,
    .mode = 0x01,
    .loDivider = 0,
    .config.frontEndMode = 0,
    .config.biasMode = 0,
    .config.analogCfgMode = 0,
    .config.bNoFsPowerUp = 0,
    .config.bSynthNarrowBand = 0,
    .txPower = 0x7217,
    .pRegOverride = 0,
};

/* Promiscuous IEEE RX. FCS is checked by the radio and replaced in the
 * queue entry by RSSI plus a status byte, matching a CC2530 AUTOCRC FIFO.
 * bAppendCorrCrc bit 7 is CRC error, not CRC OK.
 */
static rfc_CMD_IEEE_RX_t rxCmd = {
    .commandNo = CMD_IEEE_RX,
    .status = IDLE,
    .pNextOp = 0,
    .startTime = 0,
    .startTrigger.triggerType = TRIG_NOW,
    .startTrigger.bEnaCmd = 0,
    .startTrigger.triggerNo = 0,
    .startTrigger.pastTrig = 1,
    .condition.rule = COND_NEVER,
    .condition.nSkip = 0,
    .channel = 0,
    .rxConfig.bAutoFlushCrc = 0,
    .rxConfig.bAutoFlushIgn = 0,
    .rxConfig.bIncludePhyHdr = 1,
    .rxConfig.bIncludeCrc = 0,
    .rxConfig.bAppendRssi = 1,
    .rxConfig.bAppendCorrCrc = 1,
    .rxConfig.bAppendSrcInd = 0,
    .rxConfig.bAppendTimestamp = 0,
    .pRxQ = 0,
    .pOutput = 0,
    .frameFiltOpt.frameFiltEn = 0,
    .frameFiltOpt.frameFiltStop = 0,
    .frameFiltOpt.autoAckEn = 0,
    .frameFiltOpt.slottedAckEn = 0,
    .frameFiltOpt.autoPendEn = 0,
    .frameFiltOpt.defaultPend = 0,
    .frameFiltOpt.bPendDataReqOnly = 0,
    .frameFiltOpt.bPanCoord = 0,
    .frameFiltOpt.maxFrameVersion = 3,
    .frameFiltOpt.fcfReservedMask = 0,
    .frameFiltOpt.modifyFtFilter = 0,
    .frameFiltOpt.bStrictLenFilter = 0,
    .frameTypes.bAcceptFt0Beacon = 1,
    .frameTypes.bAcceptFt1Data = 1,
    .frameTypes.bAcceptFt2Ack = 1,
    .frameTypes.bAcceptFt3MacCmd = 1,
    .frameTypes.bAcceptFt4Reserved = 1,
    .frameTypes.bAcceptFt5Reserved = 1,
    .frameTypes.bAcceptFt6Reserved = 1,
    .frameTypes.bAcceptFt7Reserved = 1,
    .ccaOpt.ccaEnEnergy = 0,
    .ccaOpt.ccaEnCorr = 0,
    .ccaOpt.ccaEnSync = 0,
    .ccaOpt.ccaCorrOp = 1,
    .ccaOpt.ccaSyncOp = 1,
    .ccaOpt.ccaCorrThr = 0,
    .ccaRssiThr = 0,
    .numExtEntries = 0,
    .numShortEntries = 0,
    .pExtEntryList = 0,
    .pShortEntryList = 0,
    .localExtAddr = 0,
    .localShortAddr = 0,
    .localPanID = 0,
    .endTrigger.triggerType = TRIG_NEVER,
    .endTrigger.bEnaCmd = 0,
    .endTrigger.triggerNo = 0,
    .endTrigger.pastTrig = 1,
    .endTime = 0,
};

static void clearOutput(void)
{
    uint8_t *bytes = (uint8_t *)&rxOutput;
    uint8_t i;

    for (i = 0; i < sizeof(rxOutput); i++)
    {
        bytes[i] = 0;
    }
    lastBufFull = 0;
}

bool radioOpen(void)
{
    RF_Params params;

    rxQueueInit(&rxQueue);
    clearOutput();
    rxCmd.pRxQ = &rxQueue;
    rxCmd.pOutput = &rxOutput;

    RF_Params_init(&params);
    rfHandle = RF_open(&rfObject, &ieeeMode, (RF_RadioSetup *)&radioSetup, &params);
    failed = (rfHandle == 0);
    return rfHandle != 0;
}

void radioStop(void)
{
    if (rfHandle != 0 && rxHandle >= 0)
    {
        RF_cancelCmd(rfHandle, rxHandle, RF_ABORT_GRACEFULLY);
        RF_pendCmd(rfHandle, rxHandle,
                   RF_EventCmdStopped | RF_EventCmdAborted | RF_EventCmdCancelled |
                       RF_EventLastCmdDone);
        rxHandle = RF_ALLOC_ERROR;
    }
    running = false;
}

bool radioStart(uint8_t channel)
{
    if (rfHandle == 0 || channel < 11u || channel > 26u)
    {
        failed = true;
        return false;
    }

    radioStop();
    rxQueueReset(&rxQueue);
    clearOutput();
    rxCmd.channel = channel;
    rxCmd.status = IDLE;
    rxCmd.pRxQ = &rxQueue;
    rxCmd.pOutput = &rxOutput;

    rxHandle = RF_postCmd(rfHandle, (RF_Op *)&rxCmd, RF_PriorityNormal, 0, 0);
    if (rxHandle < 0)
    {
        failed = true;
        running = false;
        return false;
    }

    failed = false;
    running = true;
    return true;
}

bool radioIsRunning(void)
{
    return running;
}

bool radioFailed(void)
{
    uint16_t status;

    if (failed || rfHandle == 0)
    {
        return true;
    }
    if (!running)
    {
        return false;
    }

    status = ((volatile rfc_CMD_IEEE_RX_t *)&rxCmd)->status;
    if (status >= DONE_OK)
    {
        running = false;
        failed = true;
        return true;
    }
    return false;
}

bool radioOverflowed(void)
{
    return overflowLatched;
}

void radioClearOverflow(void)
{
    overflowLatched = false;
}

bool radioTakeFrame(uint8_t *dst, uint8_t *phrOut)
{
    if (rxOutput.nRxBufFull != lastBufFull)
    {
        lastBufFull = rxOutput.nRxBufFull;
        overflowLatched = true;
    }

    for (;;)
    {
        rfc_dataEntryGeneral_t *entry = rxQueueCurrent();
        uint8_t status = rxEntryStatus(entry);
        uint8_t phr;
        const uint8_t *data;
        uint8_t i;

        if (status == DATA_ENTRY_PENDING || status == DATA_ENTRY_ACTIVE ||
            status == DATA_ENTRY_BUSY)
        {
            return false;
        }

        if (status == DATA_ENTRY_UNFINISHED)
        {
            overflowLatched = true;
            rxQueueRelease();
            continue;
        }

        if (status != DATA_ENTRY_FINISHED)
        {
            rxQueueRelease();
            continue;
        }

        data = &entry->data;
        phr = (uint8_t)(data[0] & 0x7Fu);
        /* Entry layout with PHY header, no FCS, RSSI, correlation/CRC:
         * [0]=length, [1 .. phr-2]=MPDU, [phr-1]=RSSI dBm, [phr]=status.
         * phr is the PHY length and includes the 2 FCS bytes that were not stored.
         */
        if (phr < 5u || phr > 127u || (uint16_t)phr + 1u > RX_DATA_BYTES)
        {
            overflowLatched = true;
            rxQueueRelease();
            continue;
        }

        dst[0] = phr;
        for (i = 1; i <= phr; i++)
        {
            dst[i] = data[i];
        }
        *phrOut = phr;
        rxQueueRelease();
        return true;
    }
}
