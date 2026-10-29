#ifndef ZELDA_WORLD_TERRAIN_H
#define ZELDA_WORLD_TERRAIN_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A scoped actor tick may touch several native collision windows. Keep its
 * writes private until the guest scope succeeds. Coordinates index the full
 * 256 by 256 Map16 world; a repeated write occupies only one journal entry. */
typedef struct ZeldaWorldTerrainEdits {
 unsigned world,count;
 uint8_t touched[65536/8];
 uint16_t index[65536],value[65536];
} ZeldaWorldTerrainEdits;
static inline void ZeldaWorldTerrainReset(ZeldaWorldTerrainEdits *e,unsigned world) {
 e->world=world;e->count=0;memset(e->touched,0,sizeof e->touched);
}
static inline uint16_t *ZeldaWorldTerrainFind(ZeldaWorldTerrainEdits *e,int x,int y,unsigned world) {
 if(!e||e->world!=world||x<0||y<0||x>=4096||y>=4096)return NULL;
 unsigned i=(unsigned)(y/16)*256+(unsigned)(x/16);
 return (e->touched[i/8]&(1u<<(i&7)))?&e->value[i]:NULL;
}
static inline bool ZeldaWorldTerrainStage(ZeldaWorldTerrainEdits *e,int x,int y,unsigned world,uint16_t value) {
 if(!e||e->world!=world||x<0||y<0||x>=4096||y>=4096)return false;
 unsigned i=(unsigned)(y/16)*256+(unsigned)(x/16);
 if(!(e->touched[i/8]&(1u<<(i&7)))) {
  if(e->count>=65536)return false;
  e->touched[i/8]|=(uint8_t)(1u<<(i&7));e->index[e->count++]=(uint16_t)i;
 }
 e->value[i]=value;return true;
}
#endif
