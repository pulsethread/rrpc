#pragma once
#include <stdint.h>
#include <cstring>
// Region metadata contains addresses only; game files always come from the disc.
inline unsigned rage_active_region=0;
inline void rage_region_select(const char* id) {
    rage_active_region=(!std::strcmp(id,"SLPS-00600") || !std::strcmp(id,"SLPS_006.00"))?1:
        (!std::strcmp(id,"SLUS-00403") || !std::strcmp(id,"SLUS_004.03"))?2:0;
}
inline constexpr uint32_t rage_region_addresses[][3]={
{0x80016754u,0x800165c8u,0x8001672cu},
{0x8001cd54u,0x8001cbc0u,0x8001cd24u},
{0x8001cfb4u,0x8001ce20u,0x8001cf84u},
{0x8007f5b0u,0x8007ee30u,0x8007f2b8u},
{0x8009afd4u,0x8009a764u,0x8009acdcu},
{0x801e40a8u,0x801e3838u,0x801e3db0u},
{0x801e40b8u,0x801e3848u,0x801e3dc0u},
{0x801e40e0u,0x801e3870u,0x801e3de8u},
{0x801e42ccu,0x801e3a5cu,0x801e3fd4u},
{0x801e436eu,0x801e3afeu,0x801e4076u},
{0x801e4b60u,0x801e42ecu,0x801e4868u},
{0x8019cb08u,0x8019c298u,0x8019c810u},
{0x80017ad0u,0x80017944u,0x80017aa8u},
{0x8001dab0u,0x8001d91cu,0x8001da80u},
{0x8001df44u,0x8001ddb0u,0x8001df14u},
{0x8001dfc0u,0x8001de2cu,0x8001df90u},
{0x8001e098u,0x8001df04u,0x8001e068u},
{0x8001e0d8u,0x8001df44u,0x8001e0a8u},
{0x8001e538u,0x8001e3a4u,0x8001e508u},
{0x80027ff4u,0x80027ac0u,0x80027e10u},
{0x80028decu,0x800288b8u,0x80028c08u},
{0x80029064u,0x80028b30u,0x80028e80u},
{0x800293f0u,0x80028ebcu,0x8002920cu},
{0x80029230u,0x80028cfcu,0x8002904cu},
{0x800295bcu,0x80029088u,0x800293d8u},
{0x800296b4u,0x80029180u,0x800294d0u},
{0x8003d6d0u,0x8003d060u,0x8003d3e4u},
{0x800414f0u,0x80040e80u,0x80041204u},
{0x800418d4u,0x80041264u,0x800415e8u},
{0x8007f510u,0x8007ed90u,0x8007f218u},
{0x8009e690u,0x8009de20u,0x8009e398u},
{0x8019caccu,0x8019c25cu,0x8019c7d4u},
{0x801e4144u,0x801e38d4u,0x801e3e4cu},
{0x801e42e4u,0x801e3a74u,0x801e3fecu},
{0x801e5020u,0x801e47acu,0x801e4d28u},
};
inline uint32_t rage_region_address(uint32_t address) {
    for(const auto& row:rage_region_addresses)if(row[0]==address)return row[rage_active_region];
    return address;
}
inline uint32_t rage_region_code(uint32_t address) {
    const uint32_t phys=address&0x1fffffffu;
    if(rage_active_region)for(const auto& row:rage_region_addresses)
        if(row[0]<0x80060000u && (row[rage_active_region]&0x1fffffffu)==phys)return row[0]&0x1fffffffu;
    return phys;
}
