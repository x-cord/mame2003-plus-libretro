#ifndef BREW_SMAF_H
#define BREW_SMAF_H

#include "driver.h"

/* BREW's IMedia objects own independent SMAF phrase playback slots. */
extern struct CustomSound_interface brew_smaf_interface;
void brew_smaf_reset(UINT8* (*reader)(UINT32, UINT32));
UINT32 brew_smaf_load(unsigned slot, UINT32 address, UINT32 size);
void brew_smaf_play(unsigned slot);
void brew_smaf_stop(unsigned slot);
void brew_smaf_pause(unsigned slot, int paused);
void brew_smaf_volume(unsigned slot, unsigned volume);
void brew_smaf_sync(void);
int brew_smaf_finished(unsigned slot);

#endif
