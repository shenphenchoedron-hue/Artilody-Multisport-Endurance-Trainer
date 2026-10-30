#pragma once
#include "ghost_motion.h"
#include <QVector3D>
#include <QVector2D>
#include <array>
#include <functional>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

struct GhostVertex {QVector3D position,normal,color;};
// Builds the ghost rider in world space each frame.
// A procedural road cyclist with smooth anatomical contours (the app always
// uses this built-in rider; the prototype's optional GLB model is not used).
// Local frame: x = rider's right, y = up, z = forward.
class GhostRider {
    std::vector<GhostVertex> mesh;std::vector<uint32_t> index;size_t indexedFor=size_t(-1);
    QVector3D origin,right,up,forward;
    QVector3D world(QVector3D v)const{return origin+right*v.x()+up*v.y()+forward*v.z();}
    QVector3D worldNormal(QVector3D n)const{return (right*n.x()+up*n.y()+forward*n.z()).normalized();}
    void smoothTriangle(QVector3D a,QVector3D b,QVector3D c,QVector3D na,QVector3D nb,QVector3D nc,QVector3D color){
        mesh.push_back({world(a),worldNormal(na),color});
        mesh.push_back({world(b),worldNormal(nb),color});
        mesh.push_back({world(c),worldNormal(nc),color});
    }
    static void basis(QVector3D axis,QVector3D& u,QVector3D& v){
        u=QVector3D::crossProduct(axis,QVector3D(0,1,0));
        if(u.lengthSquared()<.01f)u=QVector3D::crossProduct(axis,QVector3D(1,0,0));
        u.normalize();v=QVector3D::crossProduct(axis,u).normalized();
    }
    // Truncated cone with analytic (slope-corrected) normals; capped ends optional.
    void cone(QVector3D a,QVector3D b,float ra,float rb,QVector3D color,int sides=12,bool caps=true){
        auto d=b-a;float length=d.length();if(length<1e-5f)return;auto axis=d/length;QVector3D u,v;basis(axis,u,v);
        float slope=(ra-rb)/length;
        for(int i=0;i<sides;i++){
            float t=float(i*6.283185307/sides),s=float((i+1)*6.283185307/sides);
            auto n=u*std::cos(t)+v*std::sin(t),m=u*std::cos(s)+v*std::sin(s);
            auto nn=(n+axis*slope).normalized(),mn=(m+axis*slope).normalized();
            smoothTriangle(a+n*ra,b+n*rb,b+m*rb,nn,nn,mn,color);smoothTriangle(a+n*ra,b+m*rb,a+m*ra,nn,mn,mn,color);
            if(caps){smoothTriangle(a,a+m*ra,a+n*ra,-axis,-axis,-axis,color);smoothTriangle(b,b+n*rb,b+m*rb,axis,axis,axis,color);}
        }
    }
    void tube(QVector3D a,QVector3D b,float radius,QVector3D color,int sides=10){cone(a,b,radius,radius,color,sides);}
    using Paint=std::function<QVector3D(QVector3D)>; // unit-sphere direction -> colour
    void ellipsoid(QVector3D center,QVector3D size,const Paint& paint,QVector3D longAxis=QVector3D(0,1,0),float cutoff=1.f,int rings=10,int sides=18){
        longAxis.normalize();auto xAxis=QVector3D(1,0,0);auto zAxis=QVector3D::crossProduct(xAxis,longAxis).normalized();
        if(zAxis.lengthSquared()<.5f)zAxis=QVector3D(0,0,1);
        xAxis=QVector3D::crossProduct(longAxis,zAxis).normalized();
        auto direction=[&](QVector3D q){return xAxis*q.x()+longAxis*q.y()+zAxis*q.z();};
        auto unit=[&](int r,int s){double a=3.141592654*r/rings,t=6.283185307*s/sides;
            return QVector3D(float(std::sin(a)*std::cos(t)),float(std::cos(a)),float(std::sin(a)*std::sin(t)));};
        auto p=[&](int r,int s){auto q=unit(r,s);return center+direction(QVector3D(q.x()*size.x(),q.y()*size.y(),q.z()*size.z()));};
        // Inverse-scale normals, not sphere normals, are required on an ellipsoid.
        auto n=[&](int r,int s){auto q=unit(r,s);return direction(QVector3D(q.x()/size.x(),q.y()/size.y(),q.z()/size.z())).normalized();};
        int last=std::max(1,int(std::ceil(rings*cutoff)));
        for(int r=0;r<last;r++)for(int s=0;s<sides;s++){
            auto c=paint((unit(r,s)+unit(r+1,s+1))*.5f);
            if(r>0)smoothTriangle(p(r,s),p(r,s+1),p(r+1,s+1),n(r,s),n(r,s+1),n(r+1,s+1),c);
            if(r<rings-1)smoothTriangle(p(r,s),p(r+1,s+1),p(r+1,s),n(r,s),n(r+1,s+1),n(r+1,s),c);
        }
    }
    void ellipsoid(QVector3D center,QVector3D size,QVector3D color,QVector3D longAxis=QVector3D(0,1,0),float cutoff=1.f,int rings=10,int sides=18){
        ellipsoid(center,size,[color](QVector3D){return color;},longAxis,cutoff,rings,sides);
    }
    // Smooth anatomical surface through elliptical cross-sections. Shared vertex
    // normals remove visible segment boundaries; colours can change at cuffs.
    struct Section { QVector3D center; float width,depth; QVector3D color; };
    void contour(const std::vector<Section>& keys,int sides=20,int steps=4){
        if(keys.size()<2)return;
        std::vector<Section> rows;
        auto sample=[](float a,float b,float c,float d,float t){
            return .5f*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t);
        };
        for(size_t k=0;k+1<keys.size();++k)for(int j=0;j<steps;++j){
            float t=float(j)/steps;const auto& a=keys[k?k-1:k];const auto& b=keys[k];
            const auto& c=keys[k+1];const auto& d=keys[std::min(k+2,keys.size()-1)];
            QVector3D q;for(int n=0;n<3;++n)q[n]=sample(a.center[n],b.center[n],c.center[n],d.center[n],t);
            rows.push_back({q,std::max(.003f,sample(a.width,b.width,c.width,d.width,t)),
                std::max(.003f,sample(a.depth,b.depth,c.depth,d.depth,t)),b.color});
        }
        rows.push_back(keys.back());
        std::vector<QVector3D> vertices,normals(rows.size()*sides);
        vertices.reserve(normals.size());
        for(size_t r=0;r<rows.size();++r){
            auto axis=(rows[std::min(r+1,rows.size()-1)].center-rows[r?r-1:r].center).normalized();
            auto x=QVector3D(1,0,0)-axis*axis.x();
            if(x.lengthSquared()<.01f)x=QVector3D(0,0,1)-axis*axis.z();
            x.normalize();auto z=QVector3D::crossProduct(x,axis).normalized();
            for(int j=0;j<sides;++j){float a=float(j*6.283185307/sides);
                vertices.push_back(rows[r].center+x*(rows[r].width*std::cos(a))+z*(rows[r].depth*std::sin(a)));}
        }
        struct Tri {size_t a,b,c;QVector3D color;};std::vector<Tri> tris;
        auto add=[&](size_t a,size_t b,size_t c,QVector3D color){
            auto n=QVector3D::crossProduct(vertices[b]-vertices[a],vertices[c]-vertices[a]);
            normals[a]+=n;normals[b]+=n;normals[c]+=n;tris.push_back({a,b,c,color});};
        for(size_t r=0;r+1<rows.size();++r)for(int j=0;j<sides;++j){
            size_t a=r*sides+j,b=r*sides+(j+1)%sides,c=b+sides,d=a+sides;
            add(a,c,b,rows[r].color);add(a,d,c,rows[r].color);
        }
        for(auto& n:normals)n.normalize();
        for(const auto& t:tris)smoothTriangle(vertices[t.a],vertices[t.b],vertices[t.c],normals[t.a],normals[t.b],normals[t.c],t.color);
        // Ends are concealed inside the adjoining body parts.
    }
    // Surface of revolution around the local X axis (wheel axle).
    // Profile points are (radius, axial offset) with matching 2D normals.
    void lathe(QVector3D center,const std::vector<std::pair<QVector2D,QVector2D>>& profile,int segments,QVector3D color){
        auto at=[&](int i,const QVector2D& q){double a=6.283185307*i/segments;
            return QVector3D(q.y(),float(q.x()*std::cos(a)),float(q.x()*std::sin(a)));};
        for(int i=0;i<segments;i++)for(size_t j=0;j+1<profile.size();j++){
            auto p00=center+at(i,profile[j].first),p10=center+at(i+1,profile[j].first),p01=center+at(i,profile[j+1].first),p11=center+at(i+1,profile[j+1].first);
            auto n00=at(i,profile[j].second),n10=at(i+1,profile[j].second),n01=at(i,profile[j+1].second),n11=at(i+1,profile[j+1].second);
            smoothTriangle(p00,p10,p11,n00,n10,n11,color);smoothTriangle(p00,p11,p01,n00,n11,n01,color);
        }
    }
    void wheel(float z,double phase,QVector3D tyre,QVector3D rim,QVector3D metal){
        QVector3D center(0,.34f,z);
        std::vector<std::pair<QVector2D,QVector2D>> tyreProfile;
        for(int j=0;j<=8;j++){double b=3.141592654*(-.55+1.1*j/8.0); // outer arc of a 25 mm tyre
            tyreProfile.push_back({QVector2D(float(.318+.016*std::cos(b)),float(.016*std::sin(b))),QVector2D(float(std::cos(b)),float(std::sin(b)))});}
        lathe(center,tyreProfile,40,tyre);
        // Deep-section carbon rim: trapezoid profile.
        lathe(center,{{QVector2D(.305f,.012f),QVector2D(.3f,.95f)},{QVector2D(.262f,.008f),QVector2D(-.2f,.98f)},
                      {QVector2D(.262f,.008f),QVector2D(-1,0)},{QVector2D(.262f,-.008f),QVector2D(-1,0)},
                      {QVector2D(.262f,-.008f),QVector2D(-.2f,-.98f)},{QVector2D(.305f,-.012f),QVector2D(.3f,-.95f)}},32,rim);
        for(int i=0;i<18;i++){ // alternating left/right flange spokes
            double t=phase+i*6.283185307/18;float side=i%2?.028f:-.028f;
            auto hub=center+QVector3D(side,float(.025*std::cos(t+.3)),float(.025*std::sin(t+.3)));
            auto end=center+QVector3D(0,float(.262*std::cos(t)),float(.262*std::sin(t)));
            cone(hub,end,.0032f,.0026f,metal,4,false);
        }
        tube(center+QVector3D(-.05f,0,0),center+QVector3D(.05f,0,0),.022f,metal,10);
    }
    // Two-bone IK: joint between root and end, bending towards `pole`.
    static QVector3D joint(QVector3D root,QVector3D end,float upper,float lower,QVector3D pole){
        auto delta=end-root;float d=std::clamp(delta.length(),.01f,upper+lower-.001f);auto along=delta.normalized();
        float x=(upper*upper-lower*lower+d*d)/(2*d),h=std::sqrt(std::max(0.f,upper*upper-x*x));
        auto out=(pole-along*QVector3D::dotProduct(pole,along));if(out.lengthSquared()<1e-6f)out=QVector3D(0,0,1);
        return root+along*x+out.normalized()*h;
    }
    void placeFrame(const GhostPose& pose){
        origin=QVector3D(float(pose.center.x),float(pose.center.y),float(pose.center.z));
        forward=QVector3D(float(pose.fx),float(pose.fy),float(pose.fz));
        auto r=QVector3D(-forward.z(),0,forward.x()).normalized();auto u=QVector3D::crossProduct(forward,r).normalized();
        // forward x right points down for local +Z forward; reverse to get up.
        u=-u;
        right=r*float(std::cos(pose.lean))-u*float(std::sin(pose.lean));
        up=u*float(std::cos(pose.lean))+r*float(std::sin(pose.lean));
    }
    void buildProcedural(const GhostPose& pose){
        mesh.clear();mesh.reserve(36000);
        const QVector3D frame(.10f,.24f,.56f),white(.90f,.91f,.93f),black(.06f,.06f,.07f),tyre(.09f,.09f,.095f),
            carbon(.12f,.12f,.13f),metal(.62f,.64f,.68f),jersey(.13f,.40f,.80f),bib(.05f,.05f,.06f),skin(.84f,.64f,.51f),lens(.10f,.11f,.15f);
        wheel(-.50f,pose.wheel,tyre,carbon,metal);wheel(.50f,pose.wheel,tyre,carbon,metal);
        // Frame: road geometry with tapered tubes and paired stays.
        QVector3D rear(0,.34f,-.50f),front(0,.34f,.50f),bb(0,.27f,-.08f),seatTop(0,.80f,-.25f),headTop(0,.86f,.36f),crown(0,.68f,.41f);
        cone(bb,crown,.030f,.026f,frame);cone(seatTop,headTop,.021f,.024f,frame);cone(bb,seatTop,.025f,.020f,frame);cone(crown,headTop,.027f,.024f,frame);
        for(float s:{-1.f,1.f}){
            cone(rear+QVector3D(s*.055f,0,0),bb+QVector3D(s*.035f,0,0),.010f,.015f,frame,8);
            cone(rear+QVector3D(s*.055f,0,0),seatTop+QVector3D(s*.018f,-.03f,0),.008f,.012f,frame,8);
            cone(crown+QVector3D(s*.035f,0,0),front+QVector3D(s*.05f,0,0),.016f,.009f,frame,8); // fork blades
        }
        ellipsoid(bb,QVector3D(.045f,.035f,.035f),frame);
        // Saddle, seatpost, stem and drop handlebar.
        QVector3D saddle(0,.955f,-.27f);
        tube(seatTop,saddle-QVector3D(0,.02f,0),.014f,black,8);
        ellipsoid(saddle+QVector3D(0,0,.04f),QVector3D(.035f,.018f,.12f),black);
        ellipsoid(saddle+QVector3D(0,0,-.05f),QVector3D(.068f,.022f,.065f),black);
        QVector3D stemEnd(0,.885f,.47f);
        tube(headTop,headTop+QVector3D(0,.03f,-.005f),.019f,black,8);tube(headTop+QVector3D(0,.025f,0),stemEnd,.016f,black,8);
        tube(QVector3D(-.20f,.885f,.47f),QVector3D(.20f,.885f,.47f),.013f,black,10);
        for(float s:{-1.f,1.f}){
            QVector3D p0(s*.20f,.885f,.47f),p1(s*.205f,.88f,.545f),p2(s*.205f,.83f,.575f),p3(s*.205f,.775f,.55f),p4(s*.205f,.76f,.49f);
            tube(p0,p1,.013f,black,8);tube(p1,p2,.013f,black,8);tube(p2,p3,.013f,black,8);tube(p3,p4,.013f,black,8);
            ellipsoid(QVector3D(s*.205f,.905f,.545f),QVector3D(.016f,.028f,.035f),black,QVector3D(0,.6f,.8f)); // brake hood
        }
        // Drivetrain: chainring on the right, crank arms, pedals.
        lathe(bb+QVector3D(.06f,0,0),{{QVector2D(.0f,.004f),QVector2D(0,1)},{QVector2D(.105f,.004f),QVector2D(0,1)},{QVector2D(.105f,-.004f),QVector2D(0,-1)},{QVector2D(.0f,-.004f),QVector2D(0,-1)}},28,metal*.75f);
        tube(bb+QVector3D(-.07f,0,0),bb+QVector3D(.07f,0,0),.018f,metal,8);
        // Photo-inspired posture: long gently arched back, low relaxed shoulders.
        QVector3D pelvis(0,1.025f,-.27f),shoulders(0,1.365f,.17f);
        auto spine=(shoulders-pelvis).normalized();
        // (seat narrower and flatter than the prototype: the glutes read as one
        // slim saddle area, not two balls)
        contour({{pelvis-spine*.05f,.098f,.066f,bib},
                 {pelvis,.124f,.088f,bib},
                 {pelvis+spine*.10f,.145f,.106f,jersey},
                 {pelvis+spine*.23f+QVector3D(0,.018f,-.014f),.150f,.108f,jersey},
                 {shoulders-spine*.10f,.180f,.116f,jersey},
                 {shoulders-spine*.025f,.181f,.102f,jersey},
                 {shoulders+spine*.035f,.125f,.075f,jersey},
                 {shoulders+spine*.070f,.052f,.049f,jersey}},28,4);
        ellipsoid(pelvis-QVector3D(0,.008f,-.012f),QVector3D(.108f,.056f,.076f),bib,spine);
        // Three subtle rear jersey pockets follow the slope of the lower back.
        auto back=QVector3D(0,spine.z(),-spine.y());
        for(float side:{-1.f,0.f,1.f})
            ellipsoid(pelvis+spine*.17f+back*.102f+QVector3D(side*.084f,0,0),
                QVector3D(.039f,.064f,.012f),jersey*.88f,spine,1.f,6,10);
        // Head, neck, aero helmet with vents, sunglasses.
        QVector3D head(0,1.525f,.29f);
        cone(shoulders+QVector3D(0,.02f,.02f),head-QVector3D(0,.06f,.03f),.055f,.045f,skin,10);
        ellipsoid(head,QVector3D(.082f,.10f,.098f),skin);
        auto helmetPaint=[&](QVector3D q){float a=std::atan2(q.x(),q.z());return (q.y()>.25f&&std::abs(std::sin(a*5))<.28f)?black:white;};
        ellipsoid(head+QVector3D(0,.04f,-.025f),QVector3D(.105f,.085f,.15f),helmetPaint,QVector3D(0,1,0),.58f,10,20);
        ellipsoid(head+QVector3D(0,.005f,.075f),QVector3D(.078f,.022f,.035f),lens);
        for(float s:{-1.f,1.f})tube(head+QVector3D(s*.07f,.01f,.06f),head+QVector3D(s*.075f,-.07f,-.01f),.006f,black,5); // helmet straps
        for(int sign:{-1,1}){
            float side=float(sign);
            // Arms reach the brake hoods with elbows bent out and down.
            auto shoulder=shoulders+QVector3D(side*.152f,-.018f,-.008f),hand=QVector3D(side*.205f,.915f,.54f);
            auto elbow=joint(shoulder,hand,.285f,.275f,QVector3D(side*.20f,-.45f,-.25f));
            auto upper=elbow-shoulder,lower=hand-elbow;
            contour({{shoulder-upper*.04f,.052f,.054f,jersey},
                     {shoulder+upper*.12f,.067f,.064f,jersey},
                     {shoulder+upper*.46f,.052f,.051f,jersey},
                     {shoulder+upper*.49f,.050f,.049f,skin},
                     {shoulder+upper*.75f,.042f,.043f,skin},
                     {elbow,.034f,.036f,skin},
                     {elbow+lower*.22f,.039f,.041f,skin},
                     {elbow+lower*.65f,.031f,.032f,skin},
                     {hand,.023f,.025f,skin}},18,3);
            ellipsoid(hand+QVector3D(0,-.007f,.012f),QVector3D(.030f,.038f,.030f),jersey,hand-elbow);
            // Legs follow the pedals with two-bone IK; knees track forward.
            double phase=pose.crank+(sign<0?3.141592654:0);
            auto pedal=bb+QVector3D(side*.12f,float(.17*std::cos(phase)),float(.17*std::sin(phase)));
            tube(bb+QVector3D(side*.075f,0,0),pedal-QVector3D(side*.02f,0,0),.012f,metal,6);
            ellipsoid(pedal,QVector3D(.045f,.012f,.045f),black);
            auto hip=pelvis+QVector3D(side*.080f,0,0),ankle=pedal+QVector3D(0,.075f,-.045f);
            auto knee=joint(hip,ankle,.44f,.43f,QVector3D(side*.08f,0,1));
            auto thigh=knee-hip,shin=ankle-knee;
            const QVector3D shortsBlue(.055f,.27f,.52f);
            contour({{hip,.058f,.062f,bib},
                     {hip+thigh*.16f,.074f,.078f,shortsBlue},
                     {hip+thigh*.43f,.078f,.083f,shortsBlue},
                     {hip+thigh*.69f,.061f,.065f,shortsBlue},
                     {hip+thigh*.71f,.059f,.063f,skin},
                     {hip+thigh*.89f,.047f,.051f,skin},
                     {knee,.044f,.048f,skin},
                     {knee+shin*.18f,.049f,.055f,skin},
                     {knee+shin*.34f,.052f,.058f,skin},
                     {knee+shin*.60f,.036f,.042f,skin},
                     {knee+shin*.82f,.027f,.031f,white},
                     {ankle,.026f,.029f,white}},20,3);
            auto toe=(pedal+QVector3D(0,.028f,.07f)-ankle).normalized();
            ellipsoid(pedal+QVector3D(0,.04f,.02f),QVector3D(.043f,.038f,.125f),white,QVector3D(0,toe.z(),-toe.y()).normalized());
            ellipsoid(pedal+QVector3D(0,.012f,.02f),QVector3D(.041f,.010f,.12f),black,QVector3D(0,1,0),1.f,4,12); // sole
        }
        if(indexedFor!=mesh.size()){index.resize(mesh.size());for(size_t i=0;i<index.size();++i)index[i]=uint32_t(i);indexedFor=mesh.size();}
    }
public:
    // Returns world-space vertices; indices() gives the matching triangle list.
    // crank: the pedal angle in radians (the app drives it from the rider's
    // real cadence; the wheels follow the distance)
    const std::vector<GhostVertex>& build(const Route& route,double distance,double speed,double crank){
        auto pose=ghostPose(route,distance,speed);pose.crank=crank;placeFrame(pose);
        buildProcedural(pose);
        // Transparency is composited from the front surface by the renderer.
        return mesh;
    }
    const std::vector<uint32_t>& indices()const{return index;}
};

} // namespace route3d
