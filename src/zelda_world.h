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
void ZeldaWorldHardwareReset(void);
void ZeldaWorldFrameEnd(void);
bool ZeldaWorldFrameBegin(uint32_t *inputs);
bool ZeldaWorldEvent(const SDL_Event *event);
void ZeldaWorldSetup(void);
void ZeldaWorldAttachWindow(SDL_Window *window);
void ZeldaWorldPrepareNativeHud(void);
bool ZeldaWorldPollUI(bool paused);
void ZeldaWorldActorBegin(void);
void ZeldaWorldEntranceBegin(void);
void ZeldaWorldPitBegin(void);
void ZeldaWorldLinkBegin(void);
void ZeldaWorldLinkEnd(void);
void ZeldaWorldSpritesEnd(void);
void ZeldaWorldRewardBegin(void);
void ZeldaWorldBombDamageBegin(void);
void ZeldaWorldMedallionBegin(void);
void ZeldaWorldInteractionBegin(void);
void ZeldaWorldItemReceiptBegin(void);
void ZeldaWorldSecretBegin(void);
void ZeldaWorldSecretEnd(void);
void ZeldaWorldPegBegin(void);
void ZeldaWorldWeathervaneBegin(void);
void ZeldaWorldGargoyleBegin(void);
void ZeldaWorldPyramidBegin(void);
void ZeldaWorldPuzzleEnd(void);
void ZeldaWorldDungeonTileBegin(void);
void ZeldaWorldDungeonTileEnd(void);
void ZeldaWorldProjectileCollisionBegin(void);
void ZeldaWorldProjectileCollisionEnd(void);
void ZeldaWorldSpawnBegin(void);
void ZeldaWorldSpawnEnd(void);
void ZeldaWorldHookshotBounds(void);
void ZeldaWorldAncillaBegin(void);
void ZeldaWorldAncillasEnd(void);
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
