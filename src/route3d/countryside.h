#pragma once
#include "route.h"
#include "settlement.h"
#include "terrain.h"
#include "world_noise.h"

#include <algorithm>
#include <cmath>
#include <vector>

// The Danish countryside around the road: farm animals in fenced pastures,
// hay bales and tractors in the fields, wind turbines, benches by the road
// and reeds, jetties and rowing boats at the lakes. Placement only (no GL),
// deterministic like trees and rocks (WorldNoise), so tiles rebuild
// identically. Geometry: Route3DScene (Route3DCountryside.cpp).
namespace route3d {

namespace Countryside {
// Open farmland: not in forest, water or on a beach, clear of the buildings.
inline bool open(const Terrain& t,double x,double z,const std::vector<Building>* b,double r,double margin){
    if(Landuse::forest(x,z)>.15)return false;
    if(t.wet(x,z,1.0)||std::isfinite(t.waterAt(x,z)))return false;
    return !(b&&Settlement::blocked(*b,x,z,r,margin));
}
template<class Spec,class Fn>
inline void grid(double cx,double cz,double radius,double cell,Fn fn){
    const int x0=int(std::floor((cx-radius)/cell)),x1=int(std::floor((cx+radius)/cell));
    const int z0=int(std::floor((cz-radius)/cell)),z1=int(std::floor((cz+radius)/cell));
    for(int ix=x0;ix<=x1;++ix)for(int iz=z0;iz<=z1;++iz)fn(ix,iz);
}
}

// --- animals: herds in the pastures, kept 15 m from the road (the fence is at 11.5 m) ---
enum AnimalKind {Holstein=0,DanishRed=1,Sheep=2,Horse=3};
struct AnimalSpec {double x=0,y=0,z=0,heading=0;int kind=Holstein;bool grazing=false;uint32_t seed=0;};
namespace Animals {
constexpr double cell=9,roadGap=15;
// one kind per 450 m: a farm keeps one kind of animal
inline int kindAt(double x,double z){
    const double r=WorldNoise::random(int(std::floor(x/450)),int(std::floor(z/450)),901);
    return r<.32?Holstein:r<.50?DanishRed:r<.80?Sheep:Horse;
}
inline std::vector<AnimalSpec> scatter(const Terrain& t,double cx,double cz,double radius,const std::vector<Building>* b){
    std::vector<AnimalSpec> out;
    Countryside::grid<AnimalSpec>(cx,cz,radius,cell,[&](int ix,int iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,910+k);};
        const double x=(ix+.1+.8*r(1))*cell,z=(iz+.1+.8*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius||Landuse::pasture(x,z)<.6)return;
        const int kind=kindAt(x,z);
        const double herd=WorldNoise::smoothstep(.50,.62,WorldNoise::fbm(x/70,z/70,2,907));
        double p=.02+.40*herd;
        if(kind==Sheep)p*=1.5;
        else if(kind==Horse)p*=.55;
        if(r(3)>p)return;
        if(t.nearest(x,z).distance<roadGap||!Countryside::open(t,x,z,b,1.5,2))return;
        AnimalSpec a;a.x=x;a.z=z;a.y=t.surface(x,z);a.kind=kind;a.seed=WorldNoise::hash(ix,iz,919);
        // a herd faces much the same way
        a.heading=12.566*WorldNoise::fbm(x/90,z/90,1,913)+(r(4)-.5)*1.6;
        a.grazing=r(5)<.6;
        out.push_back(a);
    });
    return out;
}
}

// --- hay bales (round: wrapped white / green / black, or straw; small stacks of square bales) ---
enum BaleKind {RoundWrapped=0,RoundStraw=1,SquareStack=2};
struct BaleSpec {double x=0,y=0,z=0,heading=0;int kind=RoundWrapped,color=0;uint32_t seed=0;};
namespace Bales {
constexpr double cell=12;
inline std::vector<BaleSpec> scatter(const Terrain& t,double cx,double cz,double radius,const std::vector<Building>* b){
    std::vector<BaleSpec> out;
    Countryside::grid<BaleSpec>(cx,cz,radius,cell,[&](int ix,int iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,950+k);};
        const double x=(ix+.2+.6*r(1))*cell,z=(iz+.2+.6*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius||Landuse::hayfield(x,z)<.6||r(3)>.18)return;
        if(t.nearest(x,z).distance<12||!Countryside::open(t,x,z,b,1.2,1.5))return;
        // one kind of bale and one row direction per field
        const int fx=int(std::floor(x/300)),fz=int(std::floor(z/300));
        const double f=WorldNoise::random(fx,fz,961);
        BaleSpec s;s.x=x;s.z=z;s.y=t.surface(x,z);s.seed=WorldNoise::hash(ix,iz,963);
        s.kind=f<.55?RoundWrapped:f<.85?RoundStraw:SquareStack;
        const double c=WorldNoise::random(fx,fz,965);
        s.color=c<.70?0:c<.85?1:2;   // wrapped: white, green, black
        s.heading=WorldNoise::random(fx,fz,967)*3.14159+(r(4)-.5)*.3;
        out.push_back(s);
    });
    return out;
}
}

// --- tractors parked in the fields ---
struct TractorSpec {double x=0,y=0,z=0,heading=0;int color=0;uint32_t seed=0;};
namespace Tractors {
constexpr double cell=380;
inline std::vector<TractorSpec> scatter(const Terrain& t,double cx,double cz,double radius,const std::vector<Building>* b){
    std::vector<TractorSpec> out;
    Countryside::grid<TractorSpec>(cx,cz,radius,cell,[&](int ix,int iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,970+k);};
        if(r(0)>.45)return;
        const double x=(ix+.15+.7*r(1))*cell,z=(iz+.15+.7*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius)return;
        if(Landuse::hayfield(x,z)<.4&&Landuse::pasture(x,z)<.4)return;
        const double d=t.nearest(x,z).distance;
        if(d<20||d>140||!Countryside::open(t,x,z,b,3,3))return;
        TractorSpec s;s.x=x;s.z=z;s.y=t.surface(x,z);s.heading=r(3)*6.283;s.color=int(r(4)*4)%4;
        s.seed=WorldNoise::hash(ix,iz,977);
        out.push_back(s);
    });
    return out;
}
}

// --- wind turbines: alone or a few in a row, in open land away from the road and houses ---
struct TurbineSpec {double x=0,y=0,z=0,hub=70,rotor=28,phase=0;uint32_t seed=0;};
namespace Turbines {
constexpr double cell=520;
inline std::vector<TurbineSpec> scatter(const Terrain& t,double cx,double cz,double radius,const std::vector<Building>* b){
    std::vector<TurbineSpec> out;
    Countryside::grid<TurbineSpec>(cx,cz,radius+300,cell,[&](int ix,int iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,980+k);};
        if(r(0)>.40)return;
        const int count=r(1)<.45?1:(r(1)<.8?2:3);
        const double a=r(2)*3.14159,dx=std::cos(a),dz=std::sin(a);
        const double x0=(ix+.2+.6*r(3))*cell,z0=(iz+.2+.6*r(4))*cell;
        const double hub=62+16*r(5),rotor=hub*.42;
        for(int k=0;k<count;++k){
            const double x=x0+dx*k*150,z=z0+dz*k*150;
            if(std::hypot(x-cx,z-cz)>radius)continue;
            const double d=t.nearest(x,z).distance;
            if(d<110||Landuse::forest(x,z)>.1||Landuse::hamlet(x,z)>.35)continue;
            if(!Countryside::open(t,x,z,b,2,60))continue;
            TurbineSpec s;s.x=x;s.z=z;s.hub=hub;s.rotor=rotor;
            s.seed=WorldNoise::hash(ix*7+k,iz,987);
            s.phase=WorldNoise::random(ix*7+k,iz,989)*6.283;
            s.y=t.surface(x,z)-.5;
            out.push_back(s);
        }
    });
    return out;
}
}

// --- benches beside the road (computed once per route: Route3DScene::setRoute) ---
struct BenchSpec {double x=0,y=0,z=0,fx=0,fz=1;uint32_t seed=0;};   // (fx,fz): the way it faces (the road)
namespace Benches {
constexpr double stretch=650,lateral=7.9;
inline std::vector<BenchSpec> along(const Terrain& t,const Route& route){
    std::vector<BenchSpec> out;
    const double length=route.length();
    for(long s=0;s*stretch<length;++s){
        auto r=[&](uint32_t k){return WorldNoise::random(int(s),0,1000+k);};
        if(r(0)>.45)continue;
        const double d=(double(s)+.15+.7*r(1))*stretch;
        if(d<30||d>length-30)continue;
        const Point p=route.at(d),a=route.at(d-4),c=route.at(d+4);
        double fx=c.x-a.x,fz=c.z-a.z,fl=std::hypot(fx,fz);
        if(fl<1e-6)continue;
        fx/=fl;fz/=fl;
        const double side=r(2)<.5?-1:1;
        const double x=p.x-fz*side*lateral,z=p.z+fx*side*lateral;
        if(t.nearest(x,z).distance<lateral-.6||t.wet(x,z,1.0))continue;
        double lo=1e9,hi=-1e9;
        for(int i=-1;i<=1;i+=2)for(int j=-1;j<=1;j+=2){
            const double h=t.surface(x+i*.9,z+j*.5);lo=std::min(lo,h);hi=std::max(hi,h);
        }
        if(hi-lo>.7)continue;   // too steep
        BenchSpec b;b.x=x;b.z=z;b.y=lo;b.fx=fz*side;b.fz=-fx*side;b.seed=WorldNoise::hash(int(s),1,1009);
        out.push_back(b);
    }
    return out;
}
}

// --- lakes: a jetty (often with a rowing boat) and reeds along the shore ---
struct ReedClump {double x=0,y=0,z=0,height=1.5;uint32_t seed=0;};
struct JettySpec {
    bool valid=false;
    double x0=0,z0=0,x1=0,z1=0;   // shore end (on land), lake end
    double deck=0,width=1.4;
    bool boat=false;double bx=0,bz=0,by=0,bheading=0;int boatColor=0;
    uint32_t seed=0;
};
namespace LakeLife {
inline JettySpec jetty(const Terrain& t,const Terrain::Lake& l){
    JettySpec j;
    auto r=[&](uint32_t k){return WorldNoise::random(int(l.seed%100003),int(l.seed/100003),1100+k);};
    if(r(0)>.75)return j;
    // towards the road (seen from it), a little to one side
    const auto n=t.nearest(l.x,l.z);
    const double a=std::atan2(n.pz-l.z,n.px-l.x)+(r(1)-.5)*1.3,c=std::cos(a),s=std::sin(a);
    const double R=Terrain::shoreRadius(l,c,s),length=std::clamp(.30*R,7.0,16.0);
    j.x0=l.x+c*R*1.06;j.z0=l.z+s*R*1.06;
    j.x1=l.x+c*(R*1.06-length);j.z1=l.z+s*(R*1.06-length);
    if(t.nearest(j.x0,j.z0).distance<9)return j;
    j.deck=l.level+.45;j.seed=l.seed;j.valid=true;
    if(r(2)<.8){   // a rowing boat tied up beside its end
        const double side=r(3)<.5?-1:1;
        j.boat=true;
        j.bx=j.x1+c*2.6-s*side*(j.width*.5+.85);j.bz=j.z1+s*2.6+c*side*(j.width*.5+.85);
        j.by=l.level;j.bheading=a+3.14159+(r(4)-.5)*.25;j.boatColor=int(r(5)*4)%4;
    }
    return j;
}
inline std::vector<ReedClump> reeds(const Terrain& t,const Terrain::Lake& l,const JettySpec& j){
    std::vector<ReedClump> out;
    const int n=std::max(24,int(6.283*l.radius/1.6));
    const double ja=j.valid?std::atan2(j.z0-l.z,j.x0-l.x):1e9;
    for(int k=0;k<n;++k){
        auto r=[&](uint32_t q){return WorldNoise::random(int(l.seed%9973),k,1200+q);};
        const double a=6.283*(k+r(1)*.6)/n,c=std::cos(a),s=std::sin(a);
        // reed beds along part of the shore
        if(WorldNoise::fbm(c*1.8+l.seed%53,s*1.8+l.seed%29,2,l.seed+7)<.52)continue;
        if(j.valid&&std::abs(std::remainder(a-ja,6.283))<.12+4.0/l.radius)continue;   // the jetty's way in
        const double R=Terrain::shoreRadius(l,c,s)*(.90+.12*r(2));
        const double x=l.x+c*R,z=l.z+s*R,y=t.surface(x,z);
        if(l.level-y>.9||t.nearest(x,z).distance<8)continue;
        ReedClump rc;rc.x=x;rc.z=z;rc.y=y;rc.height=std::max(l.level,y)-y+1.1+.9*r(3);rc.seed=WorldNoise::hash(int(l.seed%9973),k,1207);
        out.push_back(rc);
    }
    return out;
}
}

} // namespace route3d
