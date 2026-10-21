#include "rage_car_lamps.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>
int main(){
    RageCarLightState s{};
    for(int i=0;i<10;++i)rage_car_light_update(&s,1,0,.04f);
    assert(s.head==0 && s.tail==0 && s.stop==0);
    // A brief bridge shadow cannot flash the lamps; a sustained tunnel does.
    for(int i=0;i<10;++i)rage_car_light_update(&s,.25f,0,.04f);
    assert(!s.automatic && s.head==0);
    rage_car_light_update(&s,1,0,.04f);assert(s.dark==0);
    for(int i=0;i<21;++i)rage_car_light_update(&s,.25f,0,.04f);
    assert(s.automatic && s.head==1 && std::abs(s.tail-.4f)<.0001f);
    rage_car_light_update(&s,.32f,0,.04f);assert(s.automatic);
    rage_car_light_update(&s,1,1,.04f);assert(!s.automatic && s.stop==1 && s.head==0 && s.tail==0);
    rage_car_light_update(&s,1,0,.04f);assert(s.stop==0 && s.head==0 && s.tail==0);
    rage_car_light_update(&s,1,0,.08f);assert(s.stop==0);
    auto before=s;rage_car_light_update(&s,0,1,std::numeric_limits<float>::quiet_NaN());assert(s.head==before.head && s.stop==before.stop);
    RageCarLightReceipt r{};r.head=1;r.tail=.4f;r.stop=1;r.speed=0;assert(rage_car_trail_strength(&r)==1);
    r.speed=160;assert(rage_car_trail_strength(&r)==1);r.head=0;assert(rage_car_trail_strength(&r)==0);
    assert(rage_car_lamp_strength(&r,LAMP_TAIL_STOP)==1);
    for(unsigned asset=10;asset<=72;asset+=2){const RageCarLamp* lamps=nullptr;unsigned n=rage_car_lamps(asset,1,0,&lamps);assert(n>=4 && n<=6);
        unsigned rear=0,front=0;for(unsigned i=0;i<n;++i){const auto& lamp=lamps[i];assert(lamp.bounds[0]<lamp.bounds[2] && lamp.bounds[1]<lamp.bounds[3]);
            assert(std::isfinite(lamp.position.x) && std::isfinite(lamp.position.y) && std::isfinite(lamp.position.z));
            if(lamp.kind==LAMP_HEAD)++front;else ++rear;}assert(front>=2 && rear>=2);
        assert(rage_car_lamps(asset,1,1,&lamps)==0);}
    for(unsigned bank: {88u,96u,104u,112u,120u})for(unsigned mesh=0;mesh<=30;mesh+=5){const RageCarLamp* lamps=nullptr;assert(rage_car_lamps(bank,0,mesh,&lamps)==4);}
    const RageCarLamp* lamps=nullptr;assert(rage_car_lamps(128,0,4,&lamps)==4);assert(rage_car_lamps(1,0,0,&lamps)==0);
}
