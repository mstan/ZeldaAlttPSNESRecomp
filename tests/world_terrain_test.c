#include "zelda_world_terrain.h"
#undef NDEBUG
#include <assert.h>
static ZeldaWorldTerrainEdits edits;
int main(void) {
 ZeldaWorldTerrainReset(&edits,64);
 assert(!ZeldaWorldTerrainFind(&edits,0,0,64));
 assert(!ZeldaWorldTerrainStage(&edits,0,0,0,1));
 assert(!ZeldaWorldTerrainStage(&edits,-1,0,64,1));
 assert(!ZeldaWorldTerrainStage(&edits,0,4096,64,1));
 /* Exercise every possible entry, including the last uint16 index. */
 for(unsigned i=0;i<65536;i++)assert(ZeldaWorldTerrainStage(&edits,(i%256)*16,(i/256)*16,64,(uint16_t)i));
 assert(edits.count==65536);
 for(unsigned i=0;i<65536;i++) {
  uint16_t *p=ZeldaWorldTerrainFind(&edits,(i%256)*16+15,(i/256)*16+15,64);
  assert(p&&*p==i);
 }
 assert(ZeldaWorldTerrainStage(&edits,4095,4095,64,42));
 assert(edits.count==65536&&*ZeldaWorldTerrainFind(&edits,4095,4095,64)==42);
 /* Abort/discard must make all old writes unreachable in either realm. */
 ZeldaWorldTerrainReset(&edits,0);
 assert(edits.count==0&&!ZeldaWorldTerrainFind(&edits,4095,4095,64));
 assert(!ZeldaWorldTerrainFind(&edits,4095,4095,0));
 assert(ZeldaWorldTerrainStage(&edits,0,0,0,7));
 assert(edits.count==1&&*ZeldaWorldTerrainFind(&edits,0,0,0)==7);
 return 0;
}
