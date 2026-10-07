# ZBOSS sniffer for the CC2652R1 LaunchPad

Firmware for a Texas Instruments LAUNCHXL-CC26X2R1 (CC2652R1). It captures IEEE 802.15.4 frames on Zigbee channels 11–26 and speaks the ZBOSS sniffer protocol.

This firmware is designed to work with the [aidevices.uk](https://aidevices.uk) TRxSniffer application.

The image is 2.4 GHz only. It does not run the CC2531 USB stack, and it is not a copy of the CC1352P dual-band hex. Do not program `zboss_sniffer_r22_cc1352px.hex` onto this board.

## TRxSniffer settings

TRxSniffer selects this protocol by radio name. There is no separate CC2652 name. Use the CC2531 page-0 command, which is the channel number itself.

| Setting | Value |
| --- | --- |
| Radio | `zboss-cc2531` |
| Page | 0 |
| Channel | 11–26 |
| Baud | 115200 8N1 |
| Port | LaunchPad XDS110 Application/User UART |

The host writes `0xBB`, waits briefly, then writes the channel byte. This firmware also accepts `0xAA` as pause. Leave the LaunchPad’s XDS110 USB cable connected. The SensorTag debug probe should stay unplugged while CCS or UniFlash talks to this board, because a first connection can update the XDS110 firmware.

## What the board shows

Green is DIO7 and red is DIO6.

- Green blinking: idle or paused. The radio is off.
- Green solid: capturing.
- Red solid: the UART record buffer overflowed, or the radio command stopped.
- Both off, or red only: the radio or UART did not start.

## Wire format

Records are the TRxSniffer ZBoss layout, at most 128 bytes:

1. Length of the whole record, including these four header bytes.
2. Type: `0` complete, `1` not used for a stored frame, `2` overflow.
3. Two zero bytes.

A complete payload matches a CC2530 receive FIFO with auto-CRC:

1. PHY length, including the two FCS bytes. The host skips this byte.
2. The MPDU without the FCS.
3. RSSI in CC2531 register units. TRxSniffer displays `raw - 73` dBm. The CC2652 reports dBm directly, and the firmware adds 73 before sending. Readings below -73 dBm are sent as -73 so they do not wrap.
4. Status. Bit 7 is set when the CRC is good. The radio’s own bit means the opposite, and the firmware inverts it.

Frames whose PHY length is above 123 do not fit in a 128-byte record. Those frames are dropped and the red LED is lit. An overflow record (`length 4`, `type 2`) is sent when the record buffer has room.

## Build

Install these under `C:\ti`, which is what this tree expects:

- Code Composer Studio 12.8.0 (`C:\ti\ccs1280`)
- SimpleLink Low Power F2 SDK 8.33.00.16 (`C:\ti\simplelink_cc13xx_cc26xx_sdk_8_33_00_16`)
- SysConfig 1.21.1 (`C:\ti\sysconfig_1.21.1`)

The SDK stays on the machine. It is not vendored in this repository.

From the repository root:

```powershell
.\build.ps1
```

The Intel hex file is `firmware\ticlang\zboss_sniffer_cc2652.hex`.

CCS 12.8 can import `firmware\ticlang\zboss_sniffer_CC26X2R1_LAUNCHXL_nortos_ticlang.projectspec`. Select the SimpleLink CC13xx/CC26xx SDK and SysConfig when CCS asks for products.

## Program the LaunchPad

Use CCS or UniFlash with an XDS110 connection and device CC2652R1F.

- Erase unprotected sectors only.
- Leave “lock debug interface”, “disable bootloader”, “keep CCFG”, and sector write-protect unchecked.
- Do not mass-erase the part.

The image uses the SimpleLink SDK default customer configuration. Debug access stays enabled and a chip erase stays allowed. The ROM serial bootloader is left disabled, which is the SDK default and does not lock the debug probe.

After reset the green LED blinks until TRxSniffer sends a channel.

## Limits

- Channels 11–26 only. Sub-GHz pages are not implemented.
- The CC2652R1 radio cannot transmit a sniffer acknowledgement. Capture is passive: frame filtering is off, auto-ACK is off, and CRC failures are still delivered with bit 7 clear.
- `.\build.ps1` completes with CCS 12.8 and SimpleLink SDK 8.33 and writes `firmware\ticlang\zboss_sniffer_cc2652.hex`. The image has not been programmed onto a LaunchPad or checked against a live network yet.
