#pragma once
#include "route.h"
#include "world_noise.h"
#include <cstdint>
#include <unordered_map>
#include <limits>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// A single-valued world-space heightfield: unlike a road-relative ribbon,
// cells cannot fold over the camera when the route turns back on itself.
class Terrain {
    static constexpr double bucketSize=40;
    const Route* route=nullptr;
    std::unordered_map<uint64_t,std::vector<size_t>> buckets;
    mutable std::unordered_map<uint64_t,double> heightCache;
    static uint64_t key(int x,int z) {
        return (uint64_t(uint32_t(x))<<32)|uint32_t(z);
    }
public:
    static constexpr double spacing=8;
    // Natural-looking relief: domain-warped fractal hills, sharper ridge lines
    // on the higher ground and a gentle rise away from the road, so the route
    // tends to run along valley floors like real roads do.
    static double hillShape(double x,double z,double roadDistance){
        using namespace WorldNoise;
        double wx=x+180*(fbm(x/700,z/700,2,41)-.5),wz=z+180*(fbm(x/700+5.2,z/700+1.3,2,43)-.5);
        double broad=fbm(wx/560,wz/560,5,3);                                   // ~0.25..0.75
        double r=std::clamp((fbm(wx/240,wz/240,4,7)-.5)*3.2,-1.0,1.0);
        double ridge=(1-std::abs(r))*(1-std::abs(r));                         // 0..1, sharp crests
        double high=smoothstep(.45,.70,broad);                                 // ridges only on high ground
        double detail=fbm(x/55,z/55,2,9)-.5;
        double open=smoothstep(.38,.62,fbm(x/420,z/420,2,77));                  // 0 = open plain, 1 = valley side
        double rise=(std::min(roadDistance,260.0)*.10+std::max(0.0,roadDistance-260)*.03)*(.15+.85*open)*(.4+broad);             // valley sides
        return 80*(broad-.5)+22*ridge*high+3.2*detail+rise;
    }
    struct Nearest {double distance=std::numeric_limits<double>::infinity(), height=0, low=std::numeric_limits<double>::infinity();};
    void reset(const Route& r) {
        route=&r;buckets.clear();heightCache.clear();
        for(size_t i=0;i+1<r.points.size();++i) {
            auto a=r.points[i],b=r.points[i+1];
            for(int x=int(std::floor(std::min(a.x,b.x)/bucketSize));x<=int(std::floor(std::max(a.x,b.x)/bucketSize));++x)
                for(int z=int(std::floor(std::min(a.z,b.z)/bucketSize));z<=int(std::floor(std::max(a.z,b.z)/bucketSize));++z)
                    buckets[key(x,z)].push_back(i);
        }
    }
    Nearest nearest(double x,double z) const {
        Nearest result;if(!route||route->points.size()<2)return result;
        int cx=int(std::floor(x/bucketSize)),cz=int(std::floor(z/bucketSize));
        // Queries occur within 600 m of the route. Search outward with a
        // geometric lower bound, rather than choosing the first occupied bin.
        for(int ring=0;ring<=32;++ring) {
            for(int bx=cx-ring;bx<=cx+ring;++bx)for(int bz=cz-ring;bz<=cz+ring;++bz) {
                if(ring>0&&bx!=cx-ring&&bx!=cx+ring&&bz!=cz-ring&&bz!=cz+ring)continue;
                auto it=buckets.find(key(bx,bz));if(it==buckets.end())continue;
                for(size_t i:it->second) {
                    auto a=route->points[i],b=route->points[i+1];
                    double dx=b.x-a.x,dz=b.z-a.z,l2=dx*dx+dz*dz;
                    if(l2<1e-12)continue;
                    double t=std::clamp(((x-a.x)*dx+(z-a.z)*dz)/l2,0.0,1.0);
                    double dist=std::hypot(x-a.x-t*dx,z-a.z-t*dz);
                    if(dist<result.distance){result.distance=dist;result.height=a.y+t*(b.y-a.y);}
                    // Carve all corners of cells touched by a road. Taking the
                    // minimum avoids grass cutting through steep/nearby roads.
                    if(dist<16)result.low=std::min(result.low,std::min(a.y,b.y));
                }
            }
            double outside=std::min({x-(cx-ring)*bucketSize,(cx+ring+1)*bucketSize-x,
                                      z-(cz-ring)*bucketSize,(cz+ring+1)*bucketSize-z});
            if(outside>std::max(16.0,result.distance))break;
        }
        return result;
    }
    double node(int ix,int iz) const {
        auto id=key(ix,iz);auto it=heightCache.find(id);if(it!=heightCache.end())return it->second;
        double x=ix*spacing,z=iz*spacing;auto n=nearest(x,z);
        double height=n.height-.4+WorldNoise::smoothstep(12,105,n.distance)*hillShape(x,z,n.distance);
        if(std::isfinite(n.low))height=std::min(height,n.low-.4);
        if(heightCache.size()>150000)heightCache.clear();
        heightCache[id]=height;return height;
    }
    // Interpolate the actual rendered triangles, not the analytic function.
    double surface(double x,double z) const {
        int ix=int(std::floor(x/spacing)),iz=int(std::floor(z/spacing));
        double u=x/spacing-ix,w=z/spacing-iz;
        double a=node(ix,iz),b=node(ix+1,iz),c=node(ix+1,iz+1),d=node(ix,iz+1);
        return u>=w ? a+(b-a)*u+(c-b)*w : a+(c-d)*u+(d-a)*w;
    }
};

} // namespace route3d
