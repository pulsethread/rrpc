#include "rage_scene_pose_math.h"
#include <cassert>
static bool close(float a,float b){return std::abs(a-b)<.2f;}
int main(){
    assert(rage_scene_math::vehicle_ground_delta(104,100)==-16);
    assert(rage_scene_math::vehicle_ground_delta(100,100)==0);
    assert(rage_scene_math::vehicle_ground_delta(84,100)==0);
    RageSceneVertex body={};body.history=1;body.flags=RAGE_SCENE_VEHICLE;
    const int16_t first[9]={4096,0,0,0,4096,0,0,0,4096};
    const int16_t second[9]={0,0,-4096,0,4096,0,4096,0,0};
    std::memcpy(body.previous.r,first,18);std::memcpy(body.current.r,second,18);
    std::memcpy(body.vehicle_view,first,18);std::memcpy(body.vehicle_view+9,second,18);
    body.camera[2]=-1000;body.camera[3]=-1000;
    body.previous.t[2]=body.current.t[2]=1000;
    float result[12];rage_scene_math::native_matrix(body,.5,result);
    // Stationary world origin, camera half way round a 90-degree orbit.
    assert(close(result[3],0));assert(close(result[7],0));assert(close(result[11],707.1068f));
    RageSceneVertex wheel=body;wheel.previous.t[0]=200;wheel.previous.t[1]=-50;
    wheel.current.t[1]=-50;wheel.current.t[2]=1200;
    float rolling[12];rage_scene_math::native_matrix(wheel,.5,rolling);
    assert(close(rolling[3]-result[3],141.4214f));
    assert(close(rolling[7]-result[7],-50));
    assert(close(rolling[11]-result[11],141.4214f));
    // Exact endpoints preserve the authored fixed-point transform.
    rage_scene_math::native_matrix(wheel,0,rolling);assert(rolling[3]==200 && rolling[11]==1000);
    rage_scene_math::native_matrix(wheel,1,rolling);assert(rolling[3]==0 && rolling[11]==1200);
    wheel.history=0;rage_scene_math::native_matrix(wheel,.5,rolling);assert(rolling[11]==1200);
}
