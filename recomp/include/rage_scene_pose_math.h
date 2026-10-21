#pragma once
#include "rage_scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace rage_scene_math {
inline double vehicle_ground_delta(int32_t y,int32_t ground){return 4.0*(std::min(double(y),double(ground))-double(y));}
inline void quaternion(const double m[9],double q[4]) {
    const double trace=m[0]+m[4]+m[8];
    if(trace>0) { double s=std::sqrt(trace+1)*2; q[0]=s/4;q[1]=(m[7]-m[5])/s;q[2]=(m[2]-m[6])/s;q[3]=(m[3]-m[1])/s; }
    else {
        int i=m[4]>m[0]?1:0; if(m[8]>m[3*i+i])i=2;
        int j=(i+1)%3,k=(i+2)%3;
        double s=std::sqrt(std::max(0.0,1+m[3*i+i]-m[3*j+j]-m[3*k+k]))*2;
        q[0]=(m[3*k+j]-m[3*j+k])/s; q[i+1]=s/4;
        q[j+1]=(m[3*j+i]+m[3*i+j])/s; q[k+1]=(m[3*k+i]+m[3*i+k])/s;
    }
    double length=0; for(int i=0;i<4;++i)length+=q[i]*q[i];length=std::sqrt(length);
    for(int i=0;i<4;++i)q[i]/=length;
}
inline double determinant(const double m[9]) {
    return m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
}
inline void native_matrix(const RageSceneVertex& v,double t,float out[12]) {
    if(!v.history)t=1; t=std::max(0.0,std::min(t,1.0));
    if((v.flags&RAGE_SCENE_VEHICLE) && t>0 && t<1){
        // Reference Render World interpolates each car part in world space,
        // then applies the camera. Interpolating R*model and -R*C together
        // otherwise makes wheel centres orbit as the chase camera turns.
        RageSceneVertex object=v,view={};object.flags&=~(RAGE_SCENE_VEHICLE|RAGE_SCENE_CAMERA);
        object.flags|=RAGE_SCENE_FLOAT_TRANSLATION;view.history=v.history;
        bool valid=true;
        for(int endpoint=0;endpoint<2;++endpoint){
            const int16_t* vr=v.vehicle_view+9*endpoint;double m[9];for(int i=0;i<9;++i)m[i]=vr[i]/4096.;
            const double d=determinant(m);if(std::abs(d)<.5){valid=false;break;}
            double inverse[9];for(int r=0;r<3;++r)for(int c=0;c<3;++c)
                inverse[3*r+c]=(m[3*((c+1)%3)+(r+1)%3]*m[3*((c+2)%3)+(r+2)%3]-m[3*((c+1)%3)+(r+2)%3]*m[3*((c+2)%3)+(r+1)%3])/d;
            const PSXMotionMatrix& original=endpoint?v.current:v.previous;
            PSXMotionMatrix& world=endpoint?object.current:object.previous;
            PSXMotionMatrix& camera=endpoint?view.current:view.previous;std::memcpy(camera.r,vr,18);
            for(int r=0;r<3;++r){
                double position=v.camera[3*endpoint+r];
                for(int c=0;c<3;++c)position+=inverse[3*r+c]*((v.flags&RAGE_SCENE_FLOAT_TRANSLATION)?v.translation[3*endpoint+c]:original.t[c]);
                object.translation[3*endpoint+r]=float(position);
                for(int c=0;c<3;++c){double value=0;for(int k=0;k<3;++k)value+=inverse[3*r+k]*original.r[3*k+c];
                    world.r[3*r+c]=int16_t(std::lround(std::max(-32768.,std::min(value,32767.))));}
            }
        }
        if(valid){float world[12],camera[12];native_matrix(object,t,world);native_matrix(view,t,camera);
            for(int r=0;r<3;++r){for(int c=0;c<3;++c){out[4*r+c]=0;for(int k=0;k<3;++k)out[4*r+c]+=camera[4*r+k]*world[4*k+c];}
                double tr=0;for(int k=0;k<3;++k)tr+=camera[4*r+k]*(world[4*k+3]-(v.camera[k]+(v.camera[3+k]-v.camera[k])*t));out[4*r+3]=float(tr);}
            return;
        }
    }
    double a[9],b[9];
    for(int i=0;i<9;++i){a[i]=v.previous.r[i]/4096.0;b[i]=v.current.r[i]/4096.0;}
    if(!v.history)std::memcpy(a,b,sizeof a);
    // Reflection belongs to the mesh basis, not the quaternion rotation.
    const bool mirror=determinant(b)<0;
    const bool same_basis=(determinant(a)<0)==mirror;
    if(mirror)for(int i=6;i<9;++i){a[i]=-a[i];b[i]=-b[i];}
    double scale_a[3],scale_b[3]; bool regular=true;
    for(int col=0;col<3;++col) {
        scale_a[col]=std::sqrt(a[col]*a[col]+a[3+col]*a[3+col]+a[6+col]*a[6+col]);
        scale_b[col]=std::sqrt(b[col]*b[col]+b[3+col]*b[3+col]+b[6+col]*b[6+col]);
        regular&=scale_a[col]>1e-8 && scale_b[col]>1e-8;
    }
    if(!regular) {
        for(int row=0;row<3;++row){for(int col=0;col<3;++col)out[4*row+col]=v.current.r[3*row+col]/4096.0f;out[4*row+3]=(float)v.current.t[row];}
        return;
    }
    for(int row=0;row<3;++row)for(int col=0;col<3;++col){a[3*row+col]/=scale_a[col];b[3*row+col]/=scale_b[col];}
    double qa[4],qb[4],q[4]; quaternion(a,qa);quaternion(b,qb);
    double dot=0;for(int i=0;i<4;++i)dot+=qa[i]*qb[i];
    if(dot<0){dot=-dot;for(int i=0;i<4;++i)qb[i]=-qb[i];}
    dot=std::min(1.0,dot);const double angle=std::acos(dot);
    double wa=1-t,wb=t;if(angle>1e-8){wa=std::sin((1-t)*angle)/std::sin(angle);wb=std::sin(t*angle)/std::sin(angle);}
    if(!same_basis){wa=0;wb=1;t=1;}
    for(int i=0;i<4;++i)q[i]=wa*qa[i]+wb*qb[i];
    const double w=q[0],x=q[1],y=q[2],z=q[3];
    const double r[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),
        2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
    for(int row=0;row<3;++row) {
        for(int col=0;col<3;++col)out[4*row+col]=(float)(r[3*row+col]*(mirror&&row==2?-1:1)*(scale_a[col]+(scale_b[col]-scale_a[col])*t));
        const double ta=(v.flags&RAGE_SCENE_FLOAT_TRANSLATION)?v.translation[row]:v.previous.t[row];
        const double tb=(v.flags&RAGE_SCENE_FLOAT_TRANSLATION)?v.translation[3+row]:v.current.t[row];
        out[4*row+3]=(float)(ta+(tb-ta)*t);
    }
    if((v.flags&RAGE_SCENE_CAMERA) && t>0 && t<1) {
        // Interpolate camera position in world space. Interpolating -R*C
        // independently of R rotates the scene around the world's origin.
        for(int row=0;row<3;++row){double tr=0;
            for(int col=0;col<3;++col)tr-=out[4*row+col]*(v.camera[col]+(v.camera[3+col]-v.camera[col])*t);
            out[4*row+3]=(float)tr;
        }
    }
    // Exact authored endpoint retains legitimate fixed-point scale/shear.
    if(t==1||t==0)for(int row=0;row<3;++row)for(int col=0;col<3;++col)
        out[4*row+col]=(t==1?v.current:v.previous).r[3*row+col]/4096.0f;
}
}
