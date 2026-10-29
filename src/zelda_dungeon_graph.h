#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { ZD_ROOM_COUNT=320, ZD_MAX_DOORS=16 };
typedef struct ZeldaDungeonDoor {
 uint8_t direction,position,type,layer;
} ZeldaDungeonDoor;
typedef struct ZeldaDungeonTopology {
 ZeldaDungeonDoor doors[ZD_MAX_DOORS];
 uint8_t count,layout;
 bool valid;
} ZeldaDungeonTopology;

/* Original room stream: floor/layout bytes, then three object layers.
 * Objects occupy three bytes; FFF0 starts two-byte door records, and FFFF
 * ends a layer. A bounded parser never invents doors from malformed data. */
static inline bool ZeldaDungeonParseTopology(const uint8_t *data,size_t size,
                                            ZeldaDungeonTopology *out) {
 ZeldaDungeonTopology room={0};
 if(!data||!out||size<2)return false;
 room.layout=data[1]>>2;
 size_t p=2;
 for(unsigned layer=0;layer<3;layer++) {
  bool doors=false,ended=false;
  while(p+1<size) {
   unsigned word=data[p]|data[p+1]<<8;
   if(word==0xffff){p+=2;ended=true;break;}
   if(!doors&&word==0xfff0){p+=2;doors=true;continue;}
   if(doors) {
    if(room.count==ZD_MAX_DOORS)return false;
    ZeldaDungeonDoor *d=&room.doors[room.count++];
    d->direction=data[p]&3;d->position=data[p]>>4;
    d->type=data[p+1];d->layer=(uint8_t)layer;
    if(d->position>=12)return false;
    p+=2;
   }else {
    if(p+2>=size)return false;
    p+=3;
   }
  }
  if(!ended)return false;
 }
 room.valid=true;*out=room;return true;
}
static inline bool ZeldaDungeonOrdinaryDoor(const ZeldaDungeonDoor *d) {
 /* Entrance/exit graphics, staircase masks and warp-room doors keep their
  * native travel routing. Locks/shutters/bomb walls remain floor edges;
  * their native collision decides when the player may cross them. */
 return !(d->type&1)&&!(d->type>=6&&d->type<=18)&&
        !(d->type>=32&&d->type<=38)&&d->type!=70&&d->type<=74;
}
static inline bool ZeldaDungeonBoundaryDoor(const ZeldaDungeonDoor *d) {
 return ZeldaDungeonOrdinaryDoor(d)&&((d->direction&1)?d->position>=6:d->position<6);
}
static inline int ZeldaDungeonNeighbor(unsigned room,unsigned direction) {
 if(room>=ZD_ROOM_COUNT||direction>3)return -1;
 if((direction==0&&room<16)||(direction==1&&room+16>=ZD_ROOM_COUNT)||
    (direction==2&&(room&15)==0)||(direction==3&&(room&15)==15))return -1;
 static const int delta[4]={-16,16,-1,1};
 return (int)room+delta[direction];
}
static inline bool ZeldaDungeonRoomsConnect(unsigned room,const ZeldaDungeonTopology *a,
                                           unsigned next,const ZeldaDungeonTopology *b) {
 if(!a->valid||!b->valid)return false;
 for(unsigned i=0;i<a->count;i++) {
  const ZeldaDungeonDoor*d=&a->doors[i];
  if(!ZeldaDungeonBoundaryDoor(d)||ZeldaDungeonNeighbor(room,d->direction)!=(int)next)continue;
  for(unsigned j=0;j<b->count;j++) {
   const ZeldaDungeonDoor*e=&b->doors[j];
   if(ZeldaDungeonBoundaryDoor(e)&&e->direction==(d->direction^1)&&
      e->position%6==d->position%6)return true;
  }
 }
 return false;
}

/* The native map's low nibble is its basement count; floors use a signed
 * byte (FF is B1). Each authored floor occupies 25 room entries. */
static inline int ZeldaDungeonFloorOrdinal(uint8_t floor,unsigned specification,unsigned count) {
 int index=(int)(int8_t)floor+(int)(specification&15);
 return index>=0&&(unsigned)index<count?index:-1;
}
