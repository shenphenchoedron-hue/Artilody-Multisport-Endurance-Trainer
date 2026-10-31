#pragma once
#include "route.h"

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {
struct GhostPose {
    Point center;
    double fx=0,fy=0,fz=-1,lean=0,crank=0,wheel=0;
};
inline GhostPose ghostPose(const Route& route,double distance,double speedKmh){
    GhostPose pose;double d=std::min(route.length(),distance+3);
    pose.center=route.at(d);
    auto before=route.at(d-5),after=route.at(d+5);
    double dx=after.x-before.x,dy=after.y-before.y,dz=after.z-before.z;
    double horizontal=std::hypot(dx,dz),length=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(length>1e-6){pose.fx=dx/length;pose.fy=dy/length;pose.fz=dz/length;}
    if(horizontal>1e-6){pose.center.x-=dz/horizontal*1.4;pose.center.z+=dx/horizontal*1.4;}
    // on the asphalt (the road's surface is 2.5 cm above the route line)
    pose.center.y+=.028;
    auto a=route.at(d-12),b=route.at(d),c=route.at(d+12);
    double ax=b.x-a.x,az=b.z-a.z,bx=c.x-b.x,bz=c.z-b.z;
    double la=std::hypot(ax,az),lb=std::hypot(bx,bz);
    if(la>1&&lb>1){
        double curve=std::atan2(ax*bz-az*bx,ax*bx+az*bz)/((la+lb)*.5);
        double speed=std::clamp(speedKmh,0.0,120.0)/3.6;
        pose.lean=std::clamp(std::atan(speed*speed*curve/9.81),-.40,.40);
    }
    // Distance-based default (the app replaces crank with the real cadence,
    // GhostRider::build); the wheels always follow the distance.
    pose.crank=d*6.283185307/6.0;
    pose.wheel=-d/.34;
    return pose;
}

} // namespace route3d
