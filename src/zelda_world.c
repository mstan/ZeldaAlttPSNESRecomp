/* Persistent ALttP worlds. Reference: snesrev/zelda3 fbbb3f9 (MIT),
 * overworld.c, sprite.c and variables.h. Guest routines execute through the
 * normal AOT/LLE bridge; loaders never run on the real player's machine.
 * Current milestone and limitations are recorded in docs/SEAMLESS-WORLD.md.
 */
#include "zelda_world.h"
#include "zelda_world_coordinates.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "debug_server.h"
#include "config.h"
#include "desktop/opengl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ZW_AREAS=128, ZW_MAP=4096, ZW_WORLD=4096, ZW_HUD=32, ZW_EXTRA_POOLS=8 };
typedef struct WorldPiece {int16_t x,y;uint16_t tile;uint8_t flags,size;} WorldPiece;
/* Eight authored actors leave native slots for probes, enemy missiles and
 * other dynamic children. Each additional pool keeps an independent native
 * context; terrain, graphics and persistent player progress remain shared. */
typedef struct WorldActorPool {
 uint8_t ram[0x20000];
 WorldPiece pieces[1024];
 uint64_t ticks;
 unsigned piece_count;
 struct WorldActorPool *next;
} WorldActorPool;
/* ROM's canonical area-head table, not a rectangular room-ID assumption.
 * Each cell is 512px. Four cells may share one 1024px native area. */
static const uint8_t heads[64]={
 0,0,2,3,3,5,5,7,0,0,10,3,3,5,5,15,
 16,17,18,19,20,21,22,23,24,24,26,27,27,29,30,30,
 24,24,34,27,27,37,30,30,40,41,42,43,44,45,46,47,
 48,48,50,51,52,53,53,55,48,48,58,59,60,53,53,63};
typedef struct WorldArea {
  uint8_t ram[0x20000];
  uint16_t vram[0x8000], palette[256];
  uint16_t tiles[ZW_MAP];
  uint32_t *raster;
  uint64_t ticks;
  unsigned size;
  WorldPiece pieces[1024];
  unsigned piece_count;
  uint8_t obsel;
  bool valid, dirty, populated;
  WorldActorPool *pools;
} WorldArea;
static WorldArea *areas[ZW_AREAS];
static bool enabled, atlas;
static int zoom=3, active_world=-1, camera_x, camera_y;
static uint32_t *canvas;
static size_t canvas_capacity;
static int picture_width, picture_height;
static uint64_t decoded, failures;
static double decode_ms, render_ms;
static double simulation_ms;
static int drawable_w=1536,drawable_h=864;
static bool collision_window;
static int collision_x,collision_y,collision_world,last_area=-1;
static uint16_t collision_before[ZW_MAP];
static uint64_t handoffs;
static int native_oam_x,native_oam_y;
static unsigned last_call;
static WorldArea *sim_area;
static WorldActorPool *sim_pool;
static bool sim_reward;
static bool live_projection,live_first_actor,live_pieces_valid;
static unsigned live_area,live_piece_count;
static WorldPiece live_pieces[1024];
static uint16_t live_scroll[4];
static uint8_t live_native_oam[0x2a0];
static WorldPiece weapon_pieces[1024];
static unsigned weapon_piece_count;
static bool weapon_projection;
static int weapon_scroll_x,weapon_scroll_y;
static uint16_t weapon_adjusted_scroll[4];
static int projected_weapon=-1;
static bool projected_arrow;
static uint8_t weapon_oam_allocator[12],weapon_oam_pointer[4];
static uint16_t bomb_damage_mask;
static uint8_t bomb_damage_image[10][0x1000];
typedef struct ArrowTarget {uint16_t area;uint8_t pool,slot,type,valid;uint16_t spawn;} ArrowTarget;
typedef struct NativeWeapon {uint8_t fields[38];} NativeWeapon;
static ArrowTarget arrow_targets[5];
static uint16_t arrow_collision_mask,arrow_resolved_mask;
static uint8_t arrow_collision_image[5][0x1000];
static int sim_scroll_x,sim_scroll_y;
static uint64_t actor_hooks;
static bool held_transition_velocity;
static uint8_t held_vx,held_vy;
extern void (*g_interp_bridge_pc_hook)(uint32_t,int,int);
static void (*previous_pc_hook)(uint32_t,int,int);
static SDL_mutex *command_mutex;
static SDL_Window *world_window;
static uint64_t ui_resume_deadline;
static bool command_pending,command_ready;
static char pending_command[64],pending_args[512],command_reply[4096];
static void service_commands(void);
static void replay_boundary(uint32_t *inputs);
typedef struct ReplayDigest {uint64_t all,core,world;size_t bytes;} ReplayDigest;
static struct {
 int phase,n,index,first_bad;
 uint32_t inputs;
 bool final_equal;
 size_t cap,checkpoint_bytes,final_bytes,first_diff;
 uint8_t *checkpoint,*scratch,*final;
 ReplayDigest digests[600],expected,actual;
} replay;
static uint16_t rd16(unsigned off) {return g_ram[off]|g_ram[off+1]<<8;}
static void wr16(unsigned off,unsigned n) {g_ram[off]=(uint8_t)n;g_ram[off+1]=(uint8_t)(n>>8);}
static int clamp(int x,int a,int b) {return x<a?a:x>b?b:x;}
static unsigned owner(unsigned id) {return heads[id&63]|(id&64);}
static unsigned area_size(unsigned id) {
 for(unsigned i=0;i<64;i++)if(i!=(id&63)&&heads[i]==(id&63))return 1024;
 return 512;
}
static bool playable(void) {return enabled&&g_ram[0x10]==9&&g_ram[0x11]==0&&rd16(0x8a)<128;}
static bool call(unsigned pc,unsigned p,unsigned a,unsigned x,unsigned y) {
  /* A synthetic native return frame is private to this scope. The bridge
   * stops when RTS/RTL returns past its entry, before executing the sentinel.
   * Widths are the original entry ABI, never inherited from another loader. */
  last_call=pc;
  unsigned frame=pc==0x00e19b||pc==0x00d394||pc==0x0ed5a8||pc==0x09c499||pc==0x09c22f||pc==0x068328?3:2;
  g_cpu.S=(uint16_t)(0x1ff-frame);g_cpu.D=0;g_cpu.DB=(uint8_t)(pc>>16);g_cpu.PB=(uint8_t)(pc>>16);
  g_cpu.P=(uint8_t)(p|4);g_cpu.emulation=0;cpu_p_to_mirrors(&g_cpu);
  g_cpu.A=(uint16_t)a;g_cpu.X=(uint16_t)x;g_cpu.Y=(uint16_t)y;
  g_cpu.host_return_valid=0;
  g_ram[g_cpu.S+1]=0xfe;g_ram[g_cpu.S+2]=0xff;
  if(frame==3)g_ram[g_cpu.S+3]=(uint8_t)(pc>>16);
  if(cpu_dispatch_has_entry(&g_cpu,pc))
    return cpu_dispatch_pc_paired(&g_cpu,pc,(uint8_t)frame)==RECOMP_RETURN_NORMAL;
  return interp_bridge_run(&g_cpu,pc)!=0;
}
static uint32_t color(unsigned p) {
 unsigned r=p&31,g=(p>>5)&31,b=(p>>10)&31;
 return 0xff000000u|((r<<3|r>>2)<<16)|((g<<3|g>>2)<<8)|(b<<3|b>>2);
}
static uint32_t tile_pixel(const WorldArea *r,unsigned tile,unsigned x,unsigned y) {
 if(tile&0x4000)x=7-x;if(tile&0x8000)y=7-y;
 unsigned adr=(0x2000+(tile&1023)*16+y)&0x7fff;
 unsigned a=r->vram[adr],b=r->vram[(adr+8)&0x7fff],s=7-x;
 /* Planes 0/1 share one word; planes 2/3 share the next 8-word row. */
 unsigned p=((a>>s)&1)|(((a>>(8+s))&1)<<1)|(((b>>s)&1)<<2)|(((b>>(8+s))&1)<<3);
 return color(r->palette[p?((tile>>10)&7)*16+p:0]);
}
static void rasterize(unsigned id) {
 WorldArea *r=areas[id];if(!r||!r->dirty)return;
 unsigned size=r->size;
 if(!r->raster)r->raster=malloc((size_t)size*size*4);
 if(!r->raster){failures++;return;}
 for(unsigned my=0;my<size/16;my++)for(unsigned mx=0;mx<size/16;mx++) {
  unsigned mt=r->tiles[my*64+mx];
  if(mt>=3752)mt=0;
  const uint8_t *def=RomPtr(0x0f8000+mt*8);
  for(unsigned q=0;q<4;q++) {
   unsigned tile=def[q*2]|def[q*2+1]<<8;
   for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)
    r->raster[(my*16+(q>>1)*8+y)*size+mx*16+(q&1)*8+x]=tile_pixel(r,tile,x,y);
  }
 }
 r->dirty=false;
}
/* Outdoor native actor fields, including the word-sized authored spawn ID.
 * Keep the unique Mirror portal in the live CPU context. Area caches must
 * never acquire a second owner for it when Link crosses an ordinary seam. */
typedef struct NativeActor {uint8_t fields[60];} NativeActor;
static NativeActor mirrored_actor;
static int mirrored_slot=-1;
static const uint32_t actor_fields[]={0xb58,0xb6b,0xb89,0xba0,0xbb0,0xbe0,
 0xc9a,0xcaa,0xcba,0xcd2,0xce2,0x1f9c2,0x1fa1c,0x1fa2c,0x1fa3c,0x1fa4c};
static void read_actor(NativeActor*out,const uint8_t*ram,unsigned k) {
 unsigned n=0;
 for(unsigned i=0;i<sizeof actor_fields/sizeof *actor_fields;i++)out->fields[n++]=ram[actor_fields[i]+k];
 for(unsigned a=0xd00;a<=0xf90;a+=16)out->fields[n++]=ram[a+k];
 out->fields[n++]=ram[0xbc0+k*2];out->fields[n]=ram[0xbc1+k*2];
}
static void write_actor(uint8_t*ram,unsigned k,const NativeActor*in) {
 unsigned n=0;
 for(unsigned i=0;i<sizeof actor_fields/sizeof *actor_fields;i++)ram[actor_fields[i]+k]=in->fields[n++];
 for(unsigned a=0xd00;a<=0xf90;a+=16)ram[a+k]=in->fields[n++];
 ram[0xbc0+k*2]=in->fields[n++];ram[0xbc1+k*2]=in->fields[n];
}
static void remove_cached_portals(uint8_t*ram) {
 for(unsigned k=0;k<16;k++)if(ram[0xe20+k]==0x6c)ram[0xdd0+k]=0;
}
static const uint16_t weapon_fields[]={0x380,0x385,0x38a,0x38f,0x394,0x39f,0x3a4,0x3a9,
 0x3b1,0x3c5,0x3ca,0x3cf,0x3d5,0x3db,0x3e4,0x3ea};
static void read_weapon(NativeWeapon*out,const uint8_t*ram,unsigned k) {
 unsigned n=0;
 for(unsigned a=0x280;a<=0x2a8;a+=10)out->fields[n++]=ram[a+k];
 for(unsigned a=0xbf0;a<=0xc90;a+=10)out->fields[n++]=ram[a+k];
 for(unsigned i=0;i<sizeof weapon_fields/sizeof *weapon_fields;i++)out->fields[n++]=ram[weapon_fields[i]+k];
}
static void write_weapon(uint8_t*ram,unsigned k,const NativeWeapon*in) {
 unsigned n=0;
 for(unsigned a=0x280;a<=0x2a8;a+=10)ram[a+k]=in->fields[n++];
 for(unsigned a=0xbf0;a<=0xc90;a+=10)ram[a+k]=in->fields[n++];
 for(unsigned i=0;i<sizeof weapon_fields/sizeof *weapon_fields;i++)ram[weapon_fields[i]+k]=in->fields[n++];
}
static uint8_t* target_ram(const ArrowTarget*t) {
 if(!t->valid||t->area>=ZW_AREAS||t->pool>ZW_EXTRA_POOLS||t->slot>=16||!areas[t->area])return NULL;
 if(!t->pool)return t->area==owner(rd16(0x8a))?g_ram:areas[t->area]->ram;
 WorldActorPool*p=areas[t->area]->pools;
 for(unsigned i=1;i<t->pool&&p;i++)p=p->next;
 return p?p->ram:NULL;
}
static void restore_mirrored_actor(void) {
 if(mirrored_slot>=0){write_actor(g_ram,(unsigned)mirrored_slot,&mirrored_actor);mirrored_slot=-1;}
}
static void capture(unsigned id) {
 WorldArea *r=areas[id];
 if((!collision_window&&memcmp(r->tiles,g_ram+0x2000,8192))||
    memcmp(r->vram,g_ppu->vram,sizeof r->vram)||
    memcmp(r->palette,g_ppu->cgram,sizeof r->palette))r->dirty=true;
 memcpy(r->ram,g_ram,sizeof r->ram);
 if(r->populated)remove_cached_portals(r->ram);
 if(!collision_window)memcpy(r->tiles,g_ram+0x2000,sizeof r->tiles);
 memcpy(r->ram+0x2000,r->tiles,sizeof r->tiles);
 memcpy(r->vram,g_ppu->vram,sizeof r->vram);
 memcpy(r->palette,g_ppu->cgram,sizeof r->palette);
 r->obsel=g_ppu->obsel;
 r->valid=true;
}
static void free_pools(WorldArea*r) {
 while(r->pools){WorldActorPool*p=r->pools;r->pools=p->next;free(p);}
}
static unsigned pool_count(const WorldArea*r) {
 unsigned n=0;for(WorldActorPool*p=r->pools;p;p=p->next)n++;return n;
}
typedef struct PoolReserve {WorldActorPool*free;} PoolReserve;
/* Budget aborts unwind C without running callback cleanup. Reserve memory
 * before entering a scope, and reclaim both unused and failed output pools
 * after it returns, as required by the guest-scope ownership contract. */
static void release_reserve(PoolReserve*r) {
 while(r->free){WorldActorPool*p=r->free;r->free=p->next;free(p);}
}
static bool prepare_reserve(PoolReserve*r) {
 r->free=NULL;
 for(unsigned i=0;i<ZW_EXTRA_POOLS;i++) {
  WorldActorPool*p=calloc(1,sizeof(*p));if(!p){release_reserve(r);return false;}
  p->next=r->free;r->free=p;
 }
 return true;
}
static bool populate_native(WorldArea*r,bool keep_live,PoolReserve*reserve) {
 if(!keep_live) {
  /* Sprite_DisableAll intentionally preserves the unique live Mirror portal.
   * A decoded neighbor must not clone that portal into every native area. */
  memset(g_ram+0xdd0,0,16);memset(g_ram+0xb00,0,8);
  memset(g_ram+0x1ef80,0,512);
 }
 WorldActorPool*pool=NULL;unsigned pools=0;
 for(unsigned blk=0;blk<4096;blk++) {
  if(!g_ram[0x1df80+blk]||(g_ram[0x1ef80+(blk>>3)]&(0x80>>(blk&7))))continue;
  for(unsigned attempt=0;attempt<2;attempt++) {
   unsigned actors=0,overlords=0;
   for(unsigned k=0;k<16;k++)actors+=g_ram[0xdd0+k]!=0;
   for(unsigned k=0;k<8;k++)overlords+=g_ram[0xb00+k]!=0;
   if(actors>=8||overlords>=6||attempt) {
    if(pools>=ZW_EXTRA_POOLS)return false;
    memcpy(pool?pool->ram:r->ram,g_ram,0x20000);
    WorldActorPool*p=reserve->free;if(!p)return false;
    reserve->free=p->next;p->next=NULL;
    if(pool)pool->next=p;else r->pools=p;
    pool=p;pools++;
    /* Reset native per-pool bookkeeping without resetting the authored map
     * or its shared loaded bits. No player or live portal is being reset. */
    g_ram[0x1b]=1;
    if(!call(0x09c22f,0x30,0,0,0))return false;
    g_ram[0x1b]=0;
    memset(g_ram+0x1d00,0,0x100);
   }
   wr16(0,blk);
   if(!call(0x09c739,0x30,0,0,0))return false;
   if(g_ram[0x1ef80+(blk>>3)]&(0x80>>(blk&7)))break;
   if(attempt)return false;
  }
 }
 memcpy(pool?pool->ram:r->ram,g_ram,0x20000);
 /* Native proximity loading now sees every authored spawn as resident,
  * including actors that live in another pool of this same area. */
 uint8_t loaded[512];memcpy(loaded,g_ram+0x1ef80,sizeof loaded);
 memcpy(r->ram+0x1ef80,loaded,sizeof loaded);
 for(WorldActorPool*p=r->pools;p;p=p->next)memcpy(p->ram+0x1ef80,loaded,sizeof loaded);
 memcpy(g_ram,r->ram,0x20000);r->populated=true;return true;
}
typedef struct Decode {unsigned id;WorldArea *area;PoolReserve*reserve;} Decode;
static bool populate_scope(void*opaque) {
 Decode*d=opaque;memcpy(g_ram,d->area->ram,sizeof d->area->ram);
 return populate_native(d->area,true,d->reserve);
}
static bool decode_scope(void *opaque) {
 Decode *d=opaque;unsigned id=d->id;WorldArea *r=d->area;
 wr16(0x8a,id);wr16(0x40a,id);g_ram[0x10]=9;g_ram[0x11]=g_ram[0x1b]=0;
 g_ram[0xf3ca]=(uint8_t)(id&64);
 wr16(0x20,((id&63)>>3)*512+128);wr16(0x22,(id&7)*512+128);
 if(!call(0x02e58b,0,0,0,0)||!call(0x00d394,0x30,(id&63)==3||(id&63)==5||(id&63)==7?0x58:0x5a,0,0)||
    !call(0x00e19b,0x30,0,0,0)||
    !call(0x02eec5,0x30,0,0,0)||!call(0x02c5b2,0x30,0,0,0)||
    !call(0x02c692,0x30,0,0,0))return false;
 unsigned bg=RomPtr(0x00fd1c)[id],sp=g_ram[0xfd40+id];g_ram[0]=(uint8_t)sp;
 if(!call(0x0ed5a8,0x30,bg,id,0)||!call(0x0ed618,0x30,0,0,0))return false;
 /* The normal NMI uploads one 0x400-byte animation frame after the loader.
  * A decode scope does not advance a whole frame; publish that same first
  * frame explicitly rather than inheriting another area's water tiles. */
 memcpy(g_ppu->vram+0x3c00,g_ram+0xa680,0x400);
 wr16(0xe2,(id&7)*512+128);wr16(0xe8,((id&63)>>3)*512+128);
 if(!call(0x09c499,0x30,0,0,0))return false;
 if(!populate_native(r,false,d->reserve))return false;
 memcpy(r->ram,g_ram,sizeof r->ram);
 memcpy(r->tiles,g_ram+0x2000,sizeof r->tiles);
 memcpy(r->vram,g_ppu->vram,sizeof r->vram);
 memcpy(r->palette,g_ram+0xc300,sizeof r->palette);
 r->obsel=g_ppu->obsel;
 return true;
}
static bool ensure(unsigned id) {
 id=owner(id);if(id>=ZW_AREAS)return false;
 if(!areas[id])areas[id]=calloc(1,sizeof(WorldArea));
 WorldArea *r=areas[id];if(!r)return false;
 if(r->valid)return true;
 r->size=area_size(id);PoolReserve reserve;Decode d={id,r,&reserve};
 uint64_t before=SDL_GetPerformanceCounter();
 free_pools(r);
 bool ok=prepare_reserve(&reserve);
 if(ok)ok=RtlGuestScopeRun(decode_scope,&d,2000000);
 release_reserve(&reserve);
 if(!ok){free_pools(r);r->populated=false;}
 decode_ms+=(SDL_GetPerformanceCounter()-before)*1000.0/SDL_GetPerformanceFrequency();
 if(ok){r->valid=r->dirty=true;decoded++;}else failures++;
 return ok;
}
static void collect_oam(WorldArea *r,WorldPiece*pieces,unsigned*count,int scroll_x,int scroll_y) {
 static const uint8_t sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
 for(unsigned i=0;i<128&&*count<1024;i++) {
  unsigned off=0x800+i*4,ext=g_ram[0xa20+i];
  if(g_ram[off+1]>=224)continue;
  int x=g_ram[off]|((ext&1)<<8);if(x>=256)x-=512;
  WorldPiece*p=&pieces[(*count)++];
  p->x=(int16_t)(scroll_x+x);p->y=(int16_t)(scroll_y+g_ram[off+1]);
  p->tile=g_ram[off+2];p->flags=g_ram[off+3];p->size=sizes[(r->obsel>>5)&7][(ext>>1)&1];
 }
}
static void collect_pieces(WorldArea *r,int scroll_x,int scroll_y) {
 collect_oam(r,sim_pool?sim_pool->pieces:r->pieces,
   sim_pool?&sim_pool->piece_count:&r->piece_count,scroll_x,scroll_y);
}
static void clear_projected_oam(void) {
 for(unsigned i=0;i<128;i++)g_ram[0x801+i*4]=0xf0;
 memset(g_ram+0xa20,0,128);memcpy(g_ram+0xfe0,RomPtr(0x0683c7),12);
}
static void finish_projected_weapon(void) {
 if(projected_weapon>=0&&projected_arrow&&g_ram[0xc4a+projected_weapon]==10) {
  unsigned j=g_ram[0x3a9+projected_weapon];
  if(j<16)arrow_targets[projected_weapon]=(ArrowTarget){(uint16_t)live_area,0,(uint8_t)j,
    g_ram[0xe20+j],1,(uint16_t)(g_ram[0xbc0+j*2]|g_ram[0xbc1+j*2]<<8)};
 }
 projected_weapon=-1;projected_arrow=false;
}
void ZeldaWorldAncillaBegin(void) {
 if(!live_projection||RtlGuestScopeActive())return;
 finish_projected_weapon();
 restore_mirrored_actor();
 WorldArea*r=areas[live_area];if(!r)return;
 if(weapon_projection)collect_oam(r,weapon_pieces,&weapon_piece_count,weapon_scroll_x,weapon_scroll_y);
 else {
  memcpy(live_native_oam,g_ram+0x800,sizeof live_native_oam);
  memcpy(weapon_oam_allocator,g_ram+0xfe0,sizeof weapon_oam_allocator);
  memcpy(weapon_oam_pointer,g_ram+0x90,sizeof weapon_oam_pointer);
  for(unsigned i=0;i<4;i++)weapon_adjusted_scroll[i]=rd16(0x11e + i*2);
  weapon_projection=true;
 }
 unsigned k=g_ram[0xfa0];if(k>=10)return;
 projected_weapon=(int)k;projected_arrow=k<5&&g_ram[0xc4a+k]==9;
 if(k<5) {
  ArrowTarget*t=&arrow_targets[k];
  if(g_ram[0xc4a+k]!=10)memset(t,0,sizeof(*t));
  else if(t->valid) {
   uint8_t*p=target_ram(t);
   bool matches=(t->area&64)==active_world&&p&&p[0xe20+t->slot]==t->type&&
     (p[0xbc0+t->slot*2]|p[0xbc1+t->slot*2]<<8)==t->spawn;
   if(p!=g_ram||!matches) {
    read_actor(&mirrored_actor,g_ram,t->slot);mirrored_slot=t->slot;
    if(matches){NativeActor a;read_actor(&a,p,t->slot);write_actor(g_ram,t->slot,&a);}
    else g_ram[0xdd0+t->slot]=0;
   }
  }
 }
 int x=g_ram[0xc04+k]|g_ram[0xc18+k]<<8,y=g_ram[0xbfa+k]|g_ram[0xc0e + k]<<8;
 weapon_scroll_x=x-128;weapon_scroll_y=y-112;
 wr16(0xe0,weapon_scroll_x);wr16(0xe2,weapon_scroll_x);wr16(0xe6,weapon_scroll_y);wr16(0xe8,weapon_scroll_y);
 wr16(0x11e,weapon_scroll_x);wr16(0x120,weapon_scroll_x);wr16(0x122,weapon_scroll_y);wr16(0x124,weapon_scroll_y);
 clear_projected_oam();
}
void ZeldaWorldAncillasEnd(void) {
 if(!weapon_projection||RtlGuestScopeActive())return;
 finish_projected_weapon();
 restore_mirrored_actor();
 collect_oam(areas[live_area],weapon_pieces,&weapon_piece_count,weapon_scroll_x,weapon_scroll_y);
 wr16(0xe0,live_scroll[0]);wr16(0xe2,live_scroll[1]);wr16(0xe6,live_scroll[2]);wr16(0xe8,live_scroll[3]);
 memcpy(g_ram+0x800,live_native_oam,sizeof live_native_oam);
 memcpy(g_ram+0xfe0,weapon_oam_allocator,sizeof weapon_oam_allocator);
 memcpy(g_ram+0x90,weapon_oam_pointer,sizeof weapon_oam_pointer);
 for(unsigned i=0;i<4;i++)wr16(0x11e + i*2,weapon_adjusted_scroll[i]);
 sim_scroll_x=live_scroll[1];sim_scroll_y=live_scroll[3];weapon_projection=false;
}
void ZeldaWorldActorBegin(void) {
 if(!RtlGuestScopeActive())ZeldaWorldAncillasEnd();
 WorldArea*r=RtlGuestScopeActive()?sim_area:live_projection?areas[live_area]:NULL;
 if(!r)return;
 if(live_projection&&!RtlGuestScopeActive()&&live_first_actor) {
  collect_oam(r,live_pieces,&live_piece_count,sim_scroll_x,sim_scroll_y);
  memcpy(live_native_oam,g_ram+0x800,sizeof live_native_oam);live_first_actor=false;
 }else collect_pieces(r,sim_scroll_x,sim_scroll_y);
 unsigned k=g_ram[0xfa0]&15;
 int x=g_ram[0xd10+k]|g_ram[0xd30+k]<<8,y=g_ram[0xd00+k]|g_ram[0xd20+k]<<8;
 sim_scroll_x=x-128;sim_scroll_y=y-112;
 wr16(0xe2,sim_scroll_x);wr16(0xe8,sim_scroll_y);wr16(0xe0,sim_scroll_x);wr16(0xe6,sim_scroll_y);
 /* The original outer Sprite_Main owns native return/skip contracts. Only
  * the projection changes for each actor, so native off-screen culling does
  * not kill or pause a distant actor merely because Link is elsewhere. */
 clear_projected_oam();
 actor_hooks++;
}
void ZeldaWorldSpritesEnd(void) {
 if(!live_projection||RtlGuestScopeActive())return;
 ZeldaWorldAncillasEnd();
 collect_pieces(areas[live_area],sim_scroll_x,sim_scroll_y);
 wr16(0xe0,live_scroll[0]);wr16(0xe2,live_scroll[1]);wr16(0xe6,live_scroll[2]);wr16(0xe8,live_scroll[3]);
 memcpy(g_ram+0x800,live_native_oam,sizeof live_native_oam);
 areas[live_area]->ticks++;
 live_projection=false;live_pieces_valid=true;
}
static void world_pc_hook(uint32_t pc,int m,int x) {
 if(previous_pc_hook)previous_pc_hook(pc,m,x);
 pc&=0x7fffff;
 if(pc==0x0684e2)ZeldaWorldActorBegin();
 if(pc==0x02a9c4)ZeldaWorldTransitionBegin();
 if(pc==0x068328)ZeldaWorldTransitionEnd();
 if(pc==0x06d13c)ZeldaWorldRewardBegin();
 if(pc==0x0683c1)ZeldaWorldSpritesEnd();
 if(pc==0x088287)ZeldaWorldBombDamageBegin();
 if(pc==0x08833c)ZeldaWorldAncillaBegin();
 if(pc==0x08824c)ZeldaWorldAncillasEnd();
 if(pc==0x088d68)ZeldaWorldProjectileCollisionBegin();
}
void ZeldaWorldProjectileCollisionBegin(void) {
 if(RtlGuestScopeActive()||!live_projection||!playable())return;
 unsigned k=g_ram[0xfa0];if(k>=5||g_ram[0xc4a+k]!=9)return;
 /* This is the original arrow's exact post-movement collision phase. The
  * deferred pass does not move it, spend ammo or advance its timer again. */
 memcpy(arrow_collision_image[k],g_ram,sizeof arrow_collision_image[k]);
 arrow_collision_mask|=(uint16_t)(1u<<k);
}
void ZeldaWorldBombDamageBegin(void) {
 if(RtlGuestScopeActive()||!playable())return;
 unsigned k=g_ram[0xfa0];if(k>=10||g_ram[0xc4a+k]!=7)return;
 /* Capture the exact native damage phase, before its fuse/animation changes.
  * Neighbor pools reuse only the enemy damage routine. The live bomb's
  * movement, fuse, inventory cost, terrain and player damage still run once. */
 memcpy(bomb_damage_image[k],g_ram,sizeof bomb_damage_image[k]);bomb_damage_mask|=(uint16_t)(1u<<k);
}
void ZeldaWorldRewardBegin(void) {
 if(!sim_area||!RtlGuestScopeActive())return;
 unsigned k=g_ram[0xfa0]&15,type=g_ram[0xe20+k];
 /* Ordinary absorbables only. NPC item receipts, big keys and quest
  * presentation require their own native routing, not an inventory write. */
 if(type>=0xd8&&type<0xe5)sim_reward=true;
}
void ZeldaWorldTransitionBegin(void) {
 if(!playable()||RtlGuestScopeActive()||held_transition_velocity)return;
 held_vx=g_ram[0x31];held_vy=g_ram[0x30];held_transition_velocity=true;
 /* Link has already moved and the camera has run. Bypass only the ordinary
  * velocity-gated edge checks; the original special-area router still runs.
  * Sprite_Main receives the real velocity again before it executes. */
 g_ram[0x30]=g_ram[0x31]=0;wr16(0x416,0);
}
void ZeldaWorldTransitionEnd(void) {
 if(RtlGuestScopeActive())return;
 if(held_transition_velocity){g_ram[0x30]=held_vy;g_ram[0x31]=held_vx;held_transition_velocity=false;}
 if(live_projection||!playable())return;
 live_area=owner(rd16(0x8a));WorldArea*r=areas[live_area];if(!r||!r->valid)return;
 live_scroll[0]=rd16(0xe0);live_scroll[1]=rd16(0xe2);live_scroll[2]=rd16(0xe6);live_scroll[3]=rd16(0xe8);
 sim_scroll_x=live_scroll[1];sim_scroll_y=live_scroll[3];
 r->piece_count=live_piece_count=0;live_first_actor=live_projection=true;
 weapon_piece_count=0;weapon_projection=false;
}
/* Only these native player effects cross a neighbor transaction. Native
 * Sprite_Main owns enemy AI, sword hitboxes, damage/recoil and hit timers;
 * its camera, OAM allocator and frame counters remain area-local. Player
 * weapons are global and must not advance once for every neighboring area. */
static const uint16_t combat_effects[]={0x27,0x28,0x46,0x4d,0x2e3,0x31f,0x360,0x373};
typedef struct Tick {
 unsigned id;int player_x,player_y;
 WorldActorPool *pool;
 uint8_t player[0x60],action[0xb0],progress[0x1000],effect[sizeof combat_effects/sizeof *combat_effects];
 uint8_t frame,sfx[3],rewards[0x1a];bool reward;
 uint16_t arrows,arrow_results;
 NativeWeapon weapons[5];ArrowTarget targets[5];
} Tick;
static bool tick_scope(void *opaque) {
 Tick*t=opaque;WorldArea*r=areas[t->id];
 memcpy(g_ram,t->pool?t->pool->ram:r->ram,sizeof r->ram);
 memcpy(g_ram+0x2000,r->tiles,sizeof r->tiles);
 memset(g_ram+0x1ef80,0xff,512);
 memcpy(g_ppu->vram,r->vram,sizeof r->vram);memcpy(g_ppu->cgram,r->palette,sizeof r->palette);
 memcpy(g_ram+0xf000,t->progress,sizeof t->progress);memcpy(g_ram+0x20,t->player,sizeof t->player);
 memcpy(g_ram+0x2d0,t->action,sizeof t->action);
 g_ram[0x10]=9;g_ram[0x11]=g_ram[0xfc1]=0;g_ram[0x1a]=t->frame;
 g_ram[0x15]=g_ram[0x16]=g_ram[0x17]=g_ram[0x18]=0;
 memset(g_ram+0xf0,0,10);
 /* Ancillas belong to the live player, including across an area handoff.
  * Do not run stale copies inherited when this area's loader was called. */
 memset(g_ram+0xc4a,0,10);
 g_ram[0xf3cc]=0;
 int ax=(t->id&7)*512,ay=((t->id&63)>>3)*512;
 wr16(0x708,ay);wr16(0x70c,ax>>3);wr16(0x70a,r->size-16);wr16(0x70e,(r->size-16)>>3);
 wr16(0x22,t->player_x);wr16(0x20,t->player_y);
 if(t->pool)t->pool->piece_count=0;else r->piece_count=0;
 sim_scroll_x=ax;sim_scroll_y=ay;
 wr16(0xe2,ax);wr16(0xe8,ay);wr16(0xe0,ax);wr16(0xe6,ay);
 wr16(0x11e,ax);wr16(0x120,ax);wr16(0x122,ay);wr16(0x124,ay);
 for(unsigned k=0;k<5;k++)if(t->arrows&(1u<<k)) {
  const uint8_t*p=arrow_collision_image[k];NativeWeapon weapon;read_weapon(&weapon,p,k);write_weapon(g_ram,k,&weapon);
  g_ram[0xfa0]=(uint8_t)k;
  int x=g_ram[0xc04+k]|g_ram[0xc18+k]<<8,y=g_ram[0xbfa+k]|g_ram[0xc0e + k]<<8;
  wr16(0xe0,x-128);wr16(0xe2,x-128);wr16(0xe6,y-112);wr16(0xe8,y-112);
  wr16(0x11e,x-128);wr16(0x120,x-128);wr16(0x122,y-112);wr16(0x124,y-112);
  if(!call(0x088d68,0x30,0,k,0))return false;
  bool hit=(g_cpu.P&1)!=0;unsigned j=g_cpu.Y&255;
  if(hit) {
   if(j>=16)return false;
   ArrowTarget target={(uint16_t)t->id,0,(uint8_t)j,g_ram[0xe20+j],1,
     (uint16_t)(g_ram[0xbc0+j*2]|g_ram[0xbc1+j*2]<<8)};
   for(WorldActorPool*q=r->pools;q;q=q->next){target.pool++;if(q==t->pool)break;}
   if(!t->pool)target.pool=0;
   /* Continue after the arrow's original JSR, with its native result flags
    * and Y target. This owns attachment/deflection behavior and lifetimes. */
   if(!call(0x08a16e,g_cpu.P,g_cpu.A,g_cpu.X,g_cpu.Y))return false;
   t->targets[k]=target;
  }
  if(hit||g_ram[0xc4a+k]!=9) {
   read_weapon(&t->weapons[k],g_ram,k);t->arrow_results|=(uint16_t)(1u<<k);
  }
  g_ram[0xc4a+k]=0;
 }
 wr16(0xe2,ax);wr16(0xe8,ay);wr16(0xe0,ax);wr16(0xe6,ay);
 wr16(0x11e,ax);wr16(0x120,ax);wr16(0x122,ay);wr16(0x124,ay);
 for(unsigned k=0;k<10;k++)if(bomb_damage_mask&(1u<<k)) {
  const uint8_t*p=bomb_damage_image[k];
  memcpy(g_ram+0x280,p+0x280,0x32);memcpy(g_ram+0x380,p+0x380,0x70);
  memcpy(g_ram+0xbf0,p+0xbf0,0xaa);g_ram[0xfa0]=(uint8_t)k;
  if(!call(0x088287,0x30,0,k,0))return false;
 }
 memset(g_ram+0xc4a,0,10);
 if(!call(0x00841e,0x30,0,0,0))return false;
 if(!call(0x068328,0x30,0,0,0))return false;
 collect_pieces(r,sim_scroll_x,sim_scroll_y);
 for(unsigned i=0;i<sizeof combat_effects/sizeof *combat_effects;i++)t->effect[i]=g_ram[combat_effects[i]];
 memcpy(t->sfx,g_ram+0x12d,3);
 t->reward=sim_reward&&g_ram[0x10]==9&&g_ram[0x11]==0;
 memcpy(t->rewards,g_ram+0xf360,sizeof t->rewards);
 memset(g_ram+0x1ef80,0xff,512);
 if(memcmp(r->tiles,g_ram+0x2000,sizeof r->tiles)){memcpy(r->tiles,g_ram+0x2000,sizeof r->tiles);r->dirty=true;}
 memcpy(t->pool?t->pool->ram:r->ram,g_ram,sizeof r->ram);
 if(t->pool)t->pool->ticks++;else r->ticks++;
 return true;
}
static void tick_pool(unsigned id,WorldActorPool*pool) {
 if(!ensure(id))return;
 Tick t={0};t.id=id;t.pool=pool;t.player_x=rd16(0x22);t.player_y=rd16(0x20);t.frame=g_ram[0x1a];
 memcpy(t.player,g_ram+0x20,sizeof t.player);memcpy(t.progress,g_ram+0xf000,sizeof t.progress);
 memcpy(t.action,g_ram+0x2d0,sizeof t.action);
 t.arrows=arrow_collision_mask&~arrow_resolved_mask;
 for(unsigned k=0;k<5;k++)if(g_ram[0xc4a+k]!=9)t.arrows&=(uint16_t)~(1u<<k);
 uint8_t before[sizeof t.effect],sfx_before[3];
 for(unsigned i=0;i<sizeof combat_effects/sizeof *combat_effects;i++)before[i]=g_ram[combat_effects[i]];
 memcpy(sfx_before,g_ram+0x12d,3);
 /* The scoped machine starts with the live pending SFX, so only a native
  * attack's changed queue entry is published after restoration. */
 memcpy((pool?pool->ram:areas[id]->ram)+0x12d,sfx_before,3);
 sim_area=areas[id];sim_pool=pool;sim_reward=false;
 if(!RtlGuestScopeRun(tick_scope,&t,1000000))failures++;
 else {
  for(unsigned k=0;k<5;k++)if(t.arrow_results&(1u<<k)) {
   write_weapon(g_ram,k,&t.weapons[k]);arrow_targets[k]=t.targets[k];arrow_resolved_mask|=(uint16_t)(1u<<k);
  }
  for(unsigned i=0;i<sizeof combat_effects/sizeof *combat_effects;i++)if(t.effect[i]!=before[i])g_ram[combat_effects[i]]=t.effect[i];
  for(unsigned i=0;i<3;i++)if(t.sfx[i]!=sfx_before[i])g_ram[0x12d+i]=t.sfx[i];
  if(t.reward)for(unsigned i=0;i<sizeof t.rewards;i++)if(t.rewards[i]!=t.progress[0x360+i])g_ram[0xf360+i]=t.rewards[i];
 }
 sim_area=NULL;sim_pool=NULL;
}
static void tick_area(unsigned id,bool base) {
 if(!ensure(id))return;
 if(base)tick_pool(id,NULL);
 for(WorldActorPool*p=areas[id]->pools;p;p=p->next)tick_pool(id,p);
}
bool ZeldaWorldEnabled(void){return enabled;}
void ZeldaWorldEnable(void){enabled=true;}
void ZeldaWorldDisable(void){enabled=false;ZeldaWorldReset();}
void ZeldaWorldReset(void) {
 for(unsigned i=0;i<ZW_AREAS;i++){if(areas[i]){free_pools(areas[i]);free(areas[i]->raster);free(areas[i]);areas[i]=NULL;}}
 active_world=-1;decoded=failures=0;decode_ms=0;
 collision_window=false;last_area=-1;handoffs=0;
 live_projection=live_pieces_valid=false;live_piece_count=0;
 weapon_piece_count=0;weapon_projection=false;
 memset(arrow_targets,0,sizeof arrow_targets);mirrored_slot=-1;
 projected_weapon=-1;projected_arrow=false;
}
static uint16_t *world_tile(int wx,int wy,int world) {
 if(wx<0||wx>=ZW_WORLD||wy<0||wy>=ZW_WORLD)return NULL;
 unsigned id=owner((unsigned)((wy/512)*8+wx/512+world));WorldArea*r=areas[id];
 if(!r||!r->valid)return NULL;
 unsigned x=(unsigned)(wx-(int)(id&7)*512)/16,y=(unsigned)(wy-(int)((id&63)>>3)*512)/16;
 return &r->tiles[y*64+x];
}
static void commit_collision(void) {
 if(!collision_window)return;
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++) {
  unsigned i=y*64+x;uint16_t now=rd16(0x2000+i*2);
  if(now==collision_before[i])continue;
  int wx=collision_x+(int)x*16,wy=collision_y+(int)y*16;
  uint16_t*p=world_tile(wx,wy,collision_world);
  if(p){*p=now;areas[owner((unsigned)(wy/512*8+wx/512+collision_world))]->dirty=true;}
 }
}
bool ZeldaWorldFrameBegin(uint32_t *inputs) {
 replay_boundary(inputs);
 bomb_damage_mask=0;
 arrow_collision_mask=arrow_resolved_mask=0;
 for(unsigned k=0;k<5;k++)if(g_ram[0xc4a+k]!=10)memset(&arrow_targets[k],0,sizeof arrow_targets[k]);
 collision_window=false;
 if(!playable()||active_world<0){live_pieces_valid=false;live_piece_count=0;return true;}
 wr16(0x22,ZeldaWorldLinkCoordinate(rd16(0x22)));wr16(0x20,ZeldaWorldLinkCoordinate(rd16(0x20)));
 collision_x=clamp(((int)rd16(0x22)-256)/512*512,0,3072);
 collision_y=clamp(((int)rd16(0x20)-256)/512*512,0,3072);
 collision_world=active_world;
 for(int y=0;y<2;y++)for(int x=0;x<2;x++)
  if(!ensure((unsigned)((collision_y/512+y)*8+collision_x/512+x+active_world)))return true;
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++) {
  uint16_t*p=world_tile(collision_x+(int)x*16,collision_y+(int)y*16,active_world);
  collision_before[y*64+x]=p?*p:0;wr16(0x2000+(y*64+x)*2,collision_before[y*64+x]);
 }
 wr16(0x708,collision_y);wr16(0x70c,collision_x>>3);wr16(0x70a,0x3f0);wr16(0x70e,0x7e);
 collision_window=true;
 return true;
}
static bool adopt_area(unsigned id,bool crossing) {
 WorldArea*r=areas[id];if(!r||!r->valid)return false;
 NativeActor portal;int portal_slot=-1,destination=-1;
 for(int k=15;k>=0;k--)if(g_ram[0xdd0+k]&&g_ram[0xe20+k]==0x6c){portal_slot=k;break;}
 if(portal_slot>=0) {
  read_actor(&portal,g_ram,(unsigned)portal_slot);
  if(!r->ram[0xdd0+portal_slot])destination=portal_slot;
  else for(int k=15;k>=0;k--)if(!r->ram[0xdd0+k]){destination=k;break;}
  /* Match the original dynamic allocator's expendable guard-probe policy.
   * Never silently overwrite a physical authored actor to retain a portal. */
  if(destination<0)for(int k=15;k>=0;k--)if(r->ram[0xe20+k]==0x41&&r->ram[0xdb0+k]){destination=k;break;}
  if(destination<0)return false;
 }
 /* Only area-owned state crosses this handoff. Player action/stack, all
  * native progress/SRAM, input, audio, clocks and portal history stay live. */
 /* $0BF0..$0C99 is the player's global ancilla pool. Copying the whole
  * sprite slab silently replaced a traveling arrow/bomb with stale data. */
 memcpy(g_ram+0xb00,r->ram+0xb00,0xf0);
 memcpy(g_ram+0xc9a,r->ram+0xc9a,0x366);
 memcpy(g_ram+0x2000,r->tiles,sizeof r->tiles);
 memcpy(g_ram+0x4000,r->ram+0x4000,0x8000);
 memcpy(g_ram+0x1d000,r->ram+0x1d000,0x3000);
 if(destination>=0)write_actor(g_ram,(unsigned)destination,&portal);
 memcpy(g_ram+0xaa0,r->ram+0xaa0,9);
 memcpy(g_ram+0xc2f8,r->ram+0xc2f8,8);
 memcpy(g_ram+0x600,r->ram+0x600,0x18);
 memcpy(g_ram+0x708,r->ram+0x708,0x18);
 wr16(0x8a,id);wr16(0x40a,id);
 wr16(0x700,((rd16(0x20)&0x1e00)>>5)|((rd16(0x22)&0x1e00)>>8));
 wr16(0x410,0);wr16(0x416,0);g_ram[0x11]=g_ram[0xb0]=g_ram[0x126]=0;
 g_ram[0x95]=g_ram[0xc011]=g_ram[0xc00b]=0;
 int ax=(id&7)*512,ay=((id&63)>>3)*512;
 int sx=clamp((int)rd16(0x22)-128,ax,ax+(int)r->size-256);
 int sy=clamp((int)rd16(0x20)-112,ay,ay+(int)r->size-224);
 wr16(0xe0,sx);wr16(0xe2,sx);wr16(0xe6,sy);wr16(0xe8,sy);
 wr16(0x618,sy+114);wr16(0x61a,sy+112);wr16(0x61c,sx+130);wr16(0x61e,sx+128);
 memset(g_ram+0x624,0,8);
 memcpy(g_ppu->vram+0x2000,r->vram+0x2000,0x4000);
 memcpy(g_ppu->vram+0x5000,r->vram+0x5000,0x2000);
 for(unsigned i=32;i<240;i++) {
  if(i>=0xd0&&i<0xe0)continue;
  g_ppu->cgram[i]=r->palette[i];wr16(0xc300+i*2,r->palette[i]);wr16(0xc500+i*2,r->palette[i]);
 }
 g_ram[0x15]=1;
 if(crossing)handoffs++;return true;
}
void ZeldaWorldFrameEnd(void) {
 ZeldaWorldSpritesEnd();
 if(held_transition_velocity){g_ram[0x30]=held_vy;g_ram[0x31]=held_vx;held_transition_velocity=false;}
 /* Module09 draws Link AFTER Sprite_Main, using the restored native camera.
  * The native OAM prefix contains global actors/weapons plus that final Link
  * draw; the per-area lists contain the independently projected enemies. */
 if(playable()&&live_pieces_valid&&areas[owner(rd16(0x8a))]) {
  live_piece_count=0;
  collect_oam(areas[owner(rd16(0x8a))],live_pieces,&live_piece_count,rd16(0xe2),rd16(0xe8));
 }
 if(playable())for(unsigned k=0;k<10;k++) {
  unsigned type=g_ram[0xc4a+k];
  if((type==1||type==2||type==5||type==7||type==9||type==11||type==31)&&
    ((g_ram[0xc04+k]|g_ram[0xc18+k]<<8)>=ZW_WORLD||(g_ram[0xbfa+k]|g_ram[0xc0e + k]<<8)>=ZW_WORLD))g_ram[0xc4a+k]=0;
 }
 /* Native travel owns its destination and may have already repurposed the
  * tile buffer. Only an uninterrupted overworld frame can commit our window
  * or choose a geographic handoff. Menus/travel resume through capture. */
 bool continuous=playable()&&last_area>=0&&owner(rd16(0x8a))==(unsigned)last_area&&
   (rd16(0x8a)&64)==active_world;
 if(continuous)commit_collision();
 native_oam_x=rd16(0xe2);native_oam_y=rd16(0xe8);
 if(continuous) {
  int px=ZeldaWorldLinkCoordinate(rd16(0x22)),py=ZeldaWorldLinkCoordinate(rd16(0x20));
  wr16(0x22,px);wr16(0x20,py);
  unsigned geographic=owner(ZeldaWorldCell(px+8,py+16,(unsigned)active_world));
  if(geographic!=(unsigned)last_area) {
   if(areas[last_area])capture((unsigned)last_area);
   collision_window=false;
   if(!adopt_area(geographic,true))failures++;
  }
 }
 if(!playable()){collision_window=false;last_area=-1;service_commands();return;}
 unsigned id=owner(rd16(0x8a));active_world=id&64;
 if(!areas[id]){areas[id]=calloc(1,sizeof(WorldArea));if(!areas[id])return;areas[id]->size=area_size(id);}
 if(!continuous)collision_window=false;
 if(areas[id]->populated)memset(g_ram+0x1ef80,0xff,512);
 capture(id);
 if(!areas[id]->populated) {
  PoolReserve reserve;Decode d={id,areas[id],&reserve};free_pools(areas[id]);
  bool ok=prepare_reserve(&reserve);
  if(ok)ok=RtlGuestScopeRun(populate_scope,&d,2000000);
  release_reserve(&reserve);
  if(ok) {
   /* Publish the populated base pool while preserving global ancillas. */
   memcpy(g_ram+0xb00,areas[id]->ram+0xb00,0xf0);
   memcpy(g_ram+0xc9a,areas[id]->ram+0xc9a,0x366);
   memcpy(g_ram+0x1d000,areas[id]->ram+0x1d000,0x3000);
   remove_cached_portals(areas[id]->ram);
  }else {free_pools(areas[id]);areas[id]->populated=false;failures++;}
 }
 collision_window=false;last_area=(int)id;
 uint64_t before=SDL_GetPerformanceCounter();
 int w=clamp(drawable_w/zoom,128,ZW_WORLD),h=clamp(drawable_h/zoom-ZW_HUD,64,ZW_WORLD);
 int x=(int)rd16(0x22),y=(int)rd16(0x20);
 for(unsigned next=(unsigned)active_world;next<(unsigned)active_world+64;next++) {
  if(owner(next)!=next)continue;
  int ax=(next&7)*512,ay=((next&63)>>3)*512,size=(int)area_size(next);
  if(atlas||(ax<x+w/2+512&&ax+size>x-w/2-512&&ay<y+h/2+512&&ay+size>y-h/2-512))tick_area(next,next!=id);
 }
 simulation_ms=(SDL_GetPerformanceCounter()-before)*1000.0/SDL_GetPerformanceFrequency();
 service_commands();
}
bool ZeldaWorldEvent(const SDL_Event *event) {
 if(!enabled)return false;
 if(event->type==SDL_MOUSEWHEEL&&event->wheel.y!=0&&!(SDL_GetModState()&KMOD_CTRL)) {
  zoom=clamp(zoom+(event->wheel.y<0?1:-1),1,8);return true;
 }
 if((event->type==SDL_KEYDOWN||event->type==SDL_KEYUP)&&SNESRECOMP_SDL_EVENT_KEY(*event)==SDLK_F4&&
   !(SNESRECOMP_SDL_EVENT_MOD(*event)&(KMOD_CTRL|KMOD_SHIFT|KMOD_ALT|KMOD_GUI))) {
  if(event->type==SDL_KEYDOWN&&!event->key.repeat)atlas=!atlas;
  return true; /* F4 must not also load the stock fourth savestate slot. */
 }
 return false;
}
static void draw_piece(const WorldArea*r,const WorldPiece*p,int w,int h) {
 unsigned base=(r->obsel&7)<<13;
 if(p->flags&1)base+=((r->obsel&0x18)+8)<<9;
 for(unsigned y=0;y<p->size;y++)for(unsigned x=0;x<p->size;x++) {
  int ox=p->x+(int)x-camera_x,oy=p->y+(int)y-camera_y+ZW_HUD;
  if(ox<0||ox>=w||oy<ZW_HUD||oy>=h)continue;
  unsigned u=(p->flags&64)?p->size-1-x:x,v=(p->flags&128)?p->size-1-y:y;
  unsigned tile=(((p->tile>>4)+(v>>3))<<4)|(((p->tile&15)+(u>>3))&15);
  unsigned adr=(base+tile*16+(v&7))&0x7fff,s=7-(u&7),a=r->vram[adr],b=r->vram[(adr+8)&0x7fff];
  unsigned pixel=((a>>s)&1)|(((a>>(s+8))&1)<<1)|(((b>>s)&1)<<2)|(((b>>(s+8))&1)<<3);
  if(pixel)canvas[(size_t)oy*w+ox]=color(r->palette[128+((p->flags>>1)&7)*16+pixel]);
 }
}
const uint32_t *ZeldaWorldPresent(const uint32_t *native,int native_pitch,
    int dw,int dh,int *width,int *height) {
 if(!enabled||active_world<0||g_ram[0x10]!=9)return NULL;
 drawable_w=dw;drawable_h=dh;
 int w=atlas?ZW_WORLD:clamp(dw/zoom,128,ZW_WORLD),h=atlas?ZW_WORLD+ZW_HUD:clamp(dh/zoom,96,ZW_WORLD+ZW_HUD);
 size_t count=(size_t)w*h;
 if(count>canvas_capacity){uint32_t*p=realloc(canvas,count*4);if(!p)return NULL;canvas=p;canvas_capacity=count;}
 uint64_t before=SDL_GetPerformanceCounter();
 camera_x=atlas?0:clamp((int)rd16(0x22)-w/2,0,ZW_WORLD-w);
 camera_y=atlas?0:clamp((int)rd16(0x20)-(h-ZW_HUD)/2,0,ZW_WORLD-(h-ZW_HUD));
 memset(canvas,0,count*4);
 for(unsigned id=(unsigned)active_world;id<(unsigned)active_world+64;id++) {
  if(owner(id)!=id)continue;
  int ax=(id&7)*512,ay=((id&63)>>3)*512,size=(int)area_size(id);
  int l=clamp(ax-camera_x,0,w),t=clamp(ay-camera_y,0,h-ZW_HUD);
  int rr=clamp(ax+size-camera_x,0,w),bb=clamp(ay+size-camera_y,0,h-ZW_HUD);
  if(l>=rr||t>=bb||!ensure(id))continue;
  rasterize(id);WorldArea*r=areas[id];if(!r->raster)continue;
  for(int y=t;y<bb;y++)memcpy(canvas+(size_t)(y+ZW_HUD)*w+l,
    r->raster+(size_t)(y+camera_y-ay)*size+l+camera_x-ax,(size_t)(rr-l)*4);
 }
 unsigned active_id=owner(rd16(0x8a));
 for(unsigned id=(unsigned)active_world;id<(unsigned)active_world+64;id++) {
  WorldArea*r=areas[id];if(!r||!r->valid)continue;
  if(id!=active_id||live_pieces_valid)for(unsigned i=r->piece_count;i--;)draw_piece(r,&r->pieces[i],w,h);
  for(WorldActorPool*p=r->pools;p;p=p->next)for(unsigned i=p->piece_count;i--;)draw_piece(r,&p->pieces[i],w,h);
 }
 /* Native OAM supplies Link, followers, local actors and items; world-space
  * projection removes the native scene rectangle and its 9-bit view limit. */
 WorldArea*r=areas[active_id];
 if(r&&live_pieces_valid)for(unsigned i=weapon_piece_count;i--;)draw_piece(r,&weapon_pieces[i],w,h);
 if(r&&live_pieces_valid)for(unsigned i=live_piece_count;i--;)draw_piece(r,&live_pieces[i],w,h);
 else if(r)for(int i=127;i>=0;i--) {
  unsigned off=0x800+(unsigned)i*4,xy=g_ram[off]|g_ram[off+1]<<8,cf=g_ram[off+2]|g_ram[off+3]<<8;
  unsigned ext=g_ram[0xa20+i]&3;
  unsigned y=xy>>8;if(y>=224)continue;
  int x=(xy&255)|((ext&1)<<8);if(x>=256)x-=512;
  WorldPiece p={(int16_t)(native_oam_x+x),(int16_t)(native_oam_y+y),
   (uint16_t)(cf&255),(uint8_t)(cf>>8),(uint8_t)((ext&2)?16:8)};
  draw_piece(r,&p,w,h);
 }
 int hx=(w-256)/2;
 for(int y=0;y<ZW_HUD&&y<h;y++)for(int x=0;x<256;x++)if(hx+x>=0&&hx+x<w)
   canvas[(size_t)y*w+hx+x]=native[(size_t)y*native_pitch+x]|0xff000000u;
 picture_width=w;picture_height=h;*width=w;*height=h;
 render_ms=(SDL_GetPerformanceCounter()-before)*1000.0/SDL_GetPerformanceFrequency();
 return canvas;
}
static bool isolation_test(void *opaque) {
 (void)opaque;g_ram[0x5432]^=0xff;g_ppu->vram[123]^=0xffff;
 g_cpu.A^=0x5678;g_cpu.master_cycles+=12345;WriteReg(0x2140,0x42);
 WriteReg(0x2105,0x1f);
 return true;
}
static bool isolation_abort(void *opaque) {
 (void)opaque;g_ram[0x5432]^=0xff;
 cpu_interrupt_context_enter();
 for(;;)RtlGuestScopeCheckpoint();
}
static bool floor_at(int wx,int wy,int world) {
 uint16_t*p=world_tile(wx,wy,world);if(!p||*p>=3752)return false;
 unsigned q=((unsigned)(wy&8)>>2)|((unsigned)(wx&8)>>3);
 const uint8_t*map8=RomPtr(0x0f8000+*p*8+q*2);
 unsigned tile=(map8[0]|map8[1]<<8)&511;
 return RomPtr(0x0e9459)[tile]==0;
}
void ZeldaWorldAttachWindow(SDL_Window*window) {world_window=window;}
static void ui_key(SDL_Keycode key) {
 SDL_Event e={0};e.type=SDL_KEYDOWN;
#if defined(SNESRECOMP_SDL3)
 e.key.key=key;
#else
 e.key.keysym.sym=key;
#endif
 SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);
}
bool ZeldaWorldPollUI(void) {
 if(ui_resume_deadline&&SDL_GetPerformanceCounter()>=ui_resume_deadline) {
  ui_resume_deadline=0;fprintf(stderr,"[world-ui] scheduled host resume\n");return true;
 }
 return false;
}
static bool prepare_edge(unsigned id,const char*dir,unsigned*next,int*px,int*py) {
 id=owner(id);if(!ensure(id))return false;
 int ax=(id&7)*512,ay=((id&63)>>3)*512,size=(int)area_size(id),dx=0,dy=0;
 if(!strcmp(dir,"east"))dx=1;else if(!strcmp(dir,"west"))dx=-1;
 else if(!strcmp(dir,"south"))dy=1;else if(!strcmp(dir,"north"))dy=-1;else return false;
 for(int along=48;along<size-48;along+=16) {
  int ex=dx?(dx>0?ax+size:ax):ax+along,ey=dy?(dy>0?ay+size:ay):ay+along;
  int tx=ex+(dx<0?-1:0),ty=ey+(dy<0?-1:0);
  if(tx<0||tx>=4096||ty<0||ty>=4096)continue;
  unsigned dest=owner((unsigned)(ty/512*8+tx/512+(id&64)));
  if(dest==id||!ensure(dest))continue;
  bool good=true;
  for(int a=-40;a<=40&&good;a+=8)for(int b=-8;b<=8;b+=8)
    if(!floor_at(ex+(dx?a:b),ey+(dy?a:b),id&64)){good=false;break;}
  if(!good)continue;
  int x=dx?ex-dx*36-8:ex-8,y=dy?ey-dy*36-16:ey-16;
  wr16(0x22,x);wr16(0x20,y);wr16(0x49,0);g_ram[0x2e4]=0;
  /* The initial disposable save is the prologue. Positioning elsewhere must
   * also leave its AsleepInBed handler (22); this is an explicit QA assist. */
  g_ram[0x5d]=0;g_ram[0x37b]=0;
  if(!adopt_area(id,false))return false;
  last_area=(int)id;active_world=id&64;collision_window=false;
  *next=dest;*px=x;*py=y;return true;
 }
 return false;
}
static int command(const char *cmd,const char *args,DebugServerGameSendLine send) {
 char line[4096];
 if(!strcmp(cmd,"world")) {
  unsigned actors=0;uint64_t ticks=0;
  for(unsigned i=0;i<ZW_AREAS;i++)if(areas[i]&&areas[i]->valid){
   ticks+=areas[i]->ticks;for(unsigned k=0;k<16;k++)actors+=areas[i]->ram[0xdd0+k]!=0;
   for(WorldActorPool*p=areas[i]->pools;p;p=p->next){ticks+=p->ticks;for(unsigned k=0;k<16;k++)actors+=p->ram[0xdd0+k]!=0;}
  }
  snprintf(line,sizeof line,"{\"enabled\":%s,\"atlas\":%s,\"zoom\":%d,\"decoded\":%llu,\"failures\":%llu,\"decode_ms\":%.3f,\"simulation_ms\":%.3f,\"render_ms\":%.3f,\"actors\":%u,\"neighbor_ticks\":%llu,\"actor_hooks\":%llu,\"width\":%d,\"height\":%d,\"camera\":[%d,%d],\"module\":%u,\"sub\":%u,\"room\":%u,\"area\":%u,\"x\":%u,\"y\":%u}",enabled?"true":"false",atlas?"true":"false",zoom,(unsigned long long)decoded,(unsigned long long)failures,decode_ms,simulation_ms,render_ms,actors,(unsigned long long)ticks,(unsigned long long)actor_hooks,picture_width,picture_height,camera_x,camera_y,g_ram[0x10],g_ram[0x11],rd16(0xa0),rd16(0x8a),rd16(0x22),rd16(0x20));
  size_t end=strlen(line);snprintf(line+end-1,sizeof line-end+1,",\"handoffs\":%llu}",(unsigned long long)handoffs);
 }else if(!strcmp(cmd,"world_ui")) {
  /* Process-local SDL events exercise the actual input/presentation loop.
   * This never synthesizes desktop input or changes another window. */
  int w,h;
  if(!strcmp(args,"f4"))ui_key(SDLK_F4);
  else if(!strcmp(args,"gpu-capture")&&g_config.output_method==kOutputMethod_OpenGL)
   OpenGLRenderer_RequestScreenshot();
  else if(!strcmp(args,"wheel-up")||!strcmp(args,"wheel-down")) {
   SDL_Event e={0};e.type=SDL_MOUSEWHEEL;e.wheel.y=!strcmp(args,"wheel-up")?1:-1;SDL_PushEvent(&e);
  }else if(sscanf(args,"pause-resize %d %d",&w,&h)==2&&world_window&&w>=240&&h>=240&&w<=4096&&h<=4096) {
   ui_resume_deadline=SDL_GetPerformanceCounter()+SDL_GetPerformanceFrequency()*5;
   SDL_SetWindowSize(world_window,w,h);ui_key(SDLK_p);
  }else if(sscanf(args,"resize %d %d",&w,&h)==2&&world_window&&w>=240&&h>=240&&w<=4096&&h<=4096) {
   SDL_SetWindowSize(world_window,w,h);
  }else {send("{\"error\":\"use f4, wheel-up, wheel-down or pause-resize W H (resumes after 5s)\"}");return 1;}
  snprintf(line,sizeof line,"{\"ok\":true,\"process_local_ui_event\":true}");
 }else if(!strcmp(cmd,"world_edge")) {
  unsigned id=0,next=0;int x=0,y=0;char dir[16];
  bool ok=sscanf(args,"%u %15s",&id,dir)==2&&id<128&&playable()&&prepare_edge(id,dir,&next,&x,&y);
  snprintf(line,sizeof line,"{\"ok\":%s,\"from\":%u,\"to\":%u,\"x\":%d,\"y\":%d,\"assisted_position_only\":true}",ok?"true":"false",id,next,x,y);
 }else if(!strcmp(cmd,"world_area")) {
  unsigned id=0,page=0;if(sscanf(args,"%u %u",&id,&page)<1||id>=128||!areas[owner(id)]){send("{\"error\":\"area not initialized\"}");return 1;}
  WorldArea*r=areas[owner(id)];unsigned authored=0,loaded=0;
  for(unsigned i=0;i<4096;i++)if(r->ram[0x1df80+i]){authored++;loaded+=(r->ram[0x1ef80+(i>>3)]&(0x80>>(i&7)))!=0;}
  WorldActorPool*pool=r->pools;for(unsigned k=1;k<page&&pool;k++)pool=pool->next;
  if(page&&!pool){send("{\"error\":\"actor pool not initialized\"}");return 1;}
  uint8_t*ar=page?pool->ram:r->ram;uint64_t ticks=page?pool->ticks:r->ticks;
  int n=snprintf(line,sizeof line,"{\"id\":%u,\"pool\":%u,\"pools\":%u,\"ticks\":%llu,\"authored_spawns\":%u,\"loaded_spawns\":%u,\"actors\":[",owner(id),page,1+pool_count(r),(unsigned long long)ticks,authored,loaded);bool first=true;
  for(unsigned k=0;k<16;k++)if(ar[0xdd0+k]) {
   n+=snprintf(line+n,sizeof line-(size_t)n,"%s{\"slot\":%u,\"type\":%u,\"state\":%u,\"x\":%u,\"y\":%u,\"timer\":%u,\"health\":%u,\"hit_timer\":%u,\"pending_damage\":%u,\"C\":%u,\"direction\":%u}",first?"":",",k,ar[0xe20+k],ar[0xdd0+k],ar[0xd10+k]|ar[0xd30+k]<<8,ar[0xd00+k]|ar[0xd20+k]<<8,ar[0xdf0+k],ar[0xe50+k],ar[0xef0+k],ar[0xce2+k],ar[0xdb0+k],ar[0xde0+k]);first=false;
  }
  snprintf(line+n,sizeof line-(size_t)n,"]}");
 }else if(!strcmp(cmd,"world_qa_position")) {
  int x,y;
  if(sscanf(args,"%d %d",&x,&y)!=2||!playable()||x < -1||y < -1||x>4081||y>4081) {
   send("{\"error\":\"use overworld coordinates -1..4081 for an assisted edge setup\"}");return 1;
  }
  unsigned id=owner(ZeldaWorldCell(x,y,(unsigned)active_world));
  if(!ensure(id)){send("{\"error\":\"could not initialize target area\"}");return 1;}
  wr16(0x22,(uint16_t)x);wr16(0x20,(uint16_t)y);g_ram[0x5d]=g_ram[0x30]=g_ram[0x31]=0;
  if(!adopt_area(id,false)){send("{\"error\":\"could not adopt target area\"}");return 1;}
  last_area=(int)id;collision_window=false;
  snprintf(line,sizeof line,"{\"ok\":true,\"assisted_position_only\":true,\"area\":%u}",id);
 }else if(!strcmp(cmd,"world_qa_equipment")) {
  /* Disposable encounter setup only; no actor damage/death or reward flag. */
  g_ram[0xf359]=1;g_ram[0xf35a]=1;g_ram[0xf36c]=g_ram[0xf36d]=0xa0;
  g_ram[0x5d]=g_ram[0x31f]=g_ram[0x4d]=g_ram[0x46]=g_ram[0x373]=0;
  g_ram[0x37b]=!strcmp(args,"protected");
  snprintf(line,sizeof line,"{\"ok\":true,\"assisted_equipment\":true,\"protect_player\":%s}",g_ram[0x37b]?"true":"false");
 }else if(!strcmp(cmd,"world_qa_loose_pickup")) {
  unsigned id,k;
  if(sscanf(args,"%u %u",&id,&k)!=2||id>=128||k>=16||!areas[owner(id)]||
    areas[owner(id)]->ram[0xe20+k]<0xd8||areas[owner(id)]->ram[0xe20+k]>=0xe5) {
   send("{\"error\":\"use an existing ordinary native pickup\"}");return 1;
  }
  areas[owner(id)]->ram[0xe90+k]=0;
  snprintf(line,sizeof line,"{\"ok\":true,\"bypassed_hidden_pickup_prerequisite\":true}");
 }else if(!strcmp(cmd,"world_qa_item")) {
  unsigned item=!strcmp(args,"bombs")?1:!strcmp(args,"boomerang")?2:!strcmp(args,"bow")?3:!strcmp(args,"hookshot")?10:!strcmp(args,"mirror")?20:0;
  if(!item){send("{\"error\":\"use bombs, boomerang, bow, hookshot or mirror\"}");return 1;}
  g_ram[0x303]=g_ram[0x304]=(uint8_t)item;
  g_ram[0xf340]=2;g_ram[0xf341]=g_ram[0xf342]=1;g_ram[0xf343]=10;g_ram[0xf377]=50;
  if(item==20)g_ram[0xf353]=2;
  snprintf(line,sizeof line,"{\"ok\":true,\"assisted_inventory_selection\":%u}",item);
 }else if(!strcmp(cmd,"world_place_actor")) {
  unsigned id,k,x,y,dir=4,page=0;
  if(sscanf(args,"%u %u %u %u %u %u",&id,&k,&x,&y,&dir,&page)<4||id>=128||k>=16||x>=4096||y>=4096||!ensure(id)) {
   send("{\"error\":\"need an initialized native actor and valid world coordinates\"}");return 1;
  }
  uint8_t*p=areas[owner(id)]->ram;
  if(page) {
   WorldActorPool*pool=areas[owner(id)]->pools;for(unsigned i=1;i<page&&pool;i++)pool=pool->next;
   if(!pool){send("{\"error\":\"actor pool not initialized\"}");return 1;}p=pool->ram;
  }
  if(!p[0xdd0+k]){send("{\"error\":\"actor slot is empty\"}");return 1;}
  p[0xd10+k]=(uint8_t)x;p[0xd30+k]=(uint8_t)(x>>8);p[0xd00+k]=(uint8_t)y;p[0xd20+k]=(uint8_t)(y>>8);
  if(dir<4)p[0xde0+k]=p[0xeb0+k]=(uint8_t)dir;
  snprintf(line,sizeof line,"{\"ok\":true,\"pool\":%u,\"assisted_position\":true,\"assisted_facing\":%s}",page,dir<4?"true":"false");
 }else if(!strcmp(cmd,"world_player")) {
  int n=snprintf(line,sizeof line,"{\"health\":%u,\"pending_damage\":%u,\"invulnerability\":%u,\"recoil_timer\":%u,\"aux_state\":%u,\"sword\":%u,\"sword_frame\":%u,\"bombs\":%u,\"bomb_filler\":%u,\"arrows\":%u,\"rupees\":%u,\"rupee_goal\":%u,\"ancillas\":[",g_ram[0xf36d],g_ram[0x373],g_ram[0x31f],g_ram[0x46],g_ram[0x4d],g_ram[0xf359],g_ram[0x3c],g_ram[0xf343],g_ram[0xf375],g_ram[0xf377],rd16(0xf362),rd16(0xf360));
  bool first=true;for(unsigned k=0;k<10;k++)if(g_ram[0xc4a+k]) {
   const ArrowTarget*t=k<5?&arrow_targets[k]:NULL;
   n+=snprintf(line+n,sizeof line-(size_t)n,"%s{\"slot\":%u,\"type\":%u,\"x\":%u,\"y\":%u,\"timer\":%u,\"stage\":%u,\"fuse\":%u,\"target_valid\":%s,\"target\":[%u,%u,%u]}",first?"":",",k,g_ram[0xc4a+k],g_ram[0xc04+k]|((unsigned)g_ram[0xc18+k]<<8),g_ram[0xbfa+k]|((unsigned)g_ram[0xc0e + k]<<8),g_ram[0xc68+k],g_ram[0xc5e + k],g_ram[0x39f+k],t&&t->valid?"true":"false",t?t->area:0,t?t->pool:0,t?t->slot:0);first=false;
  }n+=snprintf(line+n,sizeof line-(size_t)n,"],\"weapon_pieces\":%u,\"player_layer\":%u,\"player_handler\":%u,\"player_bits\":%u,\"player_pose\":%u,\"portals\":[",weapon_piece_count,g_ram[0xee],g_ram[0x5d],g_ram[0x3a],g_ram[0x2e9]);
  first=true;for(unsigned k=0;k<16;k++)if(g_ram[0xdd0+k]&&g_ram[0xe20+k]==0x6c) {
   n+=snprintf(line+n,sizeof line-(size_t)n,"%s{\"slot\":%u,\"x\":%u,\"y\":%u,\"A\":%u,\"B\":%u,\"layer\":%u,\"state\":%u,\"flags2\":%u}",first?"":",",k,g_ram[0xd10+k]|g_ram[0xd30+k]<<8,g_ram[0xd00+k]|g_ram[0xd20+k]<<8,g_ram[0xd90+k],g_ram[0xda0+k],g_ram[0xf20+k],g_ram[0xdd0+k],g_ram[0xe40+k]);first=false;
  }snprintf(line+n,sizeof line-(size_t)n,"]}");
 }else if(!strcmp(cmd,"world_view")) {
  atlas=!strcmp(args,"atlas");if(!strcmp(args,"off"))enabled=false;else enabled=true;
  snprintf(line,sizeof line,"{\"ok\":true}");
 }else if(!strcmp(cmd,"world_replay")) {
  int n=60;unsigned mask=0;sscanf(args,"%d %x",&n,&mask);
  if(n<1||n>600||mask>0xfff||replay.phase==1||replay.phase==2||replay.phase==3){send("{\"error\":\"replay needs 1..600 frames, a 12-bit input mask and an idle probe\"}");return 1;}
  memset(&replay,0,sizeof replay);replay.phase=1;replay.n=n;replay.inputs=mask;replay.first_bad=-1;
  snprintf(line,sizeof line,"{\"ok\":true,\"frames\":%d,\"input_mask\":%u}",n,mask);
 }else if(!strcmp(cmd,"world_replay_status")) {
  snprintf(line,sizeof line,"{\"phase\":%d,\"frames\":%d,\"index\":%d,\"first_mismatch_frame\":%d,\"final_equal\":%s,\"first_diff\":%zu,\"expected\":{\"all\":\"%016llx\",\"core\":\"%016llx\",\"world\":\"%016llx\"},\"actual\":{\"all\":\"%016llx\",\"core\":\"%016llx\",\"world\":\"%016llx\"}}",replay.phase,replay.n,replay.index,replay.first_bad,replay.final_equal?"true":"false",replay.first_diff,(unsigned long long)replay.expected.all,(unsigned long long)replay.expected.core,(unsigned long long)replay.expected.world,(unsigned long long)replay.actual.all,(unsigned long long)replay.actual.core,(unsigned long long)replay.actual.world);
 }else if(!strcmp(cmd,"world_qa_overworld")) {
  /* Assisted setup using the original dungeon exit loader, not a claim of
   * walking through that exit. Main-thread transaction avoids torn loader
   * variables from background write_ram commands. Eastern Palace room C9. */
  unsigned room=0xc9;if(args&&*args)sscanf(args,"%u",&room);
  if(room>=0x100){send("{\"error\":\"use an audited native exit room below 256\"}");return 1;}
  const uint8_t*exit_rooms=RomPtr(0x02dd8a),*exit_screens=RomPtr(0x02de28);int exit=-1;
  for(int k=78;k>=0;k--)if((exit_rooms[k*2]|exit_rooms[k*2+1]<<8)==room){exit=k;break;}
  if(exit<0){send("{\"error\":\"room has no native exit-table entry\"}");return 1;}
  wr16(0xa0,room);g_ram[0xf3c5]=3;g_ram[0xf36c]=g_ram[0xf36d]=0xa0;
  g_ram[0xf3ca]=exit_screens[exit]&64;
  if(g_ram[0xf3ca])g_ram[0xf357]=1;
  g_ram[0x2e4]=g_ram[0xfc1]=g_ram[0x5d]=0;
  g_ram[0x10]=8;g_ram[0x11]=g_ram[0xb0]=0;
  snprintf(line,sizeof line,"{\"ok\":true,\"assisted_exit_loader\":%u,\"assisted_world_setup\":true,\"native_area\":%u}",room,exit_screens[exit]);
 }else if(!strcmp(cmd,"world_decode")) {
  size_t cap=RtlRollbackSnapshotBound();uint8_t*a=malloc(cap),*b=malloc(cap);
  size_t n=a?RtlNetplaySnapshotToMemory(a,cap):0;
  unsigned id=0;bool ok=sscanf(args,"%u",&id)==1&&id<128&&ensure(id);
  size_t m=b?RtlNetplaySnapshotToMemory(b,cap):0;
  /* Decoding intentionally adds a world record. Compare the core guest,
   * excluding the title's growing cache and snapshot-guard prefix. */
  size_t core=RtlSnapshotGuestSize(NULL),prefix=enabled?48:0;
  bool exact=n>=prefix+core&&m>=prefix+core&&core&&!memcmp(a+prefix,b+prefix,core);size_t diff=0;
  if(n>=prefix+core&&m>=prefix+core)for(;diff<core&&a[prefix+diff]==b[prefix+diff];diff++);
  free(a);free(b);
  snprintf(line,sizeof line,"{\"ok\":%s,\"id\":%u,\"restore_exact\":%s,\"first_diff\":%zu,\"last_call\":%u}",ok?"true":"false",id,exact?"true":"false",diff,last_call);
 }else if(!strcmp(cmd,"world_isolation")) {
  size_t cap=RtlRollbackSnapshotBound();uint8_t*a=malloc(cap),*b=malloc(cap);bool ok=false,aborted=false;
  if(a&&b){size_t n=RtlNetplaySnapshotToMemory(a,cap);uint64_t gen=RtlStateGeneration();
   bool ran=RtlGuestScopeRun(isolation_test,NULL,1000);size_t m=RtlNetplaySnapshotToMemory(b,cap);
   ok=ran&&n&&n==m&&!memcmp(a,b,n)&&gen==RtlStateGeneration();
   bool ran_abort=RtlGuestScopeRun(isolation_abort,NULL,10);m=RtlNetplaySnapshotToMemory(b,cap);
   aborted=!ran_abort&&n&&n==m&&!memcmp(a,b,n)&&gen==RtlStateGeneration();}
  free(a);free(b);snprintf(line,sizeof line,"{\"restore_exact\":%s,\"abort_exact\":%s,\"idle\":%s,\"depth\":%d,\"interrupt\":%d}",ok?"true":"false",aborted?"true":"false",interp_bridge_guest_scope_idle()?"true":"false",g_recomp_stack_top,cpu_interrupt_context_active());
 }else return 0;
 send(line);return 1;
}
static void save_reply(const char *line) {
 snprintf(command_reply,sizeof command_reply,"%s",line);
}
static void service_commands(void) {
 if(!command_mutex)return;
 SDL_LockMutex(command_mutex);
 if(command_pending) {
  if(!command(pending_command,pending_args,save_reply))
    save_reply("{\"error\":\"unknown world command\"}");
  command_pending=false;command_ready=true;
 }
 SDL_UnlockMutex(command_mutex);
}
static int queued_command(const char *cmd,const char *args,DebugServerGameSendLine send) {
 if(strcmp(cmd,"world")&&strcmp(cmd,"world_area")&&strcmp(cmd,"world_edge")&&strcmp(cmd,"world_view")&&strcmp(cmd,"world_decode")&&strcmp(cmd,"world_isolation")&&strcmp(cmd,"world_qa_overworld")&&strcmp(cmd,"world_replay")&&strcmp(cmd,"world_replay_status")&&strcmp(cmd,"world_player")&&strcmp(cmd,"world_place_actor")&&strcmp(cmd,"world_qa_equipment")&&strcmp(cmd,"world_qa_item")&&strcmp(cmd,"world_qa_loose_pickup")&&strcmp(cmd,"world_qa_position")&&strcmp(cmd,"world_ui"))return 0;
 if(!command_mutex){send("{\"error\":\"world command queue unavailable\"}");return 1;}
 SDL_LockMutex(command_mutex);
 if(command_pending){SDL_UnlockMutex(command_mutex);send("{\"error\":\"world command busy\"}");return 1;}
 snprintf(pending_command,sizeof pending_command,"%s",cmd);
 snprintf(pending_args,sizeof pending_args,"%s",args?args:"");
 command_ready=false;command_pending=true;
 SDL_UnlockMutex(command_mutex);
 uint64_t start=SDL_GetPerformanceCounter();
 while((SDL_GetPerformanceCounter()-start)*1000/SDL_GetPerformanceFrequency()<5000) {
  char reply[4096];bool ready;
  SDL_LockMutex(command_mutex);ready=command_ready;
  if(ready)snprintf(reply,sizeof reply,"%s",command_reply);
  SDL_UnlockMutex(command_mutex);
  if(ready){send(reply);return 1;}
  SDL_Delay(1);
 }
 send("{\"error\":\"world command pending; game thread did not reach its frame boundary\"}");return 1;
}

/* Current-build, pointer-free world records. Derived terrain rasters are
 * rebuilt, while native OAM pieces are retained so a restored frame need not
 * advance actor AI merely to obtain its picture. No older schema migration. */
typedef struct WorldStateHeader {
 uint32_t magic,schema,count,record_bytes;
 int32_t world,last,zoom,atlas,camera_x,camera_y,oam_x,oam_y;
 uint64_t handoffs;
 uint32_t global_count,global_valid;
 WorldPiece global_pieces[1024];
 uint32_t weapon_count,weapon_reserved;
 WorldPiece weapon_pieces[1024];
 ArrowTarget arrow_targets[5];
} WorldStateHeader;
typedef struct WorldRecordHeader {
 uint32_t id,pieces,obsel,reserved;
 uint64_t ticks;
} WorldRecordHeader;
typedef struct WorldPoolHeader {uint64_t ticks;uint32_t pieces,reserved;} WorldPoolHeader;
enum { WORLD_STATE_MAGIC=0x3157335a };
static WorldArea *pending_areas[ZW_AREAS];
static WorldStateHeader pending_header;
static bool pending_load_ok;
typedef struct StateCount {SaveLoadInfo base;size_t bytes;uint8_t first[12];} StateCount;
static void count_state(SaveLoadInfo*sli,void*data,size_t n) {
 StateCount*c=(StateCount*)sli;
 if(c->bytes<sizeof c->first){size_t k=sizeof c->first-c->bytes;if(k>n)k=n;memcpy(c->first+c->bytes,data,k);}
 c->bytes+=n;
}
static StateCount execution_layout(void) {
 StateCount c={{count_state,NULL},0,{0}};
 RtlApuLock();RtlSaveExecutionState(&c.base);RtlApuUnlock();return c;
}
static size_t record_bytes(void) {
 return sizeof(WorldRecordHeader)+sizeof(((WorldArea*)0)->ram)+sizeof(((WorldArea*)0)->vram)+
  sizeof(((WorldArea*)0)->palette)+sizeof(((WorldArea*)0)->tiles)+sizeof(((WorldArea*)0)->pieces);
}
static size_t pool_bytes(void) {
 return sizeof(WorldPoolHeader)+sizeof(((WorldActorPool*)0)->ram)+sizeof(((WorldActorPool*)0)->pieces);
}
static size_t world_state_bound(void) {
 return enabled?sizeof(WorldStateHeader)+ZW_AREAS*(record_bytes()+ZW_EXTRA_POOLS*pool_bytes())+65536:0;
}
static void clear_pending(void) {
 for(unsigned i=0;i<ZW_AREAS;i++)if(pending_areas[i]) {
  free_pools(pending_areas[i]);free(pending_areas[i]->raster);free(pending_areas[i]);pending_areas[i]=NULL;
 }
 pending_load_ok=false;
}
const char *ZeldaWorldStateIdentity(void) {return enabled?"alttp-world-v4":NULL;}
static void area_stream(SaveLoadInfo*sli,WorldArea*r) {
 sli->func(sli,r->ram,sizeof r->ram);sli->func(sli,r->vram,sizeof r->vram);
 sli->func(sli,r->palette,sizeof r->palette);sli->func(sli,r->tiles,sizeof r->tiles);
 sli->func(sli,r->pieces,sizeof r->pieces);
}
static void pool_stream(SaveLoadInfo*sli,WorldActorPool*p) {
 sli->func(sli,p->ram,sizeof p->ram);sli->func(sli,p->pieces,sizeof p->pieces);
}
void ZeldaWorldStateSave(SaveLoadInfo*sli) {
 if(!enabled)return;
 WorldStateHeader h={WORLD_STATE_MAGIC,4,0,(uint32_t)record_bytes(),
  active_world,last_area,zoom,atlas,camera_x,camera_y,native_oam_x,native_oam_y,handoffs};
 h.global_count=live_piece_count;h.global_valid=live_pieces_valid;
 memcpy(h.global_pieces,live_pieces,live_piece_count*sizeof(*live_pieces));
 h.weapon_count=weapon_piece_count;memcpy(h.weapon_pieces,weapon_pieces,weapon_piece_count*sizeof(*weapon_pieces));
 memcpy(h.arrow_targets,arrow_targets,sizeof arrow_targets);
 for(unsigned i=0;i<ZW_AREAS;i++)h.count+=areas[i]&&areas[i]->valid;
 sli->func(sli,&h,sizeof h);
 for(unsigned i=0;i<ZW_AREAS;i++)if(areas[i]&&areas[i]->valid) {
  WorldArea*r=areas[i];WorldRecordHeader a={i,r->piece_count,r->obsel,(pool_count(r)<<1)|r->populated,r->ticks};
  sli->func(sli,&a,sizeof a);area_stream(sli,r);
  for(WorldActorPool*p=r->pools;p;p=p->next) {
   WorldPoolHeader ph={p->ticks,p->piece_count,0};sli->func(sli,&ph,sizeof ph);pool_stream(sli,p);
  }
 }
 RtlSaveExecutionState(sli);
}
bool ZeldaWorldStatePreflight(const void*data,size_t size) {
 if(!enabled)return true;
 clear_pending();
 size_t core=RtlSnapshotGuestSize(NULL);
 if(!core||core>size||size-core<sizeof(WorldStateHeader))return false;
 const uint8_t*p=(const uint8_t*)data+core;
 WorldStateHeader h;memcpy(&h,p,sizeof h);p+=sizeof h;
 if(h.magic!=WORLD_STATE_MAGIC||h.schema!=4||h.count>ZW_AREAS||h.record_bytes!=record_bytes()||
    (h.world!=-1&&h.world!=0&&h.world!=64)||h.zoom<1||h.zoom>8||(h.atlas!=0&&h.atlas!=1)||
    h.last < -1||h.last>=ZW_AREAS||h.global_count>1024||h.global_valid>1||h.weapon_count>1024||h.weapon_reserved)return false;
 for(unsigned j=0;j<h.global_count;j++) {
  unsigned n=h.global_pieces[j].size;if(n!=8&&n!=16&&n!=32&&n!=64)return false;
 }
 for(unsigned j=0;j<h.weapon_count;j++) {
  unsigned n=h.weapon_pieces[j].size;if(n!=8&&n!=16&&n!=32&&n!=64)return false;
 }
 for(unsigned k=0;k<5;k++) {
  const ArrowTarget*t=&h.arrow_targets[k];
  if(t->valid>1||(t->valid&&(t->area>=ZW_AREAS||owner(t->area)!=t->area||t->pool>ZW_EXTRA_POOLS||t->slot>=16)))return false;
 }
 StateCount exec=execution_layout();
 size_t expected=sizeof h+(size_t)h.count*record_bytes()+exec.bytes;
 if(size-core<expected)return false;
 const uint8_t*end=(const uint8_t*)data+size-exec.bytes;
 for(unsigned i=0;i<h.count;i++) {
  if((size_t)(end-p)<record_bytes())goto bad;
  WorldRecordHeader a;memcpy(&a,p,sizeof a);p+=sizeof a;
  if(a.id>=ZW_AREAS||owner(a.id)!=a.id||pending_areas[a.id]||a.pieces>1024||a.obsel>255||(a.reserved>>1)>ZW_EXTRA_POOLS)goto bad;
  WorldArea*r=calloc(1,sizeof(*r));if(!r)goto bad;
  pending_areas[a.id]=r;r->size=area_size(a.id);r->ticks=a.ticks;r->piece_count=a.pieces;r->obsel=(uint8_t)a.obsel;
  memcpy(r->ram,p,sizeof r->ram);p+=sizeof r->ram;
  memcpy(r->vram,p,sizeof r->vram);p+=sizeof r->vram;
  memcpy(r->palette,p,sizeof r->palette);p+=sizeof r->palette;
  memcpy(r->tiles,p,sizeof r->tiles);p+=sizeof r->tiles;
  memcpy(r->pieces,p,sizeof r->pieces);p+=sizeof r->pieces;
  for(unsigned j=0;j<a.pieces;j++) {
   unsigned n=r->pieces[j].size;if(n!=8&&n!=16&&n!=32&&n!=64)goto bad;
  }
  r->valid=r->dirty=true;r->populated=(a.reserved&1)!=0;
  WorldActorPool**tail=&r->pools;
  for(unsigned k=0;k<(a.reserved>>1);k++) {
   if((size_t)(end-p)<pool_bytes())goto bad;
   WorldPoolHeader ph;memcpy(&ph,p,sizeof ph);p+=sizeof ph;
   if(ph.pieces>1024||ph.reserved)goto bad;
   WorldActorPool*pool=calloc(1,sizeof(*pool));if(!pool)goto bad;
   *tail=pool;tail=&pool->next;pool->ticks=ph.ticks;pool->piece_count=ph.pieces;
   memcpy(pool->ram,p,sizeof pool->ram);p+=sizeof pool->ram;
   memcpy(pool->pieces,p,sizeof pool->pieces);p+=sizeof pool->pieces;
   for(unsigned j=0;j<ph.pieces;j++) {
    unsigned n=pool->pieces[j].size;if(n!=8&&n!=16&&n!=32&&n!=64)goto bad;
   }
  }
 }
 if(p!=end||(h.last>=0&&(!pending_areas[h.last]||(h.last&64)!=h.world))||
    memcmp(p,exec.first,sizeof exec.first))goto bad;
 for(unsigned k=0;k<5;k++)if(h.arrow_targets[k].valid) {
  const ArrowTarget*t=&h.arrow_targets[k];
  if(!pending_areas[t->area]||t->pool>pool_count(pending_areas[t->area]))goto bad;
 }
 pending_header=h;pending_load_ok=true;return true;
bad:
 clear_pending();return false;
}
void ZeldaWorldStateLoad(SaveLoadInfo*sli,uint32_t version) {
 (void)version;if(!enabled)return;
 if(!pending_load_ok)return;
 WorldStateHeader h;sli->func(sli,&h,sizeof h);
 for(unsigned i=0;i<h.count;i++) {
  WorldRecordHeader a;sli->func(sli,&a,sizeof a);
  WorldArea*r=pending_areas[a.id];area_stream(sli,r);
  for(WorldActorPool*p=r->pools;p;p=p->next) {
   WorldPoolHeader ph;sli->func(sli,&ph,sizeof ph);pool_stream(sli,p);
  }
 }
 pending_load_ok=RtlLoadExecutionState(sli);
}
void ZeldaWorldStateLoaded(uint32_t version) {
 (void)version;
 uint64_t old_failures=failures;
 ZeldaWorldReset();failures=old_failures;
 if(enabled&&pending_load_ok) {
  for(unsigned i=0;i<ZW_AREAS;i++){areas[i]=pending_areas[i];pending_areas[i]=NULL;}
  active_world=pending_header.world;last_area=pending_header.last;
  zoom=pending_header.zoom;atlas=pending_header.atlas!=0;
  camera_x=pending_header.camera_x;camera_y=pending_header.camera_y;
  native_oam_x=pending_header.oam_x;native_oam_y=pending_header.oam_y;
  handoffs=pending_header.handoffs;decoded=pending_header.count;
  live_piece_count=pending_header.global_count;live_pieces_valid=pending_header.global_valid!=0;
  memcpy(live_pieces,pending_header.global_pieces,live_piece_count*sizeof(*live_pieces));
  weapon_piece_count=pending_header.weapon_count;memcpy(weapon_pieces,pending_header.weapon_pieces,weapon_piece_count*sizeof(*weapon_pieces));
  memcpy(arrow_targets,pending_header.arrow_targets,sizeof arrow_targets);
  held_transition_velocity=false;collision_window=false;
  RtlApplyExecutionState();ppu_rasterReset();
 }
 pending_load_ok=false;
}
static uint64_t digest_bytes(const uint8_t*p,size_t n) {
 uint64_t h=UINT64_C(14695981039346656037);
 for(size_t i=0;i<n;i++){h^=p[i];h*=UINT64_C(1099511628211);}return h;
}
static ReplayDigest replay_digest(const uint8_t*p,size_t n) {
 ReplayDigest d={digest_bytes(p,n),0,0,n};
 size_t prefix=enabled?48:0,core=RtlSnapshotGuestSize(NULL);
 if(prefix+core<=n)d.core=digest_bytes(p+prefix,core);
 if(enabled&&prefix+core+sizeof(WorldStateHeader)<=n) {
  WorldStateHeader h;memcpy(&h,p+prefix+core,sizeof h);
  StateCount exec=execution_layout();
  size_t bytes=n-prefix-core>=exec.bytes?n-prefix-core-exec.bytes:0;
  if(bytes<=n-prefix-core)d.world=digest_bytes(p+prefix+core,bytes);
 }
 return d;
}
static void replay_release(void) {
 free(replay.checkpoint);free(replay.scratch);free(replay.final);
 replay.checkpoint=replay.scratch=replay.final=NULL;
}
void ZeldaWorldHardwareReset(void) {
 /* A user reset cancels a developer replay. Loading its current-build
  * checkpoint still uses StateLoaded and must not cancel that replay. */
 replay_release();memset(&replay,0,sizeof replay);ui_resume_deadline=0;
 ZeldaWorldReset();
}
static void replay_boundary(uint32_t*inputs) {
 if(replay.phase<1||replay.phase>3)return;
 if(replay.phase==1) {
  replay.cap=RtlRollbackSnapshotBound();
  replay.checkpoint=malloc(replay.cap);replay.scratch=malloc(replay.cap);
  if(!replay.checkpoint||!replay.scratch)goto fail;
  replay.checkpoint_bytes=RtlNetplaySnapshotToMemory(replay.checkpoint,replay.cap);
  if(!replay.checkpoint_bytes)goto fail;
  replay.inputs|=*inputs&0xc0000000u;replay.phase=2;replay.index=0;
 }else {
  size_t bytes=RtlNetplaySnapshotToMemory(replay.scratch,replay.cap);
  if(!bytes)goto fail;
  ReplayDigest d=replay_digest(replay.scratch,bytes);
  if(replay.phase==2) {
   replay.digests[replay.index++]=d;
   if(replay.index==replay.n) {
    replay.final=malloc(bytes);if(!replay.final)goto fail;
    memcpy(replay.final,replay.scratch,bytes);replay.final_bytes=bytes;
    if(!RtlRollbackLoadFromMemory(replay.checkpoint,replay.checkpoint_bytes))goto fail;
    replay.phase=3;replay.index=0;
   }
  }else {
   ReplayDigest expected=replay.digests[replay.index];
   if((expected.all!=d.all||expected.bytes!=d.bytes)&&replay.first_bad<0) {
    replay.first_bad=replay.index+1;replay.expected=expected;replay.actual=d;
   }
   if(++replay.index==replay.n) {
    replay.final_equal=bytes==replay.final_bytes&&!memcmp(replay.final,replay.scratch,bytes);
    size_t max=bytes<replay.final_bytes?bytes:replay.final_bytes;
    for(replay.first_diff=0;replay.first_diff<max&&replay.final[replay.first_diff]==replay.scratch[replay.first_diff];replay.first_diff++);
    if(replay.first_bad<0){replay.expected=expected;replay.actual=d;}
    replay.phase=4;replay_release();return;
   }
  }
 }
 *inputs=replay.inputs;return;
fail:
 replay.phase=5;replay_release();
}
void ZeldaWorldSetup(void) {
 if(!command_mutex)command_mutex=SDL_CreateMutex();
 debug_server_set_game_command_handler(queued_command);
 previous_pc_hook=g_interp_bridge_pc_hook;g_interp_bridge_pc_hook=world_pc_hook;
 RtlSetStateExtraBound(world_state_bound);
}
