// Real-disc fixtures remain local; no original track data is packaged here.
#include "../recomp/src/rage_oval.cpp"
#include "../recomp/src/rage_free_world.cpp"
#include <cassert>
#include <iostream>
namespace {
std::vector<uint8_t> guest_ram(0x200000),guest_mod(0x100000);
uint32_t allocated=0;
uint8_t& rbref(uint32_t p){p&=0x1fffffff;return p>=0x1f000000?guest_mod.at(p-0x1f000000):guest_ram.at(p);}
uint8_t rb(uint32_t p){return rbref(p);}
uint16_t rh(uint32_t p){return rb(p)|(rb(p+1)<<8);}
uint32_t rw(uint32_t p){return rh(p)|(uint32_t(rh(p+2))<<16);}
void wb(uint32_t p,uint8_t v){rbref(p)=v;}
void wh(uint32_t p,uint16_t v){wb(p,v);wb(p+1,v>>8);}
void ww(uint32_t p,uint32_t v){wh(p,v);wh(p+2,v>>16);}
}
static bool freeRunActive=true;
extern "C" int rage_free_run_active(){return freeRunActive;}
extern "C" void rage_minimap_routes(const float*,const int*,int){}
extern "C" uint32_t psx_mod_memory_alloc(uint32_t bytes,uint32_t alignment){
    allocated=(allocated+alignment-1)&~(alignment-1);const uint32_t p=allocated;allocated+=bytes;assert(allocated<=guest_mod.size());return 0x9f000000+p;
}
int main(int argc,char** argv){
    // A circular road must use its arc, not the chord between control points.
    const RageRoadPoint arcA{1000,0,100,1024,0,0,100,150,1,1571};
    const RageRoadPoint arcB{0,1000,200,2048,0,0,100,150,1,1571};
    auto curve=rage_road_contact(arcA,arcB,0,0,std::sqrt(.5)*1000,std::sqrt(.5)*1000);
    assert(std::abs(curve.lateral)<1e-6 && std::abs(curve.t-.5)<1e-6 && curve.clearance>99);
    curve=rage_road_contact(arcA,arcB,0,0,std::sqrt(.5)*1040,std::sqrt(.5)*1040);
    assert(std::abs(curve.lateral-40)<1e-6 && std::abs(curve.clearance-110)<1e-6);
    // A short overhead bridge is excluded; a sustained roof is a tunnel.
    RageTrackCourse shelter;
    shelter.vertices={{{-1000,-400,0}},{{1000,-400,0}},{{-1000,-400,4000}},{{1000,-400,4000}}};
    RageTrackFace ceiling{};for(unsigned i=0;i<4;++i)ceiling.vertex[i]=i;shelter.faces.push_back(ceiling);
    for(int i=0;i<12;++i)shelter.route.push_back({0,0,float(i*300),400,400});
    assert(rage_track_has_roof(shelter,0,0,1500));
    assert(!rage_track_has_roof(shelter,1500,0,1500));
    assert(!rage_track_has_roof(shelter,0,-800,1500));
    const auto tunnel=rage_track_tunnel_points(shelter);
    assert(std::count(tunnel.begin(),tunnel.end(),1)==12);
    shelter.vertices[2][2]=shelter.vertices[3][2]=1200;
    const auto bridge=rage_track_tunnel_points(shelter);
    assert(std::count(bridge.begin(),bridge.end(),1)==0);
    assert(argc==2);const std::filesystem::path fixtures=argv[1];
    const char* names[4]={"BIG1","MID1","HI1","OVAL5"};
    for(unsigned r=0;r<4;++r)free_packs[(r==3?4:0)*4+r]=file(fixtures/(std::string(names[r])+".2ND"));
    for(unsigned r=0;r<3;++r){const auto& pack=free_packs[r];const auto off=chunks(pack);const size_t path=off[4];const unsigned n=u32(pack,path);
        std::vector<unsigned> sums{0};RageTrackCourse probes;
        for(unsigned i=0;i<n;++i){const size_t p=path+4+24*i;RageTrackFace f{};
            for(int k=0;k<4;++k){f.vertex[k]=unsigned(probes.vertices.size());probes.vertices.push_back({float(int32_t(u32(pack,p)))*4,float(int16_t(u16(pack,p+8)))*4,float(int32_t(u32(pack,p+4)))*4});}
            probes.faces.push_back(f);sums.push_back(sums.back()+u16(pack,p+22));}
        assign_world_banks(probes,pack,2+r*2);unsigned bankCount[2]={};const unsigned start=u32(pack,off[9]);
        for(unsigned i=0;i<n;++i){const unsigned progress=(sums[start+1]+sums.back()-sums[i])%sums.back();const unsigned section=progress>>8;
            const unsigned expected=section>=u32(pack,off[0]) && section<u32(pack,off[0]+4);const unsigned actual=(probes.faces[i].page>>9)-(2+r*2);
            // At an exact boundary the preceding end point can round a unit.
            if((progress&255)>2 && (progress&255)<253)assert(actual==expected);
            assert(actual<2);++bankCount[actual];}
        assert(bankCount[0]>10 && bankCount[1]>10);
    }
    decode(free_packs[0],oval,2);auto active=oval;oval={};decode(free_packs[19],oval,64);
    textures(file(fixtures/"OVAL5.1ST"));
    assert(rage_track_build_topology(active));rage_track_classify_layers(active);rage_track_stitch_geometry(active);
    RageTrackCourse world;assert(rage_free_world_build(active,world,0,0));
    assert(world.faces.size()>active.faces.size());assert(world.triangles.size()>active.triangles.size());assert(rage_free_world_ready());
    if(const char* dump=std::getenv("RAGE_FREE_WORLD_DUMP")){
        std::ofstream out(dump);out<<"face,later";for(unsigned k=0;k<4;++k)out<<",x"<<k<<",y"<<k<<",z"<<k;out<<"\n";
        for(size_t i=0;i<world.faces.size();++i){const auto& face=world.faces[i];out<<i<<","<<face.free_run_later;
            for(auto id:face.vertex)for(float v:world.vertices[id])out<<","<<v;out<<"\n";}
    }
    unsigned stripeBoards=0,chevronSamples=0,restoredWallSamples=0;
    for(const auto& f:world.faces)assert(!free_road_tile(f,160,191));
    for(const auto& f:active.faces){
      if(free_road_tile(f,160,191))++stripeBoards;
      Quad facePoints;for(int k=0;k<4;++k)facePoints[k]=point(active,f.vertex[k]);
      const bool chevron=free_road_tile(f,64,95,96,127);
      const bool oldWallRemoval=blocks_route(facePoints,world.route) && !free_world_omit(f,facePoints,world.route);
      if(!chevron && !oldWallRemoval)continue;
      // Verify the actual divider surface survives rebuilding and clipping.
      for(const auto& corner:corners){Key source;Point mid{};for(int k=0;k<3;++k){source[k]=point(active,f.vertex[corner[k]]);for(int axis=0;axis<3;++axis)mid[axis]+=source[k][axis]/3.;}
       Surface expected;if(!surface(source,expected))continue;bool covered=false;
       for(const auto& tri:world.triangles){Key points;for(int k=0;k<3;++k)points[k]=point(world,tri.vertex[k]);Surface candidate;if(!surface(points,candidate))continue;
        double distance=-candidate.d;for(int axis=0;axis<3;++axis)distance+=candidate.n[axis]*mid[axis];if(std::abs(distance)>.5)continue;
        const double sign=cross(points[0],points[1],points[2],candidate.axis)>0?1:-1;
        if(cross(points[0],points[1],mid,candidate.axis)*sign>=-.5 && cross(points[1],points[2],mid,candidate.axis)*sign>=-.5 && cross(points[2],points[0],mid,candidate.axis)*sign>=-.5){covered=true;break;}}
       assert(covered);if(chevron)++chevronSamples;else ++restoredWallSamples;
      }
    }
    assert(stripeBoards>0 && chevronSamples>0);
    std::cout<<"Removed red/white tile from world; preserved "<<chevronSamples<<" actual chevron surface samples and "<<restoredWallSamples<<" formerly filtered wall samples\n";
    assert(!free_closure_rails.empty());
    unsigned tunnelCaps=0;for(const auto& f:active.faces)if(free_tunnel_cap(f))++tunnelCaps;
    for(const auto& f:world.faces)assert(!free_tunnel_cap(f));assert(tunnelCaps>=4);
    std::cout<<"Removed "<<tunnelCaps<<" authored tunnel cap/rim faces\n";
    unsigned closurePosts=0,closureRails=0;
    for(const auto& f:active.faces){Quad q;for(int k=0;k<4;++k)q[k]=point(active,f.vertex[k]);if(free_closure_post(f,q))++closurePosts;if(free_metal_rail(f,q))++closureRails;}
    for(const auto& f:world.faces){Quad q;for(int k=0;k<4;++k)q[k]=point(world,f.vertex[k]);assert(!free_closure_post(f,q) && !free_metal_rail(f,q));}
    assert(closurePosts>0 && closureRails>0);
    // The same metal tile away from the closure must remain, as must any
    // unrelated wall, regardless of its height or angle across a route.
    RageTrackFace pole{};pole.page=0x15;pole.uv[0]=0;pole.uv[1]=15;pole.uv[2]=94<<8;pole.uv[3]=(94<<8)|15;
    const auto board=free_closure_boards.front();const auto v=board.front();
    Quad attached{{{v[0],v[1]-100,v[2]},{v[0]+10,v[1]-100,v[2]},{v[0],v[1]+100,v[2]},{v[0]+10,v[1]+100,v[2]}}};
    assert(free_world_omit(pole,attached,world.route));
    for(auto& p:attached)p[0]+=10000;assert(!free_world_omit(pole,attached,world.route));
    RageTrackFace unrelated=pole;unrelated.page=0x16;assert(!free_world_omit(unrelated,board,world.route));
    std::cout<<"Removed "<<closurePosts<<" closure-post faces and "<<closureRails<<" road-closing rail faces while preserving unrelated scenery\n";
    std::cout<<"Unified geometry: "<<world.faces.size()<<" faces, "<<world.triangles.size()<<" triangles\n";
    // Reconstructed terrain must retain the authored surfaces of tall lower-
    // road structures. Test points on their actual triangles, not just counts.
    // Elevated boards beside the lower road must stay visible even when
    // their height puts them closer to an upper section of the same course.
    for(unsigned r=0;r<3;++r){const auto offsets=chunks(free_packs[r]);const size_t path=offsets[4];
        const unsigned n=u32(free_packs[r],path);const auto& band=rage_free_tunnel_bands[r];RageTrackCourse boards;
        for(unsigned i=0;i<n;++i){if(i>band[1] && i<band[2])continue;const size_t p=path+4+24*i;
            const float x=float(int32_t(u32(free_packs[r],p)))*4,z=float(int32_t(u32(free_packs[r],p+4)))*4,y=float(int16_t(u16(free_packs[r],p+8)))*4-2048;
            RageTrackFace face{};for(unsigned k=0;k<4;++k){face.vertex[k]=unsigned(boards.vertices.size());boards.vertices.push_back({x+(k&1?32.f:-32.f),y+(k&2?128.f:-128.f),z});}boards.faces.push_back(face);}
        assign_world_banks(boards,free_packs[r],2+2*r,&band);
        for(const auto& face:boards.faces)assert(!face.free_run_later);
    }
    unsigned structureSamples=0;
    for(unsigned r=0;r<3 && structureSamples<64;++r)for(const auto& f:free_meshes[r]->faces){
        if(structureSamples>=64)break;if(f.free_run_later)continue;
        double low=1e30,high=-1e30;for(auto v:f.vertex){low=std::min(low,double(free_meshes[r]->vertices[v][1]));high=std::max(high,double(free_meshes[r]->vertices[v][1]));}
        if(high-low<=512)continue;
        for(const auto& c:corners){Key source;Point mid{};
            for(int k=0;k<3;++k){source[k]=point(*free_meshes[r],f.vertex[c[k]]);for(int axis=0;axis<3;++axis)mid[axis]+=source[k][axis]/3;}
            Surface expected;if(!surface(source,expected))continue;bool covered=false;
            for(const auto& tri:world.triangles){Key points;for(int k=0;k<3;++k)points[k]=point(world,tri.vertex[k]);Surface candidate;
                if(!surface(points,candidate))continue;double distance=-candidate.d;for(int axis=0;axis<3;++axis)distance+=candidate.n[axis]*mid[axis];if(std::abs(distance)>.5)continue;
                const double sign=cross(points[0],points[1],points[2],candidate.axis)>0?1:-1;
                if(cross(points[0],points[1],mid,candidate.axis)*sign>=-.5 && cross(points[1],points[2],mid,candidate.axis)*sign>=-.5 && cross(points[2],points[0],mid,candidate.axis)*sign>=-.5){covered=true;break;}
            }
            assert(covered);++structureSamples;
        }
    }
    assert(structureSamples>=32);std::cout<<"Preserved "<<structureSamples<<" authored structure surface samples\n";
    CPUState cpu{};cpu.read_word=rw;cpu.read_half=rh;cpu.read_byte=rb;cpu.write_word=ww;cpu.write_half=wh;cpu.write_byte=wb;
    const uint32_t car=0x8009e6d4;cpu.gpr[4]=car;cpu.gpr[31]=0x80012344;
    unsigned laterFaces=0,lowerFaces=0;
    for(const auto& face:world.faces)(face.free_run_later?laterFaces:lowerFaces)++;
    assert(laterFaces>100 && lowerFaces>100);
    for(unsigned r=0;r<4;++r){std::cout<<"Crests route "<<r<<" length "<<roads[r].length<<" origin "<<crests[r].origin;
        for(unsigned row=0;row<2;++row)for(unsigned e=0;e<8 && crests[r].motion[row][e]!=-1;++e)
            std::cout<<" ["<<row<<":"<<crests[r].progress[row][e]<<","<<crests[r].motion[row][e]<<"]";
        std::cout<<"\n";
    }
    unsigned exclusive[4]={},sharedRoutes=0;
    for(const auto& face:world.faces){assert(face.free_run_routes && face.free_run_routes<16);for(unsigned r=0;r<4;++r)if(face.free_run_routes==(1u<<r))++exclusive[r];if((face.free_run_routes&(face.free_run_routes-1))!=0)++sharedRoutes;}
    for(auto count:exclusive)assert(count>100);assert(sharedRoutes>100);
    std::cout<<"Route ownership exclusive "<<exclusive[0]<<","<<exclusive[1]<<","<<exclusive[2]<<","<<exclusive[3]<<" shared "<<sharedRoutes<<"\n";
    // Live HI tunnel-exit reproduction: an approach face must be available
    // before crossing point 46; distant overhead backdrop faces stay hidden.
    const float exitCamera[3]={47243*4.f,4203*4.f-256,25268*4.f};
    const auto& approach=world.faces.at(24822);assert(approach.free_run_later);
    bool approachAvailable=false;
    for(const auto& sector:world.sectors)if(24822>=sector.first && 24822<sector.first+sector.count)
        approachAvailable=rage_free_world_later_visible(true,sector.lo,sector.hi,exitCamera);
    assert(approachAvailable);
    const float upperLo[3]={exitCamera[0]-100,exitCamera[1]-6000,exitCamera[2]-100};
    const float upperHi[3]={exitCamera[0]+100,exitCamera[1]-5000,exitCamera[2]+100};
    assert(!rage_free_world_later_visible(true,upperLo,upperHi,exitCamera));
    assert(rage_free_world_later_visible(false,upperLo,upperHi,exitCamera));
    assert(rage_free_world_batch_visible(0,1,false,false,upperLo,upperHi,exitCamera));
    assert(!rage_free_world_batch_visible(0,2,false,false,upperLo,upperHi,exitCamera));
    assert(rage_free_world_batch_visible(0,8,true,true,upperLo,upperHi,exitCamera));
    assert(rage_free_world_batch_visible(1,10,true,true,upperLo,upperHi,exitCamera));
    assert(rage_free_world_batch_visible(1,3,false,false,upperLo,upperHi,exitCamera));
    const float nearLo[3]={exitCamera[0]-100,exitCamera[1]-100,exitCamera[2]-100},nearHi[3]={exitCamera[0]+100,exitCamera[1]+100,exitCamera[2]+100};
    assert(!rage_free_world_batch_visible(0,2,false,false,nearLo,nearHi,exitCamera));
    assert(rage_free_world_batch_visible(0,2,true,true,upperLo,upperHi,exitCamera,true));
    assert(rage_free_world_batch_visible(2,1,true,true,upperLo,upperHi,exitCamera,true));
    // Fixed, path-distance aprons survive every active route and travel direction.
    assert(rage_free_world_portal_apron(1000,1000,10000));
    assert(rage_free_world_portal_apron(1640,1000,10000));
    assert(!rage_free_world_portal_apron(1641,1000,10000));
    assert(rage_free_world_portal_apron(9360,1000,10000));
    assert(!rage_free_world_portal_apron(9359,1000,10000));
    assert(rage_free_world_portal_apron(1700,1000,10000,100));
    assert(!rage_free_world_portal_apron(5000,1000,10000,100));
    unsigned apronFaces=0,hiddenFaces=0;
    for(const auto& f:world.faces){apronFaces+=f.free_run_portal!=0;hiddenFaces+=f.free_run_later && !f.free_run_portal;}
    assert(apronFaces>0 && hiddenFaces>0);
    std::cout<<"Persistent portal faces "<<apronFaces<<", filtered interior "<<hiddenFaces<<"\n";
    for(unsigned r=0;r<3;++r)for(unsigned other=0;other<3;++other)if(r!=other){
        assert(rage_free_world_batch_visible(r,1u<<other,true,true,upperLo,upperHi,exitCamera,false,true));
        assert(!rage_free_world_batch_visible(r,1u<<other,false,false,upperLo,upperHi,exitCamera,false,false));
    }
    for(unsigned r=0;r<3;++r){
        current=r;const auto& b=rage_free_tunnel_bands[r];
        std::cout<<"Route "<<r<<" tunnel visibility boundaries "<<b[0]<<","<<b[1]<<","<<b[2]<<","<<b[3]<<"\n";
        for(unsigned portal=0;portal<2;++portal){const unsigned first=b[portal*2],last=b[portal*2+1];
            assert(!rage_free_world_tunnel_window(first-2,b));
            assert(rage_free_world_tunnel_window(first-1,b));
            assert(rage_free_world_tunnel_window(first,b));
            assert(rage_free_world_tunnel_window(last,b));
            assert(rage_free_world_tunnel_window(last+1,b));
            assert(!rage_free_world_tunnel_window(last+2,b));
            for(int direction:{-1,1})for(int offset=-2;offset<=2;++offset){
                const unsigned index=unsigned(int(direction>0?first:last)+direction*offset);
                const bool open=rage_free_world_tunnel_window(index,b);
                for(unsigned other=0;other<3;++other)if(other!=r)
                    assert(rage_free_world_batch_visible(r,1u<<other,false,false,upperLo,upperHi,exitCamera,open)==open);
            }
        }
        assert(b[0]<b[1] && b[1]<b[2] && b[2]<b[3] && b[3]<roads[r].count);
        for(unsigned i:{0u,b[1],b[2],roads[r].count-1}){
            ww(car+0x30,i);assert(rage_free_world_hide_later(&cpu));
        }
        ww(car+0x30,(b[1]+b[2])/2);assert(!rage_free_world_hide_later(&cpu));
    }
    current=0;
    ww(0x801e42e4,12);ww(0x8009e688,0x80150000);ww(0x8009e6a8,333);ww(0x8019c7d0,0x80152000);ww(0x801e40d8,100000);
    // A live oval run crossed a starting-course crest event at (55442,
    // 5822,1403), then stayed at Y=5820 underneath the rising bank. The
    // event lookup must not launch from this unrelated lap coordinate.
    for(unsigned region=0;region<3;++region){rage_active_region=region;
        const auto player=rage_region_address(car),scene=rage_region_address(0x801e42e4);
        cpu.gpr[4]=player;cpu.gpr[2]=123;cpu.pc=0x80039184;ww(scene,12);
        ww(player,55442);ww(player+4,5822);ww(player+8,1403);wh(player+0x98,0);
        assert(rage_region_code(rage_region_address(0x80039184))==0x39184);
        assert(rage_free_world_dispatch(&cpu,0x39184)==1);
        assert(cpu.gpr[2]==0 && cpu.pc==cpu.gpr[31]);
        assert(rw(player+4)==5822 && rh(player+0x98)==0);
        // Grand Prix, other cars and non-race scenes keep retail behavior.
        freeRunActive=false;cpu.gpr[2]=123;cpu.pc=0x80039184;
        assert(!rage_free_world_dispatch(&cpu,0x39184) && cpu.gpr[2]==123 && cpu.pc==0x80039184);
        freeRunActive=true;ww(scene,8);assert(!rage_free_world_dispatch(&cpu,0x39184));
        ww(scene,12);cpu.gpr[4]=player+0x200;assert(!rage_free_world_dispatch(&cpu,0x39184));
    }
    rage_active_region=0;cpu.gpr[4]=car;
    // Every authored crest is matched in its own route coordinates, in both
    // directions, without touching the guest's rescaled lap bookkeeping.
    unsigned matchedCrests=0;
    for(unsigned r=0;r<3;++r)for(unsigned row=0;row<2;++row)for(unsigned e=0;e<8 && crests[r].motion[row][e]!=-1;++e){
        current=r;crest_history={};const auto& road=roads[r];const int event=crests[r].progress[row][e];
        for(unsigned step=0;step<2;++step){const int progress=event+(row?1:-1)*(step? -80:80);
            const unsigned local=(crests[r].origin+road.length-unsigned(progress))%road.length;
            unsigned i=0;while(i+1<road.count && road.prefix[i+1]<=local)++i;
            const auto point=road_point(road,i);const double distance=local-road.prefix[i],angle=point.angle*6.283185307179586/4096.;
            ww(car,uint32_t(std::lround(point.x+distance*std::cos(angle))));ww(car+8,uint32_t(std::lround(point.z+distance*std::sin(angle))));
            ww(car+0x60,uint32_t(point.y));ww(car+0x30,i);ww(car+0xa4,1000);ww(0x8009e690,step+1);
            assert(rage_free_world_dispatch(&cpu,0x39184)==1);
            if(step){if(int32_t(cpu.gpr[2])!=crests[r].motion[row][e])std::cerr<<"Crest mismatch "<<r<<","<<row<<","<<e<<" got "<<int32_t(cpu.gpr[2])<<"\n";
                assert(int32_t(cpu.gpr[2])==crests[r].motion[row][e]);++matchedCrests;
            }else assert(cpu.gpr[2]==0);
        }
    }
    assert(matchedCrests>=20);std::cout<<"Matched "<<matchedCrests<<" route-local authored crests\n";
    std::array<std::array<double,3>,4> samples{};
    for(unsigned r=0;r<4;++r){const auto& road=roads[r];double distance=-1;
        for(unsigned i=0;i<road.count;++i){size_t p=4+24*i;double x=int32_t(word(road.bytes,p)),y=int16_t(half(road.bytes,p+8)),z=int32_t(word(road.bytes,p+4));
            double nearest=1e30;for(unsigned other=0;other<4;++other)if(other!=r)nearest=std::min(nearest,closest(x,y,z,false,other).distance);
            if(nearest>distance){distance=nearest;samples[r]={x,y,z};}
        }
        std::cout<<"Route "<<r<<": "<<road.count<<" points; independent stretch "<<distance<<"\n";assert(distance>100);
    }
    for(unsigned r:{0u,1u,2u,3u,0u}){const auto& p=samples[r];ww(car,uint32_t(p[0]));ww(car+4,0x80078194);ww(car+0x60,uint32_t(p[1]));ww(car+8,uint32_t(p[2]));
        assert(rage_free_world_dispatch(&cpu,0x2c168)==1);assert(rage_free_world_current_route()==r);
        assert(rw(0x8009e6a8)==roads[r].count && rw(0x8009e688)==roads[r].guest+4);
        assert(rw(car+0x30)<roads[r].count && rw(0x801e40d8)==100000);
        assert(rage_free_world_dispatch(&cpu,0x30eb4)==0); // Stock segment search remains authoritative.
    }
    // Collision-time lookup must replace a stale segment argument before the
    // retail boundary routine can push the player back onto the old course.
    const uint32_t limits=0x80170000,hull=0x8007dab0;
    const int corners[4][2]={{-15,20},{15,20},{-8,-10},{8,-10}};
    for(unsigned i=0;i<4;++i){wh(hull+4*i,uint16_t(corners[i][0]));wh(hull+4*i+2,uint16_t(corners[i][1]));}
    wh(limits,60);wh(limits+2,uint16_t(-60));ww(limits+4,0x00040003);
    for(unsigned r=0;r<4;++r){current=(r+1)%4;const auto& p=samples[r];
        ww(car,uint32_t(p[0]));ww(car+8,uint32_t(p[2]));ww(car+0x60,uint32_t(p[1]));
        ww(car+16,0);ww(car+24,0);cpu.gpr[5]=9999;cpu.gpr[6]=limits;cpu.pc=0x80031298;
        assert(rage_free_world_dispatch(&cpu,0x31298)==0);
        assert(current==r && cpu.gpr[5]<roads[r].count && cpu.pc==0x80031298);
        assert(rw(limits)==0xffc4003c && rw(limits+4)==0x00040003);
    }
    unsigned junctions=0;
    for(unsigned r=0;r<4 && junctions<6;++r)for(unsigned i=0;i<roads[r].count && junctions<6;++i){
        const auto a=road_point(roads[r],i);const double angle=a.angle*6.283185307179586/4096.;
        for(double lateral:{-a.left+10,a.right-10}){
            const double x=a.x-std::sin(angle)*lateral,z=a.z+std::cos(angle)*lateral;
            const double y=contact(roads[r],i,x,z).height;
            ww(car+0x24,uint32_t(3072-int(a.angle)));
            if(!junction_footprint(&cpu,car,x,y,z,r))continue;
            // Removing the neighbouring road makes this a real outside edge.
            auto saved=roads;for(unsigned other=0;other<4;++other)if(other!=r)roads[other].count=0;
            assert(!junction_footprint(&cpu,car,x,y,z,r));roads=std::move(saved);
            ++junctions;break;
        }
    }
    assert(junctions>0);std::cout<<"Verified "<<junctions<<" physical junction openings with outside-edge protection\n";
    assert(rage_free_world_switches()>=4);
    // Recorded live corrections on HI, the stadium straight and MID. All
    // four hull corners had pavement support, so no wall contact is valid.
    for(const auto& sample:std::array<std::array<double,5>,3>{{
        {22056,5126,23012,21.158197007306647,2},
        {49958,6000,11760,20.782371714274667,0},
        {39929,2549,40733,16.991905187409248,1}}}){
        ww(car+0x24,uint32_t(std::lround(sample[3]*4096/6.283185307179586)));
        const auto h=closest(sample[0],sample[1],sample[2],true,unsigned(sample[4]));
        int right,left;uint16_t contacts[2]={99,99};
        assert(junction_corner_limits(&cpu,car,sample[0],sample[1],sample[2],h.road,h.index,right,left,contacts));
        assert(contacts[0]==0 && contacts[1]==0);
    }
    // Every actual road point remains covered by a correctly elevated road;
    // sharing a horizontal position with a bridge cannot select its underside.
    for(unsigned r=0;r<4;++r){const auto& road=roads[r];
        for(unsigned i=0;i<road.count;++i){const size_t p=4+24*i;
            const Hit h=closest(int32_t(word(road.bytes,p)),int16_t(half(road.bytes,p+8)),int32_t(word(road.bytes,p+4)),true,r);
            assert(h.distance<1 && h.score<1);
        }
    }
    // Follow every network continuously through shared stretches and exits.
    // The primary height contains retail contact scratch, not an altitude.
    for(unsigned r=0;r<4;++r){const auto& road=roads[r];
        for(unsigned i=0;i<road.count;++i){const size_t p=4+24*i,q=4+24*((i+1)%road.count);
            for(unsigned step=0;step<4;++step){double t=step*.25;
                double x=int32_t(word(road.bytes,p))*(1-t)+int32_t(word(road.bytes,q))*t;
                double z=int32_t(word(road.bytes,p+4))*(1-t)+int32_t(word(road.bytes,q+4))*t;
                const auto a=road_point(road,i),b=road_point(road,i+1);
                if(a.arc&3){const size_t c=4+24*road.count+12*(int16_t(a.arc)>>4);const double cx=int32_t(word(road.bytes,c)),cz=int32_t(word(road.bytes,c+4));
                    const double start=std::atan2(a.z-cz,a.x-cx),end=std::atan2(b.z-cz,b.x-cx),angle=start+rage_angle_delta(start,end)*t;
                    const double radius=std::hypot(a.x-cx,a.z-cz)*(1-t)+std::hypot(b.x-cx,b.z-cz)*t;x=cx+std::cos(angle)*radius;z=cz+std::sin(angle)*radius;}
                const double y=contact(road,i,x,z).height;
                ww(car,uint32_t(int32_t(x)));ww(car+8,uint32_t(int32_t(z)));ww(car+4,0x80078194);ww(car+0x60,uint32_t(int32_t(y)));
                assert(rage_free_world_dispatch(&cpu,0x2c168));
                const auto h=closest(x,y,z,false,current);
                const auto roadContact=contact(roads[current],rw(car+0x30),x,z);
                if(roadContact.clearance<35 || std::abs(roadContact.height-y)>80)std::cerr<<"road "<<r<<" segment "<<i<<" step "<<step<<" selected "<<current<<":"<<h.index<<" clearance "<<roadContact.clearance<<" height "<<roadContact.height-y<<"\n";
                assert(roadContact.clearance>=35 && std::abs(roadContact.height-y)<=80);
                assert(rw(car+0x30)<roads[current].count);
            }
        }
    }
    if(const char* dump=std::getenv("RAGE_FREE_WORLD_LANES")){
        std::ofstream out(dump);out<<"road,index,t,lane,direction,x,y,z,selected,segment,selected_y,clearance\n";
        for(unsigned r=0;r<4;++r)for(unsigned i=0;i<roads[r].count;++i){const auto a=road_point(roads[r],i),b=road_point(roads[r],i+1);
            for(double t:{0.,.5}){double cx=a.x+(b.x-a.x)*t,cz=a.z+(b.z-a.z)*t;
                if(a.arc&3){const size_t p=4+24*roads[r].count+12*(int16_t(a.arc)>>4);const double ox=int32_t(word(roads[r].bytes,p)),oz=int32_t(word(roads[r].bytes,p+4));
                    const double start=std::atan2(a.z-oz,a.x-ox),end=std::atan2(b.z-oz,b.x-ox),angle=start+rage_angle_delta(start,end)*t;
                    const double radius=std::hypot(a.x-ox,a.z-oz)*(1-t)+std::hypot(b.x-ox,b.z-oz)*t;cx=ox+std::cos(angle)*radius;cz=oz+std::sin(angle)*radius;}
                const double heading=(a.angle+std::remainder(b.angle-a.angle,4096.)*t)*6.283185307179586/4096.;
                for(double lane:{-.85,-.45,0.,.45,.85}){const double offset=lane*(lane<0?a.left:a.right),x=cx-std::sin(heading)*offset,z=cz+std::cos(heading)*offset;
                    const auto surface=contact(roads[r],i,x,z);
                    for(int direction:{-1,1}){const double mx=direction*160*std::cos(heading),mz=direction*160*std::sin(heading);
                        const double previousY=contact(roads[r],i,x-mx,z-mz).height;
                        const auto hit=closest(x,previousY,z,true,r,mx,mz);const auto selected=contact(roads[hit.road],hit.index,x,z);
                        out<<r<<","<<i<<","<<t<<","<<lane<<","<<direction<<","<<x<<","<<surface.height<<","<<z<<","<<hit.road<<","<<hit.index<<","<<selected.height<<","<<selected.clearance<<"\n";}
                }
            }
        }
    }
    // Every ring shares a physical, equally elevated junction with BIG.
    for(unsigned r=1;r<4;++r){unsigned shared=0;
        for(unsigned i=0;i<roads[r].count;++i){const size_t p=4+24*i;
            const auto h=closest(int32_t(word(roads[r].bytes,p)),int16_t(half(roads[r].bytes,p+8)),int32_t(word(roads[r].bytes,p+4)),false,0);
            if(h.distance<1 && h.score<1)++shared;
        }
        assert(shared>10);
    }
    // Two adjacent road corridors form an open junction for a straddling
    // hull. A missing road or a bridge at another height must not open it.
    for(unsigned r=0;r<4;++r){auto& road=roads[r];road={};if(r>=2)continue;
        road.count=16;road.length=1600;road.bytes.resize(4+24*16);put(road.bytes,0,16);
        for(unsigned i=0;i<16;++i){const size_t p=4+24*i;put(road.bytes,p,i*100);put(road.bytes,p+4,r*200);
            put(road.bytes,p+16,100,2);put(road.bytes,p+18,100,2);put(road.bytes,p+22,100,2);road.prefix.push_back(i*100);}}
    current=0;ww(car,500);ww(car+8,95);ww(car+0x60,0);ww(car+0x24,0);ww(car+16,0);ww(car+24,0);
    cpu.gpr[6]=limits;cpu.gpr[5]=9999;
    assert(rage_free_world_dispatch(&cpu,0x31298)==0 && cpu.gpr[5]<16);
    assert(cpu.gpr[6]!=limits && int16_t(rh(cpu.gpr[6]))<=3);
    assert(rw(limits)==0xffc4003c && rw(cpu.gpr[6]+4)==0);
    // One outside corner must retain its wall collision without making the
    // opposite, road-supported corner hit an invisible junction wall too.
    const int partialHull[4][2]={{-15,-40},{15,40},{-8,-10},{8,10}};
    for(unsigned i=0;i<4;++i){wh(hull+4*i,uint16_t(partialHull[i][0]));wh(hull+4*i+2,uint16_t(partialHull[i][1]));}
    ww(car+8,0);int rightLimit,leftLimit;assert(!junction_footprint(&cpu,car,500,0,0,0));
    uint16_t retainedContacts[2];
    assert(junction_corner_limits(&cpu,car,500,0,0,0,5,rightLimit,leftLimit,retainedContacts));
    assert(rightLimit==40 && leftLimit==-160); // Only the unsupported left corner collides.
    assert(retainedContacts[0]==4 && retainedContacts[1]==1);
    for(unsigned i=0;i<4;++i){wh(hull+4*i,uint16_t(corners[i][0]));wh(hull+4*i+2,uint16_t(corners[i][1]));}
    ww(car+8,95);
    for(unsigned i=0;i<16;++i)put(roads[1].bytes,4+24*i+8,uint16_t(-1000),2);
    cpu.gpr[6]=limits;assert(!junction_footprint(&cpu,car,500,0,95,0));
    assert(rage_free_world_dispatch(&cpu,0x31298)==0 && cpu.gpr[6]==limits);
    roads[1].count=0;cpu.gpr[6]=limits;
    assert(rage_free_world_dispatch(&cpu,0x31298)==0 && cpu.gpr[6]==limits);
    std::cout<<"All four road networks remain drivable; switching and height separation passed\n";
}
