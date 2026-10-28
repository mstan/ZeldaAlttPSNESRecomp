#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "desktop/sdl_compat.h"

/* Optional local-play world view. All terrain and actors come from the
 * owner's verified USA ROM; no extracted game assets are distributed. */
void ZeldaWorldEnable(void);
void ZeldaWorldDisable(void);
bool ZeldaWorldEnabled(void);
void ZeldaWorldReset(void);
void ZeldaWorldFrameEnd(void);
bool ZeldaWorldFrameBegin(uint32_t *inputs);
void ZeldaWorldEvent(const SDL_Event *event);
void ZeldaWorldSetup(void);
void ZeldaWorldActorBegin(void);
void ZeldaWorldRewardBegin(void);
void ZeldaWorldTransitionBegin(void);
void ZeldaWorldTransitionEnd(void);
struct SaveLoadInfo;
void ZeldaWorldStateSave(struct SaveLoadInfo *sli);
void ZeldaWorldStateLoad(struct SaveLoadInfo *sli,uint32_t version);
void ZeldaWorldStateLoaded(uint32_t version);
bool ZeldaWorldStatePreflight(const void *data,size_t size);
const char *ZeldaWorldStateIdentity(void);
const uint32_t *ZeldaWorldPresent(const uint32_t *native, int native_pitch,
    int drawable_width, int drawable_height, int *width, int *height);
