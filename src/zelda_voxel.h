#ifndef ZELDA_VOXEL_H
#define ZELDA_VOXEL_H

#include <stddef.h>
#include <stdint.h>
#include <SDL.h>

void ZeldaVoxelHandleEvent(const SDL_Event *event);
uint32_t ZeldaVoxelRemapInput(uint32_t input);
void ZeldaVoxelConfigurePpu(void);
void ZeldaVoxelPostRender(uint8_t *pixels, size_t pitch,
                          int width, int height);

#endif
