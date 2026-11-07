#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <iterator>
#include <unordered_map>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

struct GeoPoint { double lat, lon, elevation; };
struct Point { double x=0, y=0, z=0, distance=0; };
class Route {
public:
    std::vector<Point> points;
    bool flatFallback=false;
    // The landscape it is drawn in (route3d::Biome, biome.h; 0 = Nordic).
    int biome=0;
    // Where the ridden route ends (the FINISH arch); < 0 = at length(). The
    // drawn road may go on past it (a visual run-out after the finish).
    double finishDistance=-1;
    double finish() const { return finishDistance>=0?std::min(finishDistance,length()):length(); }
    // A looped there-and-back road (GPX Ride loop): where it turns back, a
    // second finish line (a lap ends there too); < 0 = none.
    double turnDistance=-1;
    // Per point: 1 = this part of the road was already ridden earlier on the
    // route (out-and-back, loops) and lies on it; the earlier pass draws it.
    std::vector<unsigned char> shared;
    bool isShared(size_t i) const { return i<shared.size()&&shared[i]; }
    double length() const { return points.empty()?0:points.back().distance; }
    Point at(double distance) const {
        if(points.empty()) return {};
        distance=std::clamp(distance,0.0,length());
        auto it=std::lower_bound(points.begin(),points.end(),distance,
            [](const Point &p,double d){return p.distance<d;});
        if(it==points.begin()) return *it;
        if(it==points.end()) return points.back();
        // Catmull-Rom through the neighbouring points: position AND direction are
        // continuous, so the rider and camera turn smoothly instead of snapping
        // at every polyline vertex.
        size_t i=size_t(it-points.begin())-1,n=points.size();
        const auto& a=points[i];const auto& b=points[i+1];
        const auto& p0=points[i>0?i-1:i];const auto& p3=points[std::min(n-1,i+2)];
        double t=(distance-a.distance)/(b.distance-a.distance);
        // Tangents scaled to the local spacing (non-uniform Catmull-Rom).
        double ha=b.distance-a.distance;
        double s0=i>0?ha/std::max(1e-9,b.distance-p0.distance):1.0, s1=i+2<n?ha/std::max(1e-9,p3.distance-a.distance):1.0;
        auto spline=[&](double q0,double q1,double q2,double q3){
            double m1=i>0?(q2-q0)*s0:(q2-q1), m2=i+2<n?(q3-q1)*s1:(q2-q1);
            double t2=t*t,t3=t2*t;
            return (2*t3-3*t2+1)*q1+(t3-2*t2+t)*m1+(-2*t3+3*t2)*q2+(t3-t2)*m2;
        };
        return {spline(p0.x,a.x,b.x,p3.x),spline(p0.y,a.y,b.y,p3.y),spline(p0.z,a.z,b.z,p3.z),distance};
    }
    double grade(double d) const {
        auto a=at(d-15), b=at(d+15);
        double ds=b.distance-a.distance;
        return ds>0?(b.y-a.y)/ds*100:0;
    }
    // Piecewise-linear lookup in an ascending table (clamped at both ends).
    static double interpolate(const std::vector<double>& xs,const std::vector<double>& ys,double x){
        if(xs.empty()) return 0;
        if(x<=xs.front()) return ys.front();
        if(x>=xs.back()) return ys.back();
        size_t j=size_t(std::upper_bound(xs.begin(),xs.end(),x)-xs.begin());
        size_t i=j-1; double span=xs[j]-xs[i];
        return span>0?ys[i]+(ys[j]-ys[i])*(x-xs[i])/span:ys[i];
    }
    // inputDistance (optional): for every input point, its distance along the
    // returned (smoothed, densified) road - follows each stage below, so the
    // mapping is local (bends that the smoothing shortens are accounted for
    // where they are, not spread over the whole route).
    static Route fromGeo(const std::vector<GeoPoint>& input, std::vector<double>* inputDistance=nullptr) {
        if(input.size()<2) throw std::runtime_error("The route needs at least two points.");
        constexpr double rad=3.14159265358979323846/180.0, earth=6371000;
        double lat0=input.front().lat,lon0=input.front().lon;
        if(std::abs(lat0)>85) throw std::runtime_error("Routes near the poles cannot be shown in 3D.");
        Route raw;
        bool hasAll=true;
        for(auto g:input) if(!std::isfinite(g.elevation)) hasAll=false;
        raw.flatFallback=!hasAll;
        std::vector<double> inputRaw; inputRaw.reserve(input.size());   // raw distance per input point
        for(auto g:input) {
            if(!std::isfinite(g.lat)||!std::isfinite(g.lon)||std::abs(g.lat)>90||std::abs(g.lon)>180)
                throw std::runtime_error("The GPX file contains invalid coordinates.");
            double lonDelta=std::remainder(g.lon-lon0,360.0);
            Point p{earth*lonDelta*rad*std::cos(lat0*rad),hasAll?g.elevation:0,-earth*(g.lat-lat0)*rad,0};
            if(!raw.points.empty()) {
                auto a=raw.points.back(); double gap=std::hypot(p.x-a.x,p.z-a.z);
                if(gap<0.1) { inputRaw.push_back(a.distance); continue; }
                if(gap>2000) throw std::runtime_error("Two track points are more than 2 km apart – the 3D view needs a continuous track.");
                p.distance=a.distance+gap;
            }
            raw.points.push_back(p);
            inputRaw.push_back(p.distance);
        }
        if(raw.length()<20) throw std::runtime_error("The route must be at least 20 m long.");
        if(raw.length()>200000) throw std::runtime_error("The 3D view supports routes up to 200 km.");
        Route out; out.flatFallback=raw.flatFallback;
        std::vector<double> outRaw;   // raw distance each resampled point was taken at
        for(double d=0;d<raw.length();d+=5) { out.points.push_back(raw.at(d)); outRaw.push_back(d); }
        out.points.push_back(raw.at(raw.length())); outRaw.push_back(raw.length());
        // Symmetric smoothing over a 20 m window, with endpoints preserved.
        auto original=out.points;
        for(size_t i=1;i+1<original.size();++i) {
            double sum=0; int count=0;
            for(int j=-2;j<=2;++j) {
                auto k=std::clamp<long long>(static_cast<long long>(i)+j,0,original.size()-1);
                sum+=original[size_t(k)].y; ++count;
            }
            out.points[i].y=sum/count;
        }
        // Horizontal smoothing (Gaussian, sigma ~7.5 m) removes GPS jitter and
        // turns sharp polyline corners into curves. The window shrinks near the
        // ends so start and finish stay in place.
        static const double weight[]={1.0,.8007,.4111,.1353};
        auto plan=out.points;
        for(size_t i=1;i+1<plan.size();++i){
            long long reach=std::min<long long>({3,static_cast<long long>(i),static_cast<long long>(plan.size()-1-i)});
            double sx=0,sz=0,sw=0;
            for(long long j=-reach;j<=reach;++j){double w=weight[std::abs(j)];sx+=plan[size_t(i+j)].x*w;sz+=plan[size_t(i+j)].z*w;sw+=w;}
            out.points[i].x=sx/sw;out.points[i].z=sz/sw;
        }
        const std::vector<double> snapped=snapToEarlierPasses(out.points);
        for(size_t i=1;i<out.points.size();++i)
            out.points[i].distance=out.points[i-1].distance+std::hypot(out.points[i].x-out.points[i-1].x,out.points[i].z-out.points[i-1].z);
        // Densify to 2.5 m along the spline, so the drawn road edge is a curve.
        Route fine; fine.flatFallback=out.flatFallback;
        double total=out.length();
        std::vector<double> fineOut;   // smoothed distance each fine point was taken at
        for(double d=0;d<total;d+=2.5) { fine.points.push_back(out.at(d)); fineOut.push_back(d); }
        fine.points.push_back(out.points.back()); fineOut.push_back(total);
        {   // shared: between two fully snapped points of out
            std::vector<double> outD(out.points.size());
            for(size_t i=0;i<outD.size();++i) outD[i]=out.points[i].distance;
            fine.shared.resize(fine.points.size(),0);
            for(size_t k=0;k<fineOut.size();++k){
                size_t j=size_t(std::upper_bound(outD.begin(),outD.end(),fineOut[k])-outD.begin());
                size_t i=j>0?j-1:0; j=std::min(j,outD.size()-1);
                fine.shared[k]=snapped[i]>=1.0&&snapped[j]>=1.0;
            }
        }
        for(size_t i=1;i<fine.points.size();++i)
            fine.points[i].distance=fine.points[i-1].distance+std::hypot(fine.points[i].x-fine.points[i-1].x,fine.points[i].z-fine.points[i-1].z);
        if(inputDistance){
            std::vector<double> outD(out.points.size()),fineD(fine.points.size());
            for(size_t i=0;i<outD.size();++i) outD[i]=out.points[i].distance;
            for(size_t i=0;i<fineD.size();++i) fineD[i]=fine.points[i].distance;
            inputDistance->resize(input.size());
            for(size_t i=0;i<input.size();++i)
                (*inputDistance)[i]=interpolate(fineOut,fineD,interpolate(outRaw,outD,inputRaw[i]));
        }
        {   // drop repeated points (and their flags)
            size_t w=0;
            for(size_t r=0;r<fine.points.size();++r){
                if(w>0&&fine.points[r].distance-fine.points[w-1].distance<1e-6) continue;
                fine.points[w]=fine.points[r];fine.shared[w]=fine.shared[r];++w;
            }
            fine.points.resize(w);fine.shared.resize(w);
        }
        return fine;
    }
    // Out-and-back routes and loops come back along roads ridden before; GPS
    // puts the passes a few metres apart (and a little higher or lower), so
    // drawn as recorded two roads overlap: double edge lines, asphalt beside
    // the road. A later point near an earlier, parallel part of the road (at
    // least kSnapGapM further along) is moved onto it, height included: fully
    // within kSnapFullM, fading out to kSnapNoneM, so the road joins and leaves
    // smoothly. The weight also fades in with the distance along the route
    // between the passes (kSnapGapM..2 kSnapGapM): after a turnaround the way
    // back joins the way out gradually instead of jumping onto it. Another
    // road more than kSnapRiseM higher or lower (a hairpin on a climb, a
    // bridge) is never joined. Points in order: a third pass snaps onto the
    // snapped second.
    // Returns the snap weight per point (1 = fully on the earlier road).
    static constexpr double kSnapFullM=4.5, kSnapNoneM=9.0, kSnapGapM=40.0, kSnapRiseM=3.5;
    static std::vector<double> snapToEarlierPasses(std::vector<Point>& pts){
        const size_t n=pts.size();
        std::vector<double> weight(n,0.0);
        if(n<4) return weight;
        constexpr double cell=20, minCos=.8;
        auto key=[](long long x,long long z){return (x<<32)^(z&0xffffffffLL);};
        auto cellOf=[&](double v){return (long long)std::floor(v/cell);};
        std::unordered_map<long long,std::vector<size_t>> grid;   // segments j..j+1 by both ends' cells
        auto insert=[&](size_t j){
            long long ax=cellOf(pts[j].x),az=cellOf(pts[j].z),bx=cellOf(pts[j+1].x),bz=cellOf(pts[j+1].z);
            grid[key(ax,az)].push_back(j);
            if(bx!=ax||bz!=az) grid[key(bx,bz)].push_back(j);
        };
        // the resampled points lie ~5 m apart along the raw track
        const double spacing=std::max(1.0,pts[n-1].distance/double(n-1));
        const size_t gap=size_t(std::ceil(kSnapGapM/spacing));
        const std::vector<Point> orig=pts;
        size_t inserted=0;
        for(size_t i=1;i<n;++i){   // (the end too: a return ending on the way out ends on it)
            while(inserted+1<n&&inserted+gap<i) insert(inserted++);
            if(inserted==0) continue;
            Point& p=pts[i];
            // the road's direction here as recorded (the point before is already moved)
            const size_t next=std::min(i+1,n-1);
            double dx=orig[next].x-orig[i-1].x,dz=orig[next].z-orig[i-1].z,dl=std::hypot(dx,dz);
            if(dl<1e-9) continue;
            auto smooth=[](double u){u=std::clamp(u,0.0,1.0);return u*u*(3-2*u);};
            double best=0; Point target;
            long long cx=cellOf(p.x),cz=cellOf(p.z);
            for(long long gx=cx-1;gx<=cx+1;++gx)for(long long gz=cz-1;gz<=cz+1;++gz){
                auto it=grid.find(key(gx,gz)); if(it==grid.end()) continue;
                for(size_t j:it->second){
                    const Point& a=pts[j]; const Point& b=pts[j+1];
                    double sx=b.x-a.x,sz=b.z-a.z,sl2=sx*sx+sz*sz;
                    if(sl2<1e-12) continue;
                    if(std::abs(sx*dx+sz*dz)<minCos*std::sqrt(sl2)*dl) continue;   // crossing, not along
                    double t=std::clamp(((p.x-a.x)*sx+(p.z-a.z)*sz)/sl2,0.0,1.0);
                    double qx=a.x+sx*t,qz=a.z+sz*t,r=std::hypot(p.x-qx,p.z-qz);
                    if(r>=kSnapNoneM) continue;
                    const double qy=a.y+(b.y-a.y)*t;
                    if(std::abs(qy-p.y)>kSnapRiseM) continue;   // another road above / below
                    const double sep=p.distance-(a.distance+(b.distance-a.distance)*t);   // raw: monotonic
                    const double w=smooth((kSnapNoneM-r)/(kSnapNoneM-kSnapFullM))*smooth((sep-kSnapGapM)/kSnapGapM);
                    if(w>best){best=w;target={qx,qy,qz,p.distance};}
                }
            }
            if(best<=0) continue;
            const double w=best;
            p.x+=(target.x-p.x)*w;p.y+=(target.y-p.y)*w;p.z+=(target.z-p.z)*w;
            weight[i]=w;
        }
        return weight;
    }
    // Artificial ~6 km test loop with a realistic profile: rolling start, a
    // 1.2 km climb (avg ~5 %, steepest ~8 %), a summit plateau, a fast descent,
    // a valley section, a short steep kicker and a run-out. Gradients vary
    // smoothly, and bends are organic rather than one regular sine wave.
    static Route demo() {
        struct Section {double length,grade;};
        static const Section profile[]={{700,.4},{500,-1.2},{350,2.5},{500,5.0},{350,7.5},{250,4.0},
            {500,.8},{400,-3.5},{700,-6.2},{300,-2.0},{650,.5},{250,3.0},{300,8.0},{150,2.0},{500,-4.5},{300,-1.0}};
        auto gradeAt=[&](double d){ // blend between neighbouring sections over ~150 m
            double start=0;
            for(size_t i=0;i<std::size(profile);++i){
                double end=start+profile[i].length;
                if(d<end||i+1==std::size(profile)){
                    double g=profile[i].grade;
                    if(i+1<std::size(profile)&&d>end-75){double t=std::clamp((d-(end-75))/150,0.0,1.0);g+=(profile[i+1].grade-g)*t*t*(3-2*t)*.5;}
                    if(i>0&&d<start+75){double t=std::clamp((d-(start-75))/150,0.0,1.0);g=profile[i-1].grade+(g-profile[i-1].grade)*(.5+.5*t*t*(3-2*t));}
                    return g/100;
                }
                start=end;
            }
            return 0.0;
        };
        std::vector<GeoPoint> g;double x=0,z=0,h=32,heading=0;
        for(int i=0;i<=1240;++i){
            double d=i*5.0;
            g.push_back({55.75+z/111195.0,12.4+x/62500.0,h});
            // Gentle, varying curvature; heading stays within +-75 degrees of north,
            // so the road never loops back over itself.
            double curve=.0042*std::sin(d/310+.7)+.0026*std::sin(d/133+2.1)+.0012*std::sin(d/57);
            heading=std::clamp(heading+curve*5,-1.3,1.3);
            x+=std::sin(heading)*5;z+=std::cos(heading)*5;
            h+=gradeAt(d)*5+.03*std::sin(d/41);
        }
        return fromGeo(g);
    }
};

} // namespace route3d
