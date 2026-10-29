#include "zelda_dungeon_graph.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>

int main(void) {
 /* Ordinary object bytes may contain direction-like values. Only the door
  * section can establish an edge, and a reciprocal boundary is required. */
 const uint8_t data[]={0,4,0x10,0x20,0x30,0xf0,0xff,0x61,0x1c,0xff,0xff,
                       0xff,0xff,0xff,0xff};
 ZeldaDungeonTopology a={0},b={0};
 assert(ZeldaDungeonParseTopology(data,sizeof data,&a));
 assert(a.layout==1&&a.count==1&&a.doors[0].type==0x1c);
 b.valid=true;b.count=1;b.doors[0]=(ZeldaDungeonDoor){0,0,0,0};
 assert(ZeldaDungeonRoomsConnect(32,&a,48,&b));
 assert(ZeldaDungeonRoomsConnect(48,&b,32,&a));
 assert(!ZeldaDungeonRoomsConnect(32,&a,49,&b));
 b.doors[0].position=1;assert(!ZeldaDungeonRoomsConnect(32,&a,48,&b));
 b.doors[0].position=0;b.doors[0].type=70;
 assert(!ZeldaDungeonRoomsConnect(32,&a,48,&b));
 b.doors[0].type=0;b.doors[0].position=6;
 assert(!ZeldaDungeonRoomsConnect(32,&a,48,&b));
 for(size_t n=0;n<sizeof data;n++) {
  ZeldaDungeonTopology untouched={0},expected=untouched;
  assert(!ZeldaDungeonParseTopology(data,n,&untouched));
  assert(!memcmp(&untouched,&expected,sizeof expected));
 }
 assert(!ZeldaDungeonParseTopology(data,sizeof data,NULL));
 assert(ZeldaDungeonFloorOrdinal(0,0x20,2)==0);
 assert(ZeldaDungeonFloorOrdinal(1,0x20,2)==1);
 assert(ZeldaDungeonFloorOrdinal(2,0x20,2)==-1);
 assert(ZeldaDungeonFloorOrdinal(0xff,0x23,5)==2);
 assert(ZeldaDungeonFloorOrdinal(0xfd,0x23,5)==0);
 assert(ZeldaDungeonFloorOrdinal(0xfc,0x23,5)==-1);
 assert(ZeldaDungeonNeighbor(0,0)==-1);
 assert(ZeldaDungeonNeighbor(0,2)==-1);
 assert(ZeldaDungeonNeighbor(15,3)==-1);
 assert(ZeldaDungeonNeighbor(319,1)==-1);
 return 0;
}
