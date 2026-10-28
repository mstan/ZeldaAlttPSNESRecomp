#include "zelda_world_coordinates.h"
#undef NDEBUG
#include <assert.h>

int main(void) {
 for(unsigned world=0;world<=64;world+=64) {
  assert(ZeldaWorldCell(0,0,world)==world);
  assert(ZeldaWorldCell(4095,4095,world)==world+63);
  assert(ZeldaWorldCell(4080+8,4080+16,world)==world+63);
  assert(ZeldaWorldCell(-1,-1,world)==world);
  assert(ZeldaWorldCell(512,511,world)==world+1);
  assert(ZeldaWorldCell(511,512,world)==world+8);
  for(unsigned raw=0;raw<65536;raw++) {
   int n=ZeldaWorldLinkCoordinate((uint16_t)raw);
   assert(n>=0&&n<=4080);
   unsigned cell=ZeldaWorldCell(n+8,n+16,world);
   assert(cell>=world&&cell<world+64);
  }
 }
 assert(ZeldaWorldLinkCoordinate(65535)==0);
 assert(ZeldaWorldLinkCoordinate(65534)==0);
 assert(ZeldaWorldLinkCoordinate(4081)==4080);
 assert(ZeldaWorldLinkCoordinate(0)==0);
 /* The native ledge landing search rebases Y and Z together. That must
  * leave the camera target unchanged, including at takeoff and landing. */
 assert(ZeldaWorldFollowY(1912,4,1)==1908);
 assert(ZeldaWorldFollowY(1992,84,1)==1908);
 assert(ZeldaWorldFollowY(1992,0,1)==1992);
 assert(ZeldaWorldFollowY(1992,UINT16_MAX,1)==1992);
 assert(ZeldaWorldFollowY(1992,84,0)==1992);
 assert(ZeldaWorldFollowY(8,16,1)==-8);
 assert(ZeldaWorldFollowY(2000,0xfff0,1)==2016);
 return 0;
}
