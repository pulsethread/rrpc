#include "pgxp.h"
#include <cassert>
#include <cmath>
extern "C" {uint32_t g_psx_ram_mask=0x1fffff;}
extern "C" int gte_geometry_correction_lookup(uint32_t,int32_t*,int32_t*){return 0;}
extern "C" int gte_geometry_correction_lookup_probe(uint32_t,int32_t*,int32_t*){return 0;}
extern "C" int psx_mod_gpu_dma_memory_contains(uint32_t,uint32_t){return 0;}
int main(){
 pgxp_set_enabled(1);pgxp_set_cpu_mode(1);pgxp_set_projection_tracking(1);pgxp_set_position_fallback(0);pgxp_set_tolerance(-1);pgxp_set_preserve_projection(1);
 const uint32_t address=0x80110008,packed=0x000c0008;
 pgxp_gte_push_sxy(100,200,300,packed);int32_t beforex,beforey;assert(pgxp_get_gte_sxy(2,&beforex,&beforey));
 for(int i=1;i<10;++i){double x=8+i*.1;assert(pgxp_store_host_vertex(address,packed,x,12.5,300));
  int32_t xx,yy;uint16_t z;assert(pgxp_get_precise_vertex(address,packed,8,12,&xx,&yy,&z));
  assert(xx==std::lround(x*65536) && yy==12*65536+32768 && z==300);
  PGXPProjection p;assert(pgxp_load_projection(address,packed,&p));assert(std::abs(p.x/p.z-x)<.0001);
 }
 int32_t afterx,aftery;assert(pgxp_get_gte_sxy(2,&afterx,&aftery) && beforex==afterx && beforey==aftery);
 assert(!pgxp_store_host_vertex(address+1,packed,8.5,12.5,300));assert(!pgxp_store_host_vertex(0x1f801814,packed,8.5,12.5,300));
 assert(!pgxp_store_host_vertex(address,packed,18.5,12.5,300));assert(!pgxp_store_host_vertex(address,packed,NAN,12.5,300));assert(!pgxp_store_host_vertex(address,packed,8.5,12.5,0));
 pgxp_suppress_begin();assert(!pgxp_store_host_vertex(address,packed,8.5,12.5,300));pgxp_suppress_end();
 pgxp_invalidate_word(address);int32_t x,y;uint16_t z;assert(!pgxp_get_precise_vertex(address,packed,8,12,&x,&y,&z));
}
