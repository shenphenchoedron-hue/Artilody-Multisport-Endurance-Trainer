#pragma once
#include "landuse.h"
#include "route.h"
#include "world_noise.h"
#include <cstdint>
#include <unordered_map>
#include <limits>
#include <vector>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// A single-valued world-space heightfield: unlike a road-relative ribbon,
// cells cannot fold over the camera when the route turns back on itself.
//
// Water: now and then a lake lies in a hollow beside the route, and where the
// road runs fairly level for a while it can follow a coastline (the sea on
// one side, behind a sandy beach). Both are worked out once per route in
// reset(), so the landscape stays deterministic and the tile job only reads.
class Terrain {
    static constexpr double bucketSize=40;
public:
    static constexpr double spacing=8;
    static constexpr double kCapM=10.0;          // ground held below roads within this
    static constexpr double kSlopeReachM=140.0;  // road heights blended into the slope from this far
    static constexpr double lakeCell=320;   // lakes are looked up by cell
    static constexpr double lakeStretch=700;   // at most one lake per stretch of road
    struct Lake {double x=0,z=0,radius=0,level=0,depth=0;uint32_t seed=0;};
    // One grid node: its height, the water level over it (NaN: no lake or sea
    // here) and how sandy the ground is (beach, lake shore; 0..1).
    struct Node {double height=0,water=std::numeric_limits<double>::quiet_NaN();float sand=0;};
    struct Nearest {
        double distance=std::numeric_limits<double>::infinity(), height=0, low=std::numeric_limits<double>::infinity();
        size_t segment=0; double t=0, px=0, pz=0;   // closest road segment, position on it and the closest point
    };
private:
    const Route* route=nullptr;
    std::unordered_map<uint64_t,std::vector<size_t>> buckets;
    mutable std::unordered_map<uint64_t,Node> nodeCache;
    std::unordered_map<uint64_t,std::vector<Lake>> lakes;   // by lake cell
    // per route point: coast weight (0 = inland), sea level and the sea side (unit XZ vector)
    std::vector<double> coastW,seaLevel,seaX,seaZ;
    static uint64_t key(int x,int z) {
        return (uint64_t(uint32_t(x))<<32)|uint32_t(z);
    }
public:
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
    void reset(const Route& r) {
        route=&r;buckets.clear();nodeCache.clear();lakes.clear();
        coastW.clear();seaLevel.clear();seaX.clear();seaZ.clear();
        for(size_t i=0;i+1<r.points.size();++i) {
            auto a=r.points[i],b=r.points[i+1];
            for(int x=int(std::floor(std::min(a.x,b.x)/bucketSize));x<=int(std::floor(std::max(a.x,b.x)/bucketSize));++x)
                for(int z=int(std::floor(std::min(a.z,b.z)/bucketSize));z<=int(std::floor(std::max(a.z,b.z)/bucketSize));++z)
                    buckets[key(x,z)].push_back(i);
        }
        computeCoast();
        computeLakes();
        nodeCache.clear();
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
                    if(dist<result.distance){
                        result.distance=dist;result.height=a.y+t*(b.y-a.y);
                        result.segment=i;result.t=t;result.px=a.x+t*dx;result.pz=a.z+t*dz;
                    }
                    // Carve all corners of cells touched by a road. Taking the
                    // minimum avoids grass cutting through steep/nearby roads.
                    // (within one grid cell: kept below every road there, so no
                    // grass covers the asphalt - but not the whole strip out to a
                    // hairpin's other leg: that cut the upper road out as a wall)
                    if(dist<kCapM)result.low=std::min(result.low,a.y+t*(b.y-a.y)-std::abs(b.y-a.y)*.5);
                }
            }
            double outside=std::min({x-(cx-ring)*bucketSize,(cx+ring+1)*bucketSize-x,
                                      z-(cz-ring)*bucketSize,(cz+ring+1)*bucketSize-z});
            if(outside>std::max(kCapM,result.distance))break;
        }
        return result;
    }
    // The lakes whose centre lies within r of (x,z) (immutable after reset()).
    std::vector<Lake> lakesNear(double x,double z,double r) const {
        std::vector<Lake> out;
        const int x0=int(std::floor((x-r)/lakeCell)),x1=int(std::floor((x+r)/lakeCell));
        const int z0=int(std::floor((z-r)/lakeCell)),z1=int(std::floor((z+r)/lakeCell));
        for(int i=x0;i<=x1;++i)for(int j=z0;j<=z1;++j){
            auto it=lakes.find(key(i,j));if(it==lakes.end())continue;
            for(const auto& l:it->second)if(std::hypot(l.x-x,l.z-z)<=r)out.push_back(l);
        }
        return out;
    }
    // Lake shoreline radius in the direction (c,s) (unit vector): wobbly, not a circle.
    static double shoreRadius(const Lake& l,double c,double s){
        return l.radius*(.80+.40*WorldNoise::fbm(c*1.4+double(l.seed%61),s*1.4+double(l.seed%37),2,l.seed));
    }
    Node nodeData(int ix,int iz) const {
        auto id=key(ix,iz);auto it=nodeCache.find(id);if(it!=nodeCache.end())return it->second;
        double x=ix*spacing,z=iz*spacing;
        double roadCap=std::numeric_limits<double>::infinity();
        Node d=base(x,z,&roadCap);
        applyLakes(x,z,d);
        d.height=std::min(d.height,roadCap);   // a lake's bank never rises over the road
        if(nodeCache.size()>150000)nodeCache.clear();
        nodeCache[id]=d;return d;
    }
    double node(int ix,int iz) const {return nodeData(ix,iz).height;}
    // Interpolate the actual rendered triangles, not the analytic function.
    double surface(double x,double z) const {
        int ix=int(std::floor(x/spacing)),iz=int(std::floor(z/spacing));
        double u=x/spacing-ix,w=z/spacing-iz;
        double a=node(ix,iz),b=node(ix+1,iz),c=node(ix+1,iz+1),d=node(ix,iz+1);
        return u>=w ? a+(b-a)*u+(c-b)*w : a+(c-d)*u+(d-a)*w;
    }
    // The water level over (x,z) (NaN when no lake or sea is near).
    double waterAt(double x,double z) const {
        int ix=int(std::floor(x/spacing)),iz=int(std::floor(z/spacing));
        double level=std::numeric_limits<double>::quiet_NaN();
        for(int i=0;i<2;++i)for(int j=0;j<2;++j){
            auto d=nodeData(ix+i,iz+j);
            if(std::isfinite(d.water)&&!(level>=d.water))level=d.water;
        }
        return level;
    }
    // True in or close to water, or on sand (beach, lake shore): no trees,
    // houses or flowers there.
    bool wet(double x,double z,double margin=.5) const {
        int ix=int(std::floor(x/spacing)),iz=int(std::floor(z/spacing));
        for(int i=0;i<2;++i)for(int j=0;j<2;++j){
            auto d=nodeData(ix+i,iz+j);
            if(d.sand>.3f)return true;
            if(std::isfinite(d.water)&&d.height<d.water+margin)return true;
        }
        return false;
    }

private:
    // Coast weight and sea level at a point (false: inland, or the land side of a coast road).
    bool coast(const Nearest& n,double x,double z,double& w,double& sea) const {
        size_t i=n.segment;
        if(i+1>=coastW.size())return false;
        double t=n.t;
        w=coastW[i]+(coastW[i+1]-coastW[i])*t;
        if(w<.005)return false;
        double sx=seaX[i]+(seaX[i+1]-seaX[i])*t,sz=seaZ[i]+(seaZ[i+1]-seaZ[i])*t;
        double side=(x-n.px)*sx+(z-n.pz)*sz;
        if(side<=0)return false;
        // fade out where the offset runs along the road (beyond the route's ends)
        w*=WorldNoise::smoothstep(.2,.6,side/std::max(n.distance,1e-3));
        if(w<.005)return false;
        sea=seaLevel[i]+(seaLevel[i+1]-seaLevel[i])*t;
        return true;
    }
    // The terrain without lakes: hills, valleys and coast. roadCap: the most
    // the ground may be here (just under a road within reach, else infinite).
    // The mountainside the road climbs: the heights of all road nearby,
    // weighted by distance (and segment length), so between two legs of a
    // switchback the ground rises from the lower road to the upper one
    // instead of following only the nearest road and stepping up halfway.
    // bankCap: the ground may rise from any road at most kBankSlope (after a
    // flat strip beside it), so a road below another (a hairpin's lower leg)
    // gets a steep but even bank, not a sawtooth cliff on the coarse grid.
    static constexpr double kBankSlope=.9, kBankFlatM=4.5;
    double slopeHeight(double x,double z,double fallback,double* bankCap=nullptr) const {
        if(bankCap)*bankCap=std::numeric_limits<double>::infinity();
        if(!route)return fallback;
        const int cx=int(std::floor(x/bucketSize)),cz=int(std::floor(z/bucketSize)),r=int(std::ceil(kSlopeReachM/bucketSize));
        double sw=0,sh=0,lo=1e18,hi=-1e18;
        for(int bx=cx-r;bx<=cx+r;++bx)for(int bz=cz-r;bz<=cz+r;++bz){
            auto it=buckets.find(key(bx,bz));if(it==buckets.end())continue;
            for(size_t i:it->second){
                const auto& a=route->points[i];const auto& b=route->points[i+1];
                const double dx=b.x-a.x,dz=b.z-a.z,l2=dx*dx+dz*dz;
                if(l2<1e-12)continue;
                const double t=std::clamp(((x-a.x)*dx+(z-a.z)*dz)/l2,0.0,1.0);
                const double d=std::hypot(x-a.x-t*dx,z-a.z-t*dz);
                if(d>kSlopeReachM)continue;
                const double y=a.y+t*(b.y-a.y);
                if(bankCap&&d<45)*bankCap=std::min(*bankCap,y-.4+std::max(0.0,d-kBankFlatM)*kBankSlope);
                const double w=std::sqrt(l2)/((d+6)*(d+6)*(d+6));
                sw+=w;sh+=w*y;lo=std::min(lo,y);hi=std::max(hi,y);
            }
        }
        if(sw<=0||hi-lo<4)return fallback;   // (no relief: as before)
        return sh/sw;
    }
    Node base(double x,double z,double* roadCap=nullptr) const {
        Node out;auto n=nearest(x,z);
        // next to the road its own height, further out the mountainside
        double bank=std::numeric_limits<double>::infinity();
        const double ground=n.height+(slopeHeight(x,z,n.height,&bank)-n.height)*WorldNoise::smoothstep(7,30,n.distance);   // (flat out to the cap: no teeth where capped and free nodes alternate)
        double h=ground-.4+WorldNoise::smoothstep(12,105,n.distance)*hillShape(x,z,n.distance);
        double w=0,sea=0;
        if(coast(n,x,z,w,sea)){
            // road level next to the road, a sandy beach down to the water, then the sea floor
            double d=n.distance,road=n.height-.4;
            double shore=road+(sea-.7-road)*WorldNoise::smoothstep(13,45,d)-6*WorldNoise::smoothstep(45,160,d);
            h+=(shore-h)*w;
            out.water=sea;
            out.sand=float(WorldNoise::smoothstep(.15,.5,w)*WorldNoise::smoothstep(9,15,d)*WorldNoise::smoothstep(sea+3.2,sea+1.2,h));
        }
        h=std::min(h,bank);
        if(std::isfinite(n.low)){
            h=std::min(h,n.low-.4);
            if(roadCap)*roadCap=n.low-.4;
        }
        out.height=h;
        return out;
    }
    // Out to lakeOpen shore radii the land is held low (a meadow around the
    // water), so hills right next to a lake cannot hide it from the road.
    static constexpr double lakeOpen=2.6;
    // The highest the land may be at q shore radii from a lake's centre (q >= 1):
    // a short, low bank, then a gentle rise up to the surrounding land.
    static double lakeCap(const Lake& l,double q){
        double bank=WorldNoise::smoothstep(1,1.12,q)*.55;
        double rise=WorldNoise::smoothstep(1.12,lakeOpen,q);
        double outer=WorldNoise::smoothstep(1.8,lakeOpen,q);
        return l.level-.3+bank+7*rise*rise+30*outer*outer;
    }
    void applyLakes(double x,double z,Node& d) const {
        if(lakes.empty())return;
        int cx=int(std::floor(x/lakeCell)),cz=int(std::floor(z/lakeCell));
        for(int i=-1;i<=1;++i)for(int j=-1;j<=1;++j){
            auto it=lakes.find(key(cx+i,cz+j));if(it==lakes.end())continue;
            for(const auto& l:it->second){
                double dx=x-l.x,dz=z-l.z,dist=std::hypot(dx,dz);
                if(dist>l.radius*1.2*lakeOpen+1)continue;
                double c=dist>1e-9?dx/dist:1,s=dist>1e-9?dz/dist:0;
                double q=dist/shoreRadius(l,c,s);
                if(q>=lakeOpen)continue;
                if(q<1)d.height=l.level-.3-l.depth*WorldNoise::smoothstep(1,.35,q);
                else{
                    d.height=std::min(d.height,lakeCap(l,q));   // low banks, an open meadow around
                    d.height=std::max(d.height,l.level+.25-3*WorldNoise::smoothstep(1.5,1.9,q));   // the dam
                }
                if(q<1.5){
                    d.water=l.level;
                    d.sand=std::max(d.sand,float(.55*WorldNoise::smoothstep(1.32,1.08,q)));
                }
            }
        }
    }
    void computeCoast(){
        const auto& p=route->points;const size_t N=p.size();
        coastW.assign(N,0);seaLevel.assign(N,0);seaX.assign(N,0);seaZ.assign(N,0);
        if(N<2)return;
        std::vector<double> lo(N),zone(N),facing(N);
        size_t a=0,b=0;
        for(size_t i=0;i<N;++i){
            // level ground over +-350 m along the road
            while(p[i].distance-p[a].distance>350)++a;
            if(b<i)b=i;
            while(b+1<N&&p[b+1].distance-p[i].distance<=350)++b;
            double mn=p[b].y,mx=p[b].y;
            for(size_t j=a;j<=b;j+=3){mn=std::min(mn,p[j].y);mx=std::max(mx,p[j].y);}
            lo[i]=mn;
            // the sea side: the road's normal pointing the way a slowly turning world direction points
            size_t i0=i>0?i-1:i,i1=std::min(N-1,i+1);
            double dx=p[i1].x-p[i0].x,dz=p[i1].z-p[i0].z,l=std::hypot(dx,dz);
            if(l<1e-9)continue;
            double nx=-dz/l,nz=dx/l;
            double angle=WorldNoise::fbm(p[i].x/4500,p[i].z/4500,2,509)*12.566;
            double f=nx*std::cos(angle)+nz*std::sin(angle);
            double sign=f<0?-1:1;
            seaX[i]=nx*sign;seaZ[i]=nz*sign;facing[i]=std::abs(f);
            zone[i]=WorldNoise::smoothstep(.60,.70,WorldNoise::fbm(p[i].x/1800,p[i].z/1800,2,501))
                    *WorldNoise::smoothstep(7,3.5,mx-mn);
        }
        // box filter along the road (prefix sums, two pointers)
        auto smooth=[&](const std::vector<double>& in,double reach){
            std::vector<double> sum(N+1,0),out(N);
            for(size_t i=0;i<N;++i)sum[i+1]=sum[i]+in[i];
            size_t s=0,e=0;
            for(size_t i=0;i<N;++i){
                while(p[i].distance-p[s].distance>reach)++s;
                if(e<i)e=i;
                while(e+1<N&&p[e+1].distance-p[i].distance<=reach)++e;
                out[i]=(sum[e+1]-sum[s])/double(e+1-s);
            }
            return out;
        };
        auto w=smooth(zone,150);
        // every lo within 350 m lies below this road point: so does their mean
        auto sea=smooth(lo,250);
        for(size_t i=0;i<N;++i){
            // no coast where the road turns along the sea direction (the side would flip)
            coastW[i]=w[i]*WorldNoise::smoothstep(.3,.6,facing[i]);
            seaLevel[i]=sea[i]-1.4;
        }
    }
    // Lakes: every lakeStretch metres of road maybe one, close beside it
    // (from the saddle a lake further off is only a thin line), placed by the
    // world cell it lies in so the same stretch gets the same lake.
    void computeLakes(){
        const auto& p=route->points;
        if(p.size()<2)return;
        std::vector<Lake> all;
        long stretch=-1;
        for(size_t i=0;i+1<p.size();++i){
            long s=long(std::floor(p[i].distance/lakeStretch));
            if(s==stretch)continue;
            stretch=s;
            int cx=int(std::floor(p[i].x/lakeCell)),cz=int(std::floor(p[i].z/lakeCell));
            tryLake(cx,cz,s,all);
        }
    }
    void tryLake(int ix,int iz,long stretch,std::vector<Lake>& all){
        auto r=[&](uint32_t k){return WorldNoise::random(ix,iz,700+k);};
        if(r(0)>.6)return;
        // a point along this stretch, then out to one side
        double along=(double(stretch)+.1+.8*r(1))*lakeStretch;
        if(along>route->length())return;
        auto c=route->at(along),f=route->at(std::min(route->length(),along+4));
        double fx=f.x-c.x,fz=f.z-c.z,fl=std::hypot(fx,fz);
        if(fl<1e-6)return;
        Lake l;
        l.radius=24+30*r(3);l.depth=2.5+2.5*r(4);l.seed=WorldNoise::hash(ix,iz,733);
        // the shore (up to 1.2 radii out) comes to 14..40 m from the road
        const double reach=1.8*l.radius,side=r(5)<.5?-1:1,off=1.2*l.radius+14+26*r(2);
        l.x=c.x-fz/fl*off*side;l.z=c.z+fx/fl*off*side;
        auto n=nearest(l.x,l.z);
        if(n.distance<1.2*l.radius+12)return;   // clear of every part of the road
        // in open country, not hidden in a forest
        if(Landuse::forest(l.x,l.z)>.25)return;
        if(n.segment+1<coastW.size()&&std::max(coastW[n.segment],coastW[n.segment+1])>.005)return;
        for(const auto& o:all)if(std::hypot(o.x-l.x,o.z-l.z)<reach+1.8*o.radius+10)return;
        // the water lies at the lowest point of the rim; the banks rise from there
        double lo=1e18,hi=-1e18;
        for(int k=0;k<16;++k){
            double a=k*6.283185307/16,c=std::cos(a),s=std::sin(a);
            for(double ring:{1.1,1.5}){
                double rr=ring*shoreRadius(l,c,s),h=base(l.x+c*rr,l.z+s*rr).height;
                lo=std::min(lo,h);if(ring>1.2)hi=std::max(hi,h);
            }
        }
        if(hi-lo>35)return;   // a steep hillside: no lake
        // just below the road (seen from the saddle); a low natural dam
        // (see applyLakes()) holds it where the shore dips below that
        l.level=n.height-.55;
        if(lo<l.level-2.5)return;   // a deep hollow or a slope: it would take a high dam
        // no hill between the road and the shore hiding it (the land as
        // drawn: held low around the lake, see lakeCap())
        for(int k=1;k<=8;++k){
            double t=k/9.0*(n.distance-.9*l.radius)/n.distance;
            double x=n.px+(l.x-n.px)*t,z=n.pz+(l.z-n.pz)*t,dx=x-l.x,dz=z-l.z,dist=std::hypot(dx,dz);
            double h=base(x,z).height,q=dist/shoreRadius(l,dx/dist,dz/dist);
            if(q<lakeOpen)h=std::min(h,lakeCap(l,q));
            if(h>n.height+1.2)return;
        }
        // the road beside it stays clear of the water: where the ground is held
        // under the road (16 m to each side, see base()) it must stay above
        // the water, or the lake would show through the verge
        {
            const double reachOut=1.9*1.2*l.radius+16;
            const int b0x=int(std::floor((l.x-reachOut)/bucketSize)),b1x=int(std::floor((l.x+reachOut)/bucketSize));
            const int b0z=int(std::floor((l.z-reachOut)/bucketSize)),b1z=int(std::floor((l.z+reachOut)/bucketSize));
            for(int bx=b0x;bx<=b1x;++bx)for(int bz=b0z;bz<=b1z;++bz){
                auto it=buckets.find(key(bx,bz));if(it==buckets.end())continue;
                for(size_t i:it->second){
                    const auto& a=route->points[i];
                    double dx=a.x-l.x,dz=a.z-l.z,dist=std::hypot(dx,dz);
                    if(dist>reachOut||dist<1e-6)continue;
                    double q=(dist-16)/shoreRadius(l,dx/dist,dz/dist);
                    if(q<1.9&&a.y-.4<l.level+.05)return;
                }
            }
        }
        all.push_back(l);
        lakes[key(int(std::floor(l.x/lakeCell)),int(std::floor(l.z/lakeCell)))].push_back(l);
    }
};

} // namespace route3d
