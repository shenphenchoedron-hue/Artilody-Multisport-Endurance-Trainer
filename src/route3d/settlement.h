#pragma once
#include "terrain.h"
#include "world_noise.h"
#include <vector>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// Land use shared by vegetation and buildings, so houses never stand in forest.
namespace Landuse {
inline double forest(double x,double z){
    return WorldNoise::smoothstep(.50,.62,WorldNoise::fbm(x/300,z/300,3,11));
}
// Hamlets: areas where farms and houses cluster along the road.
inline double hamlet(double x,double z){
    return WorldNoise::smoothstep(.50,.64,WorldNoise::fbm(x/650,z/650,3,91));
}
}

// Danish countryside buildings: detached houses, farm longhouses with a barn.
// Placement is a jittered 50 m world grid (deterministic per position), set back
// 16-75 m from the road, facing it, on fairly level ground and outside forest.
enum BuildingKind {House=0, Longhouse=1, Barn=2};
struct Building {
    double x=0,z=0;      // footprint centre
    double fx=0,fz=1;    // unit direction towards the road (front)
    double length=10,width=7,wall=2.7,pitch=.85; // pitch = ridge rise / half width
    double ground=0,floor=0;                     // lowest terrain under footprint, floor level
    int kind=House,roof=0,walls=0;uint32_t seed=0;
    // Long axis is perpendicular to the front direction.
    double ux()const{return -fz;} double uz()const{return fx;}
    double radius()const{return .5*std::hypot(length,width);}
};

namespace Settlement {
constexpr double cell=50;
constexpr double minSetback=16,maxSetback=75;

inline bool place(const Terrain& terrain,Building& b){
    double top=-1e9,low=1e9;
    for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j){
        double x=b.x+b.ux()*i*b.length*.5+b.fx*j*b.width*.5,z=b.z+b.uz()*i*b.length*.5+b.fz*j*b.width*.5;
        double h=terrain.surface(x,z);top=std::max(top,h);low=std::min(low,h);
        if((i||j)&&terrain.nearest(x,z).distance<9)return false; // keep clear of asphalt and verge
    }
    if(top-low>2.4)return false; // too steep for a house
    b.ground=low;b.floor=top+.2;return true;
}

// Buildings belonging to one grid cell (0-2), fully deterministic.
inline void site(const Terrain& terrain,int ix,int iz,std::vector<Building>& out){
    auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,300+k);};
    double x=(ix+.3+.4*r(1))*cell,z=(iz+.3+.4*r(2))*cell;
    double h=Landuse::hamlet(x,z);
    if(r(3)>.06+.70*h)return;
    if(Landuse::forest(x,z)>.2)return;
    double d=terrain.nearest(x,z).distance;
    if(d<minSetback||d>maxSetback)return;
    // Face the road: the distance field's gradient points away from it.
    double gx=terrain.nearest(x+2,z).distance-terrain.nearest(x-2,z).distance;
    double gz=terrain.nearest(x,z+2).distance-terrain.nearest(x,z-2).distance;
    double gl=std::hypot(gx,gz);if(gl<1e-6)return;
    Building b;b.x=x;b.z=z;b.fx=-gx/gl;b.fz=-gz/gl;b.seed=WorldNoise::hash(ix,iz,313);
    bool farm=r(4)<.28;
    if(farm){
        b.kind=Longhouse;b.length=16+6*r(5);b.width=7.6+.8*r(6);b.wall=2.5+.3*r(7);b.pitch=.95;
        b.roof=r(8)<.3?2:(r(8)<.7?0:1);b.walls=r(9)<.6?0:1;
        if(!place(terrain,b))return;
        out.push_back(b);
        // Barn at right angles behind one end, forming an L-shaped farmyard.
        Building barn=b;barn.kind=Barn;barn.seed=b.seed*2654435761u+7;
        barn.length=12+5*r(10);barn.width=9+2*r(11);barn.wall=3.4;barn.pitch=.75;barn.roof=3;barn.walls=r(12)<.6?3:2;
        double end=(r(13)<.5?-1:1)*(b.length*.5-barn.width*.5);
        double back=-(b.width*.5+barn.length*.5+.5);
        barn.x=b.x+b.ux()*end+b.fx*back;barn.z=b.z+b.uz()*end+b.fz*back;
        barn.fx=b.ux();barn.fz=b.uz(); // rotated 90 degrees
        if(place(terrain,barn))out.push_back(barn);
    }else{
        b.kind=House;b.length=9+5*r(5);b.width=6.6+1.4*r(6);
        b.wall=r(7)<.35?3.6:2.6;b.pitch=.8+.35*r(8);
        b.roof=r(9)<.45?0:(r(9)<.8?1:4);b.walls=r(10)<.45?0:(r(10)<.8?1:2);
        if(place(terrain,b))out.push_back(b);
    }
}

inline std::vector<Building> near(const Terrain& terrain,double cx,double cz,double radius){
    std::vector<Building> out;
    int x0=int(std::floor((cx-radius)/cell)),x1=int(std::floor((cx+radius)/cell));
    int z0=int(std::floor((cz-radius)/cell)),z1=int(std::floor((cz+radius)/cell));
    for(int ix=x0;ix<=x1;++ix)for(int iz=z0;iz<=z1;++iz){
        double x=(ix+.5)*cell,z=(iz+.5)*cell;
        if(std::hypot(x-cx,z-cz)>radius+cell)continue;
        site(terrain,ix,iz,out);
    }
    return out;
}

// True if a circle (x,z,r) overlaps a building footprint plus a garden margin.
inline bool blocked(const std::vector<Building>& buildings,double x,double z,double r,double margin=3){
    for(const auto& b:buildings){
        double dx=x-b.x,dz=z-b.z;
        double along=std::abs(dx*b.ux()+dz*b.uz()),across=std::abs(dx*b.fx+dz*b.fz);
        double ox=std::max(0.0,along-b.length*.5),oz=std::max(0.0,across-b.width*.5);
        if(std::hypot(ox,oz)<r+margin)return true;
    }
    return false;
}
}

} // namespace route3d
