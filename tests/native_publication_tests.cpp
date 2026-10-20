#undef NDEBUG
#include <cassert>
#include <array>
#include <vector>
#include <memory>
#include "native_terrain_publication.h"
static std::array<uint32_t,0x200000/4> memory;
static std::vector<uint32_t> writes;
static uint32_t read(uint32_t a){return memory[(a&0x1fffffffu)/4];}
static void write(uint32_t a,uint32_t v){writes.push_back(a);memory[(a&0x1fffffffu)/4]=v;}
int main(){
    constexpr uint32_t cursor=0x80120000,ot=0x800e0000,slot=ot+575*4;
    memory.fill(0x12345678);memory[(slot&0x1fffffffu)/4]=0x000abc00;
    assert(rage_native_terrain_publish(cursor,ot,read,write));
    assert(writes.size()==6);
    assert(read(cursor)==0x040abc00 && read(cursor+4)==0x20000000);
    assert(read(cursor+8)==0 && read(cursor+12)==0 && read(cursor+16)==0);
    assert(read(cursor+20)==0x12345678 && read(cursor-4)==0x12345678);
    assert(read(slot)==(cursor&0xffffff));
    // A second view may share the OT: preserve the preceding chain link.
    assert(rage_native_terrain_publish(cursor+20,ot,read,write));
    assert(read(cursor+20)==(0x04000000|(cursor&0xffffff)));
    assert(read(slot)==((cursor+20)&0xffffff));
    // Reject malformed spans without making any partial packet/OT writes.
    const auto before=std::make_unique<decltype(memory)>(memory);
    for(auto bad:std::array<uint32_t,5>{cursor+1,0x801ffff0,0x1f800000,0x80200000,ot+100}){
        writes.clear();assert(!rage_native_terrain_publish(bad,ot,read,write));assert(writes.empty());
    }
    writes.clear();assert(!rage_native_terrain_publish(cursor,0x801ff800,read,write));assert(writes.empty());
    assert(!rage_native_terrain_publish(cursor,ot,nullptr,write));assert(writes.empty());
    assert(memory==*before);
    // A stale self-link would create a DMA cycle; fail closed.
    writes.clear();assert(!rage_native_terrain_publish(cursor+20,ot,read,write));assert(writes.empty());
}
