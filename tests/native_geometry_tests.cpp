#include "rage_native_geometry.h"
#include "rage_track_visibility.h"
#include "rage_vehicle_material.h"
#include <array>
#include <iostream>
#include <stdexcept>
#define require(x) do {if(!(x))throw std::runtime_error(#x);} while(0)
int main() {
    try {
        // Compare cache-admission tags with the original draw-time scans.
        // Player tags may precede caster tags or appear on another vertex;
        // the first player pose wins, but only if the batch casts a shadow.
        for(unsigned pattern=0;pattern<4096;++pattern){
            float mesh[6*16]={};
            for(unsigned i=0;i<6;++i){mesh[16*i+9]=float(i+3);
                mesh[16*i+11]=float(((pattern>>(2*i))&3u)*8192u);}
            int caster=0,player=-1;
            for(unsigned i=0;i<6;++i)if((int)mesh[16*i+11]&8192){caster=1;break;}
            if(caster)for(unsigned i=0;i<6;++i)if((int)mesh[16*i+11]&16384){player=(int)mesh[16*i+9];break;}
            const auto tags=rage_native_mesh_tags(mesh,6);
            require(tags.caster==caster && tags.player_pose==player);
            for(bool decal:{false,true})for(bool background:{false,true}){
                const int active=!decal && !background && tags.caster;
                require(active==(!decal && !background && caster));
                require((active?tags.player_pose:-1)==(!decal && !background && caster?player:-1));
            }
        }
        require(rage_native_mesh_tags(nullptr,0).player_pose==-1);
        // Signed authored layer offsets and raw-texture mode coexist in the
        // immutable native vertex key, without touching geometry or UVs.
        {float input[32]={},output[16]={};input[13]=1;input[26]=float(255u<<10);rage_native_pack_vertex(output,input,0);
         require(int(output[10])==511);require((int(output[10])&1)==1);require(int8_t(int(output[10])>>1)==-1);}

        {float input[32]={},output[16]={};input[26]=float((255u<<10)|262144u|524288u);rage_native_pack_vertex(output,input,0);
         require(int8_t(int(output[10])>>1)==-1);require((int(output[11])&1536)==1536);}
        {float input[32]={},output[16]={};input[26]=2097152.f;rage_native_pack_vertex(output,input,0);require((int(output[11])&4096)!=0);require(output[10]==0);}
        {float input[32]={},output[16]={};input[26]=float(4194304u|8388608u|(255u<<10));rage_native_pack_vertex(output,input,0);
         require((int(output[11])&24576)==24576);require(int8_t(int(output[10])>>1)==-1);}
        require(rage_vehicle_tread_material(0,0x29064,0x280c0c0c));
        require(rage_vehicle_tread_material(5,0x293f0,0x28101010));
        require(!rage_vehicle_tread_material(1,0x29064,0x280c0c0c));
        require(!rage_vehicle_tread_material(2,0x29064,0x280c0c0c));
        require(rage_vehicle_tread_material(0,0x29064,0x28000000));
        require(!rage_vehicle_tread_material(0,0x29064,0x2a0c0c0c));
        require(rage_vehicle_tread_material(0,0x29064,0x280c100c));
        require(rage_vehicle_tread_material(0,0x29064,0x28404040));
        require(rage_vehicle_tread_material(0,0x29230,0x280c0c0c));
        require(rage_vehicle_tread_material(4,0x295bc,0));
        require(!rage_vehicle_tread_material(1,0x295bc,0));
        // AI front/rear wheel streams, including distant zero-area caps,
        // retain opaque geometry without admitting shadows or far bodies.
        require(rage_vehicle_wheel_material(false,true,2,0x29230,0));
        require(rage_vehicle_wheel_material(false,false,33,0x295bc,0));
        require(rage_vehicle_wheel_material(false,false,3,0x29064,0x280c0c0c));
        require(!rage_vehicle_wheel_material(false,false,1,0x29230,0));
        require(!rage_vehicle_wheel_material(false,false,4,0x29230,0));
        require(!rage_vehicle_wheel_material(false,false,3,0x29064,0x2a0c0c0c));
        require(rage_vehicle_wheel_material(true,false,6,0x29230,0));
        require(!rage_vehicle_wheel_material(true,false,1,0x29230,0));
        require(!rage_vehicle_shadow_entry(true,false,6));
        require(rage_vehicle_shadow_entry(true,false,1));
        require(rage_vehicle_shadow_entry(false,false,6));
        require(!rage_vehicle_shadow_entry(false,true,2));
        const auto code=[](RageTrackProjection p){return rage_track_outcode(p,0,0,320,240,10000);};
        require(code({16000,12000,100})==0);
        require((code({-100,100,100})&code({-200,200,100})&code({-300,300,100}))!=0);
        // A straddling triangle must survive even if no corner is in-view.
        require((code({-100,100,100})&code({33000,100,100})&code({16000,-100,100}))==0);
        require((code({-100,-100,-1})&code({16000,12000,100})&code({32000,24000,100}))==0);
        require((code({0,0,.1})&code({0,0,.2})&code({0,0,.3}))&1);
        require(!(code({0,0,2})&1));
        require((code({0,0,10001})&code({1,0,10002})&code({0,1,10003}))&2);
        std::array<unsigned char,1057> bytes{};
        for(size_t i=0;i<bytes.size();++i)bytes[i]=(unsigned char)(i*73+19);
        // Include unaligned addresses, every partial word/lane tail, and a
        // many complete lane blocks. Every input byte must affect the key.
        for(size_t offset=0;offset<8;++offset)for(size_t length=0;length<=1024;++length){
            const auto key=rage_native_geometry_key(bytes.data()+offset,length);
            require(key==rage_native_geometry_key(bytes.data()+offset,length));
            if(length){for(size_t index:{size_t(0),length/2,length-1}){
                bytes[offset+index]^=1;
                require(key!=rage_native_geometry_key(bytes.data()+offset,length));
                bytes[offset+index]^=1;
            }}
        }
        float source[32]={},first[16],next[16];
        source[2]=12;source[3]=23;source[4]=.5f;source[13]=1;
        source[26]=18;source[27]=120000;source[29]=200000;source[30]=47;source[31]=9;
        rage_native_pack_vertex(first,source,0);
        const auto key=rage_native_geometry_key(first,sizeof first);
        source[0]=-16384;source[1]=16384;source[7]=-65537;source[19]=.001f;source[30]=2;
        rage_native_pack_vertex(next,source,0);
        require(!memcmp(first,next,sizeof first));
        require(key==rage_native_geometry_key(next,sizeof next));
        for(unsigned attribute:{2u,3u,4u,13u,14u,15u,16u,17u,26u,27u,28u,29u,31u}){
            const float old=source[attribute];source[attribute]+=1;
            rage_native_pack_vertex(next,source,0);
            require(key!=rage_native_geometry_key(next,sizeof next));source[attribute]=old;
        }
        rage_native_pack_vertex(next,source,1);
        require(next[9]==1 && key!=rage_native_geometry_key(next,sizeof next));
        // Adjacent fan wedges use a common image. Filtering must cross
        // their shared edge while stopping at the enclosing atlas tile.
        float fan[6*16]={};
        const float points[4][5]={{0,0,20,160,158},{10,0,20,129,159},{7,7,20,138,137},{0,10,20,160,128}};
        const int ids[6]={0,1,2,0,2,3};
        for(int i=0;i<6;++i){float* v=fan+i*16;memcpy(v,points[ids[i]],20);v[8]=2;v[9]=0;v[11]=256;
            v[12]=i<3?129:138;v[13]=i<3?137:128;v[14]=160;v[15]=i<3?159:158;}
        float authored[96];memcpy(authored,fan,sizeof fan);
        require(rage_native_material_regions(fan,6)==2);
        for(int i=0;i<6;++i){require(!memcmp(authored+i*16,fan+i*16,12*sizeof(float)));
            require(fan[i*16+12]==129 && fan[i*16+13]==128 && fan[i*16+14]==160 && fan[i*16+15]==159);}
        // Identical geometry with a UV seam must not join another tile.
        fan[3*16+3]+=1;fan[4*16+3]+=1;
        for(int i=3;i<6;++i)fan[i*16+12]=138;
        require(rage_native_material_regions(fan,6)==0);
        // Neither a separate pose nor a separate decoded material may join.
        fan[3*16+3]-=1;fan[4*16+3]-=1;
        for(int i=3;i<6;++i)fan[i*16+9]=1;
        require(rage_native_material_regions(fan,6)==0);
        for(int i=3;i<6;++i){fan[i*16+9]=0;fan[i*16+8]=3;}
        require(rage_native_material_regions(fan,6)==0);
        // Sharing a repeated corner is not sharing a surface edge.
        for(int i=0;i<6;++i){memcpy(fan+i*16,points[i%3==2?(i<3?1:3):0],20);fan[i*16+8]=2;}
        require(rage_native_material_regions(fan,6)==0);
        std::cout<<"Geometry hash tails, alignment, material changes and camera independence passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
