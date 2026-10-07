#ifndef RADIO_IEEE_H
#define RADIO_IEEE_H

#include <stdbool.h>
#include <stdint.h>

bool radioOpen(void);
void radioStop(void);
bool radioStart(uint8_t channel);
bool radioIsRunning(void);
bool radioFailed(void);
bool radioTakeFrame(uint8_t *dst, uint8_t *phr);
bool radioOverflowed(void);
void radioClearOverflow(void);

#endif
