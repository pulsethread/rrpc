#pragma once
#include "cpu_state.h"
#include "pgxp_hooks.h"

inline void rage_native_mirror_state(CPUState* cpu,uint32_t last_cell){
    // The original engine leaves the reflected GTE row installed for
    // later mirror objects. Replay its small register-only operation,
    // including precision shadows, without running any face builders.
    auto cfc=[&](unsigned reg,unsigned dest,uint32_t instr){
        cpu->gpr[dest]=gte_read_ctrl(cpu,(uint8_t)reg);
        psx_pgxp_cop2(cpu,instr,cpu->gpr[dest],0);
    };
    auto ctc=[&](unsigned reg,unsigned src,uint32_t instr){
        gte_write_ctrl(cpu,(uint8_t)reg,cpu->gpr[src]);
        psx_pgxp_cop2(cpu,instr,cpu->gpr[src],0);
    };
    auto alu=[&](uint32_t instr,uint32_t value){
        const uint32_t a=cpu->gpr[(instr>>21)&31],b=cpu->gpr[(instr>>16)&31];
        cpu->gpr[(instr>>26)?((instr>>16)&31):((instr>>11)&31)]=value;
        psx_pgxp_alu(cpu,instr,value,a,b);
    };
    cfc(0,10,0x484a0000);cfc(1,11,0x484b0800);
    alu(0x000a6400,cpu->gpr[10]<<16);alu(0x000c6403,uint32_t(int32_t(cpu->gpr[12])>>16));
    alu(0x000c6022,0u-cpu->gpr[12]);alu(0x000a6c03,uint32_t(int32_t(cpu->gpr[10])>>16));
    alu(0x000d6822,0u-cpu->gpr[13]);alu(0x000d6c00,cpu->gpr[13]<<16);
    alu(0x318cffff,cpu->gpr[12]&65535);alu(0x018d6025,cpu->gpr[12]|cpu->gpr[13]);ctc(0,12,0x48cc0000);
    alu(0x000b6400,cpu->gpr[11]<<16);alu(0x000c6403,uint32_t(int32_t(cpu->gpr[12])>>16));
    alu(0x000c6022,0u-cpu->gpr[12]);alu(0x000b6c03,uint32_t(int32_t(cpu->gpr[11])>>16));
    alu(0x000d6c00,cpu->gpr[13]<<16);alu(0x318cffff,cpu->gpr[12]&65535);
    alu(0x018d6025,cpu->gpr[12]|cpu->gpr[13]);ctc(1,12,0x48cc0800);
    if(last_cell){
        for(unsigned i=0;i<3;++i){const uint32_t value=cpu->read_word(last_cell+4*i);
            cpu->gpr[9+i]=value;psx_pgxp_load(cpu,0x8e29fff0u+(i<<16)+(4*i),last_cell+4*i,value);}
        ctc(6,10,0x48ca3000);ctc(7,11,0x48cb3800);
        alu(0x00094823,0u-cpu->gpr[9]);ctc(5,9,0x48c92800);
    }
}
