#pragma once
#include "biome.h"
#include "terrain.h"
#include "world_noise.h"
#include "settlement.h"
#include <vector>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// Natural-looking vegetation placement: trees grow in forest patches defined by
// world-space noise, with scattered solitary trees and bushes in open fields.
// Placement is on a jittered world grid, so it is deterministic per position.
// Palm, Cactus (saguaro), Eucalyptus and Acacia (flat-topped) grow in the
// warm biomes (biome.h).
enum TreeKind {Spruce=0, Broadleaf=1, Bush=2, Palm=3, Cactus=4, Eucalyptus=5, Acacia=6};
static_assert(kTreeKinds==7,"biome.h: one weight per TreeKind");
struct TreeSpec {double x=0,y=0,z=0,height=0,radius=0;int kind=Spruce;uint32_t seed=0;};

namespace Vegetation {
constexpr double cell=10;          // metres between candidate positions
constexpr double roadClearance=8;  // from road centre line to crown edge
inline double forestDensity(double x,double z){
    return Landuse::forest(x,z);
}
// A kind from the weights (roll 0..1), tried in this order (the open land
// tries bushes first: the Nordic world keeps the trees it always had).
inline int pick(const float (&w)[kTreeKinds],double roll,bool open){
    static constexpr int forestOrder[kTreeKinds]={Spruce,Broadleaf,Bush,Palm,Cactus,Eucalyptus,Acacia};
    static constexpr int openOrder[kTreeKinds]={Bush,Broadleaf,Spruce,Palm,Cactus,Eucalyptus,Acacia};
    double sum=0;for(float v:w)sum+=v;
    if(sum<=0)return Bush;
    roll*=sum;int last=Bush;
    const int* order=open?openOrder:forestOrder;
    for(int i=0;i<kTreeKinds;++i){
        const int k=order[i];
        if(w[k]<=0)continue;
        last=k;
        if(roll<w[k])return k;
        roll-=w[k];
    }
    return last;
}
// Height and crown radius of a tree of its kind (size 0..1).
inline void shape(TreeSpec& t,double size,double scale){
    switch(t.kind){
    case Spruce:t.height=9+8*size;t.radius=t.height*.25;break;
    case Broadleaf:t.height=7+7*size;t.radius=t.height*.33;break;
    case Palm:t.height=7+7*size;t.radius=t.height*.36;break;
    case Cactus:t.height=2.2+5*size;t.radius=std::max(.8,t.height*.22);break;
    case Eucalyptus:t.height=11+11*size;t.radius=t.height*.30;break;
    case Acacia:t.height=4.5+4.5*size;t.radius=t.height*.62;break;
    default:t.height=1.3+1.6*size;t.radius=t.height*.65;break;
    }
    if(t.kind!=Bush&&t.kind!=Cactus){t.height*=scale;t.radius*=scale;}
}
inline std::vector<TreeSpec> scatter(const Terrain& terrain,double cx,double cz,double radius,const std::vector<Building>* buildings=nullptr,
                                     const BiomeProfile* profile=nullptr){
    static const BiomeProfile nordic;
    const BiomeProfile& p=profile?*profile:nordic;
    std::vector<TreeSpec> out;
    int x0=int(std::floor((cx-radius)/cell)),x1=int(std::floor((cx+radius)/cell));
    int z0=int(std::floor((cz-radius)/cell)),z1=int(std::floor((cz+radius)/cell));
    for(int ix=x0;ix<=x1;++ix)for(int iz=z0;iz<=z1;++iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,k);};
        double x=(ix+.1+.8*r(1))*cell,z=(iz+.1+.8*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius)continue;
        double forest=forestDensity(x,z);
        if(r(3)>p.treeOpen+p.treeForest*forest)continue;
        TreeSpec t;t.x=x;t.z=z;t.seed=WorldNoise::hash(ix,iz,77);
        double kindRoll=r(4),size=r(5);
        t.kind=forest>.5?pick(p.forestMix,kindRoll,false):pick(p.openMix,kindRoll,true);
        shape(t,size,p.treeScale);
        const bool small=t.kind==Bush||t.kind==Cactus;
        if(buildings&&Settlement::blocked(*buildings,x,z,t.radius*(small?.5:.7)))continue;
        if(terrain.wet(x,z,.8))continue;   // not in lakes, the sea or on the beach
        // open, mostly treeless banks around lakes and along the coast keep the water in view
        if(r(6)>.12&&std::isfinite(terrain.waterAt(x,z)))continue;
        if(terrain.nearest(x,z).distance<roadClearance+t.radius)continue;
        t.y=terrain.surface(x,z)-.15; // sink the trunk slightly into the ground
        // the tree line (mountains): thinning out over the last 150 m below it
        if(t.y>p.treeLine||(t.y>p.treeLine-150&&r(7)>(p.treeLine-t.y)/150))continue;
        out.push_back(t);
    }
    return out;
}
}

// Wild flowers: small clumps in colourful meadow patches (mostly one colour per
// patch, like poppies or cornflowers in a field), sparser in the open grass,
// and along the verges. Only near the camera: they are small.
struct FlowerClump {double x=0,y=0,z=0,size=1;int count=1,color=0;uint32_t seed=0;};
namespace Flowers {
constexpr double cell=2.0;
constexpr int colors=7;   // see Route3DScene::flowers()
inline std::vector<FlowerClump> scatter(const Terrain& terrain,double cx,double cz,double radius,const std::vector<Building>* buildings=nullptr,
                                        double density=1){
    std::vector<FlowerClump> out;
    if(density<=0)return out;
    int x0=int(std::floor((cx-radius)/cell)),x1=int(std::floor((cx+radius)/cell));
    int z0=int(std::floor((cz-radius)/cell)),z1=int(std::floor((cz+radius)/cell));
    for(int ix=x0;ix<=x1;++ix)for(int iz=z0;iz<=z1;++iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,400+k);};
        double x=(ix+.1+.8*r(1))*cell,z=(iz+.1+.8*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius)continue;
        double forest=Landuse::forest(x,z);
        if(forest>.45)continue;
        double patch=WorldNoise::smoothstep(.56,.72,WorldNoise::fbm(x/28,z/28,2,421))*(1-forest*2);
        double p=(.05+.62*patch)*density;
        if(r(3)>p+.22*density)continue;   // cheap test first: the road bonus is at most .22
        double road=terrain.nearest(x,z).distance;
        if(road<7)continue;       // the verge ends at 6.5 m
        if(road<14)p+=.22*density;   // flowers along the roadside
        if(r(3)>p)continue;
        if(buildings&&Settlement::blocked(*buildings,x,z,.3,1))continue;
        if(terrain.wet(x,z,.3))continue;
        FlowerClump f;f.x=x;f.z=z;f.seed=WorldNoise::hash(ix,iz,433);
        // one colour per patch, sometimes a stray other one
        int patchColor=int(WorldNoise::fbm(x/45+3.1,z/45-7.3,1,437)*colors*1.6)%colors;
        f.color=r(4)<.78?patchColor:int(r(5)*colors)%colors;
        f.count=patch>.3?4+int(6*r(6)):1+int(3*r(6));
        f.size=1.1+.5*r(7);
        f.y=terrain.surface(x,z);
        out.push_back(f);
    }
    return out;
}
}

// Rocks: small stones scattered in the grass, now and then a big boulder with a
// few stones around it; more on rocky ground and along lake shores and beaches.
struct RockSpec {double x=0,y=0,z=0,size=1;uint32_t seed=0;};
namespace Rocks {
constexpr double cell=9;
inline std::vector<RockSpec> scatter(const Terrain& terrain,double cx,double cz,double radius,const std::vector<Building>* buildings=nullptr,
                                     double density=1){
    std::vector<RockSpec> out;
    int x0=int(std::floor((cx-radius)/cell)),x1=int(std::floor((cx+radius)/cell));
    int z0=int(std::floor((cz-radius)/cell)),z1=int(std::floor((cz+radius)/cell));
    auto add=[&](double x,double z,double size,uint32_t seed){
        if(terrain.nearest(x,z).distance<7.2+size)return;
        if(buildings&&Settlement::blocked(*buildings,x,z,size,1.5))return;
        double y=terrain.surface(x,z);
        double water=terrain.waterAt(x,z);
        if(std::isfinite(water)&&y<water-size*.45)return;   // all but drowned
        RockSpec s;s.x=x;s.z=z;s.size=size;s.seed=seed;s.y=y-size*.22;   // sunk into the ground
        out.push_back(s);
    };
    for(int ix=x0;ix<=x1;++ix)for(int iz=z0;iz<=z1;++iz){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,500+k);};
        double x=(ix+.1+.8*r(1))*cell,z=(iz+.1+.8*r(2))*cell;
        if(std::hypot(x-cx,z-cz)>radius)continue;
        double rocky=WorldNoise::smoothstep(.55,.75,WorldNoise::fbm(x/120,z/120,2,511));
        double p=(.02+.20*rocky)*density;
        if(r(3)>p+.25)continue;
        if(std::isfinite(terrain.waterAt(x,z)))p+=.25;   // stony shores
        if(r(3)>p)continue;
        bool boulder=r(4)<.16;
        double size=boulder?.9+1.5*r(5):.22+.5*r(5);
        uint32_t seed=WorldNoise::hash(ix,iz,517);
        add(x,z,size,seed);
        if(boulder)
            for(int k=0;k<2;++k){
                double a=r(10+k)*6.283,d=size*(1.1+.8*r(12+k));
                add(x+std::cos(a)*d,z+std::sin(a)*d,size*(.2+.25*r(14+k)),seed*2654435761u+uint32_t(k)+1);
            }
    }
    return out;
}
}

} // namespace route3d
