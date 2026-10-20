#undef NDEBUG
#include <cassert>
#include <cstdint>
#include "native_terrain_mirror.h"
static unsigned alu_count,load_count,control_writes;
static uint32_t coords[3]={uint32_t(-12345),6789,31415};
static uint32_t read(uint32_t addr){assert(addr>=0x1000 && addr<0x100c);return coords[(addr-0x1000)/4];}
extern "C" uint32_t gte_read_ctrl(CPUState* cpu,uint8_t reg){return cpu->gte_ctrl[reg];}
extern "C" void gte_write_ctrl(CPUState* cpu,uint8_t reg,uint32_t value){cpu->gte_ctrl[reg]=value;++control_writes;}
extern "C" void psx_pgxp_cop2(CPUState* cpu,uint32_t instr,uint32_t value,uint32_t addr){
    assert(addr==0);const unsigned rt=(instr>>16)&31,rd=(instr>>11)&31;
    assert(cpu->gpr[rt]==value && cpu->gte_ctrl[rd]==value);
}
extern "C" void psx_pgxp_load(CPUState* cpu,uint32_t instr,uint32_t addr,uint32_t value){
    assert(cpu->gpr[(instr>>16)&31]==read(addr) && value==read(addr));++load_count;
}
extern "C" void psx_pgxp_alu(CPUState* cpu,uint32_t instr,uint32_t result,uint32_t a,uint32_t b){
    uint32_t expected=0;const unsigned shift=(instr>>6)&31;
    if((instr>>26)==12)expected=a&(instr&65535);
    else switch(instr&63){
        case 0:expected=b<<shift;break;
        case 3:expected=uint32_t(int32_t(b)>>shift);break;
        case 0x22:case 0x23:expected=a-b;break;
        case 0x25:expected=a|b;break;
        default:assert(false);
    }
    const unsigned dest=(instr>>26)?((instr>>16)&31):((instr>>11)&31);
    assert(result==expected && cpu->gpr[dest]==expected);++alu_count;
}
int main(){
    // All signed-half edge values, paired with a different second component.
    for(uint32_t x=0;x<65536;++x){
        CPUState cpu{};cpu.read_word=read;cpu.gpr[16]=0xaabbccdd;cpu.gpr[17]=0x11223344;
        cpu.gpr[29]=0x801ff000;cpu.gpr[31]=0x80040000;
        const uint32_t y=(x*37+17)&65535,z=(x*19+3)&65535;
        const uint32_t high=((x*13+0x8000)&65535)<<16;
        cpu.gte_ctrl[0]=x|(y<<16);cpu.gte_ctrl[1]=high|z;
        cpu.gte_ctrl[21]=0xfeedbeef;alu_count=load_count=control_writes=0;
        rage_native_mirror_state(&cpu,0x1000);
        assert(cpu.gte_ctrl[0]==(((0u-x)&65535)|(((0u-y)&65535)<<16)));
        assert(cpu.gte_ctrl[1]==(high|((0u-z)&65535)));
        assert(cpu.gte_ctrl[5]==0u-coords[0] && cpu.gte_ctrl[6]==coords[1] && cpu.gte_ctrl[7]==coords[2]);
        assert(cpu.gpr[16]==0xaabbccdd && cpu.gpr[17]==0x11223344);
        assert(cpu.gpr[29]==0x801ff000 && cpu.gpr[31]==0x80040000 && cpu.gpr[0]==0);
        assert(cpu.gte_ctrl[21]==0xfeedbeef && load_count==3 && control_writes==5 && alu_count==16);
    }
    CPUState cpu{};cpu.gte_ctrl[5]=42;load_count=control_writes=0;
    rage_native_mirror_state(&cpu,0);
    assert(cpu.gte_ctrl[5]==42 && load_count==0 && control_writes==2);
}
