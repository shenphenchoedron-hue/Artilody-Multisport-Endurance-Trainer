#pragma once
#include "terrain.h"
#include "world_noise.h"
#include "settlement.h"
#include <vector>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// Natural-looking vegetation placement: trees grow in forest patches defined by
// world-space noise, with scattered solitary trees and bushes in open fields.
// Placement is on a jittered world grid, so it is deterministic per position.
enum TreeKind {Spruce=0, Broadleaf=1, Bush=2};
struct TreeSpec {double x=0,y=0,z=0,height=0,radius=0;int kind=Spruce;uint32_t seed=0;};

namespace Vegetation {
constexpr double cell=10;          // metres between candidate positions
constexpr double roadClearance=8;  // from road centre line to crown edge
inline double forestDensity(double x,double z){
    return Landuse::forest(x,z);
}
inline std::vector<TreeSpec> scatter(const Terrain& terrain,double cx,double cz,double radius,const std::vector<Building>* buildings=nullptr){
    std::vector<TreeSpec> out;
    int x0=int(std::floor((cx-radius)/cell)),x1=int(std::floor((cx+radius)/cell));
    int z0=int(std::floor((cz-radius)/cell)),z1=int(std::floor((cz+radius)/cell));
    for(int ix=x0;ix<=x1;++ix)for(int iz=z0;iz<=z1;++iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,k);};
        double x=(ix+.1+.8*r(1))*cell,z=(iz+.1+.8*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius)continue;
        double forest=forestDensity(x,z);
        if(r(3)>.035+.80*forest)continue;
        TreeSpec t;t.x=x;t.z=z;t.seed=WorldNoise::hash(ix,iz,77);
        double kindRoll=r(4),size=r(5);
        if(forest>.5)t.kind=kindRoll<.62?Spruce:Broadleaf;
        else t.kind=kindRoll<.40?Bush:(kindRoll<.80?Broadleaf:Spruce);
        if(t.kind==Spruce){t.height=9+8*size;t.radius=t.height*.25;}
        else if(t.kind==Broadleaf){t.height=7+7*size;t.radius=t.height*.33;}
        else{t.height=1.3+1.6*size;t.radius=t.height*.65;}
        if(buildings&&Settlement::blocked(*buildings,x,z,t.radius*(t.kind==Bush?.5:.7)))continue;
        if(terrain.nearest(x,z).distance<roadClearance+t.radius)continue;
        t.y=terrain.surface(x,z)-.15; // sink the trunk slightly into the ground
        out.push_back(t);
    }
    return out;
}
}

} // namespace route3d
