#pragma once
#include "ghost_rider.h"
#include "glb_model.h"

#include <memory>

// The Treadmill Run's figure: a procedural runner built with the same
// primitives as the cyclist (GhostRider). Geometry only: no Qt Widgets / GL.
namespace route3d {

// Local frame: x = runner's right, y = up, z = forward (see GhostRider).
// `stride` is the gait phase in radians: one full turn is one stride cycle
// (a left and a right step); the app drives it from the step cadence.
class GhostRunner : public GhostRider {
    // Running gait: the ankle path of one leg over a stride (u = 0..1),
    // relative to the ground under the hip: z forward, y up, and the shoe
    // pitch (+ = heel up / toe down). Short ground contact (u 0..0.32),
    // then toe-off, a high heel kick behind, the knee driving forward and
    // the shin reaching out to the next landing - with a flight phase in
    // which neither foot touches the ground (the other leg is half a stride
    // later).
    struct GaitKey { float u, z, y, pitch; };
    static void gait(double u,float reach,float lift,float& z,float& y,float& pitch){
        static const GaitKey k[]={
            {0.00f, 0.20f,0.000f,-0.10f},   // landing, a little ahead of the hip
            {0.16f, 0.00f,0.000f, 0.00f},   // midstance, under the body
            {0.32f,-0.30f,0.010f, 0.45f},   // toe-off behind
            {0.47f,-0.40f,0.300f, 0.75f},   // heel kicks up behind
            {0.62f,-0.10f,0.450f, 0.55f},   // foot passes high under the hip
            {0.78f, 0.24f,0.300f, 0.10f},   // knee drive, shin swings out
            {0.92f, 0.28f,0.080f,-0.15f},   // reaching for the ground
            {1.00f, 0.20f,0.000f,-0.10f}};
        const int n=int(sizeof(k)/sizeof(k[0]));
        u-=std::floor(u);
        int i=0;while(i<n-2&&u>=k[i+1].u)++i;
        // periodic Catmull-Rom through the keys (smooth, no snapping)
        auto at=[&](int j)->const GaitKey&{return k[((j%(n-1))+(n-1))%(n-1)];};
        const GaitKey &p0=at(i-1),&p1=k[i],&p2=k[i+1],&p3=at(i+2);
        const float t=float((u-p1.u)/std::max(1e-6f,p2.u-p1.u));
        auto cr=[t](float a,float b,float c,float d){
            return .5f*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t);};
        z=reach*cr(p0.z,p1.z,p2.z,p3.z)/.40f;
        y=std::max(0.f,lift*cr(p0.y,p1.y,p2.y,p3.y)/.45f);
        pitch=cr(p0.pitch,p1.pitch,p2.pitch,p3.pitch);
    }
    // Returns the ankle's forward position (for the arm swing).
    float leg(float side,double u,float reach,float lift,QVector3D pelvis,
              QVector3D skin,QVector3D shorts,QVector3D sock,QVector3D shoe,QVector3D sole){
        auto hip=pelvis+QVector3D(side*.088f,-.02f,0);
        float fz,fy,pitch;
        gait(u,reach,lift,fz,fy,pitch);
        auto ankleWant=QVector3D(side*.092f,.085f+fy,fz);
        const float upper=.45f,lower=.45f;
        auto knee=joint(hip,ankleWant,upper,lower,QVector3D(0,.1f,1));
        auto ankle=knee+(ankleWant-knee).normalized()*lower;
        auto thigh=knee-hip,shin=ankle-knee;
        contour({{hip-thigh*.05f,.075f,.080f,shorts},
                 {hip+thigh*.22f,.078f,.084f,shorts},
                 {hip+thigh*.42f,.070f,.074f,shorts},
                 {hip+thigh*.45f,.066f,.070f,skin},
                 {hip+thigh*.75f,.056f,.060f,skin},
                 {knee,.045f,.049f,skin},
                 {knee+shin*.20f,.050f,.056f,skin},
                 {knee+shin*.40f,.050f,.055f,skin},
                 {knee+shin*.72f,.033f,.037f,skin},
                 {knee+shin*.80f,.031f,.034f,sock},
                 {ankle,.028f,.031f,sock}},18,3);
        QVector3D toe(0,std::sin(pitch),std::cos(pitch));
        QVector3D axis=QVector3D(0,toe.z(),-toe.y()).normalized();
        QVector3D fwd(0,-std::sin(pitch),std::cos(pitch));
        ellipsoid(ankle+QVector3D(0,-.035f,0)+fwd*.055f,QVector3D(.045f,.042f,.135f),shoe,axis);
        ellipsoid(ankle+QVector3D(0,-.065f,0)+fwd*.055f,QVector3D(.047f,.012f,.13f),sole,axis,1.f,4,12);
        return fz;
    }
    // swing: -1 (hand back) .. +1 (hand forward); elbows bent ~80-100 degrees
    void arm(float side,float swing,QVector3D shoulders,QVector3D shirt,QVector3D skin){
        // (the elbow goes well behind the back: visible from the camera behind)
        const float a=swing>0?swing*.70f:swing*.95f;
        auto shoulder=shoulders+QVector3D(side*.18f,-.025f,-.01f);
        QVector3D upperDir(side*.16f,-std::cos(a),std::sin(a));
        auto elbow=shoulder+upperDir.normalized()*.29f;
        // the forearm points forward-up; tighter when the hand comes forward
        const float bend=.25f+std::max(0.f,a)*.75f+std::max(0.f,-a)*.15f;
        QVector3D foreDir(-side*.22f,std::sin(bend),std::cos(bend));
        auto hand=elbow+foreDir.normalized()*.26f;
        auto upper=elbow-shoulder,lower=hand-elbow;
        contour({{shoulder-upper*.05f,.058f,.060f,shirt},
                 {shoulder+upper*.18f,.060f,.060f,shirt},
                 {shoulder+upper*.36f,.050f,.050f,shirt},
                 {shoulder+upper*.40f,.046f,.046f,skin},
                 {shoulder+upper*.78f,.040f,.040f,skin},
                 {elbow,.034f,.036f,skin},
                 {elbow+lower*.30f,.036f,.038f,skin},
                 {elbow+lower*.80f,.027f,.028f,skin},
                 {hand,.024f,.026f,skin}},16,3);
        ellipsoid(hand+lower.normalized()*.03f,QVector3D(.030f,.042f,.032f),skin,lower);
    }
    void buildRunner(const GhostPose& pose,double speedKmh){
        mesh.clear();mesh.reserve(26000);
        const QVector3D shirt(.13f,.38f,.85f),shorts(.07f,.08f,.12f),skin(.84f,.64f,.51f),hair(.84f,.64f,.51f),
            sock(.93f,.94f,.95f),shoe(.15f,.55f,.95f),sole(.95f,.95f,.95f),cap(.12f,.14f,.20f),lens(.10f,.11f,.15f);
        // stride phase 0..1 of the left leg; the right leg is half a stride later
        const double u=pose.crank/(2*3.141592654);
        // longer strides, higher heel kick and more bounce the faster
        const float pace=float(std::clamp((speedKmh-5.0)/9.0,0.15,1.3));
        const float reach=.22f+.20f*pace,lift=.14f+.26f*pace;
        // pelvis lowest at each midstance (u = .16 and .66), highest in flight
        const float bounce=(.012f+.030f*pace)*float(std::cos(2*3.141592654*2*(u-.16)+3.141592654));
        const float lean=.12f+.07f*pace;   // forward lean of the torso (radians)
        QVector3D pelvis(0,.965f+bounce,0);
        QVector3D spine(0,std::cos(lean),std::sin(lean));
        QVector3D shoulders=pelvis+spine*.47f;
        // torso and hips
        contour({{pelvis-spine*.07f,.110f,.085f,shorts},
                 {pelvis,.135f,.098f,shorts},
                 {pelvis+spine*.06f,.135f,.096f,shirt},
                 {pelvis+spine*.20f,.130f,.092f,shirt},
                 {shoulders-spine*.16f,.150f,.102f,shirt},
                 {shoulders-spine*.05f,.175f,.104f,shirt},
                 {shoulders+spine*.02f,.150f,.088f,shirt},
                 {shoulders+spine*.06f,.060f,.052f,shirt}},24,4);
        // head with a cap and sunglasses
        auto head=shoulders+spine*.19f+QVector3D(0,0,.02f);
        cone(shoulders+spine*.03f,head-spine*.07f,.050f,.044f,skin,10);
        ellipsoid(head,QVector3D(.080f,.100f,.095f),skin);
        ellipsoid(head+QVector3D(0,.035f,-.012f),QVector3D(.086f,.075f,.096f),hair,QVector3D(0,1,0),.5f,10,18);
        ellipsoid(head+QVector3D(0,.055f,.0f),QVector3D(.090f,.045f,.098f),cap,QVector3D(0,1,0),.55f,8,18);
        ellipsoid(head+QVector3D(0,.045f,.10f),QVector3D(.070f,.008f,.055f),cap,QVector3D(0,1,0),1.f,4,14);   // visor
        ellipsoid(head+QVector3D(0,.005f,.078f),QVector3D(.074f,.020f,.030f),lens);
        // legs (right half a stride after the left); each arm swings against
        // the leg on its side
        for(int sign:{-1,1}){
            const float side=float(sign);
            const float footZ=leg(side,u+(sign<0?0.0:0.5),reach,lift,pelvis,skin,shorts,sock,shoe,sole);
            arm(side,std::clamp(-footZ/std::max(.1f,reach),-1.f,1.f),shoulders,shirt,skin);
        }
        modelIndexed=false;
        if(indexedFor!=mesh.size()){index.resize(mesh.size());for(size_t i=0;i<index.size();++i)index[i]=uint32_t(i);indexedFor=mesh.size();}
    }
    std::shared_ptr<const GlbModel> model;
    std::shared_ptr<const GlbModel> walkModel;   // same figure walking (optional)
    std::vector<GhostVertex> local,walkLocal;
    bool modelIndexed=false;   // index holds the model's triangles
    double transition=(kWalkKmh+kRunKmh)*.5;
public:
    // A skinned, animated .glb runner (GlbModel) instead of the procedural
    // figure; null = the procedural figure. One animation cycle = one stride.
    void setModel(std::shared_ptr<const GlbModel> m){model=std::move(m);modelIndexed=false;indexedFor=size_t(-1);}
    bool hasModel()const{return model!=nullptr;}
    // The same figure (same mesh) walking: below kWalkKmh the walk cycle is
    // shown, above kRunKmh the run cycle, cross-faded in between. Ignored
    // when its mesh does not match the running model.
    static constexpr double kWalkKmh=5.5,kRunKmh=7.0;   // defaults (transition 6.25 km/h)
    // The athlete's own walk -> run speed: the cross-fade is centred on it.
    void setTransitionKmh(double kmh){transition=std::clamp(kmh,3.0,12.0);}
    double transitionKmh()const{return transition;}
    bool setWalkModel(std::shared_ptr<const GlbModel> m){
        if(m&&(!model||m->vertexCount()!=model->vertexCount()||m->indices()!=model->indices())){walkModel.reset();return false;}
        walkModel=std::move(m);return true;
    }
    bool hasWalkModel()const{return walkModel!=nullptr;}
    // 0 = walking, 1 = running
    static double runWeight(double speedKmh,double transitionKmh=(kWalkKmh+kRunKmh)*.5){
        const double half=(kRunKmh-kWalkKmh)*.5;
        const double t=std::clamp((speedKmh-(transitionKmh-half))/(2*half),0.0,1.0);
        return t*t*(3-2*t);
    }
    // World-space vertices of the runner at `distance` on the route (3 m
    // ahead of the camera, like the cyclist); indices() gives the triangles.
    const std::vector<GhostVertex>& build(const Route& route,double distance,double speedKmh,double stride){
        auto pose=ghostPose(route,distance,speedKmh);
        pose.lean*=.35;   // a runner barely leans into bends
        pose.crank=stride;
        placeFrame(pose);
        if(model){
            const double phase=stride/(2*3.141592654);
            const double w=walkModel?runWeight(speedKmh,transition):1.0;
            if(w>=1.0)
                model->pose(phase,local);
            else if(w<=0.0)
                walkModel->pose(phase,local);
            else{
                model->pose(phase,local);
                walkModel->pose(phase,walkLocal);
                const float a=float(w);
                for(size_t i=0;i<local.size();++i){
                    local[i].position=walkLocal[i].position*(1-a)+local[i].position*a;
                    local[i].normal=(walkLocal[i].normal*(1-a)+local[i].normal*a).normalized();
                }
            }
            mesh.resize(local.size());
            for(size_t i=0;i<local.size();++i)
                mesh[i]={world(local[i].position),worldNormal(local[i].normal),local[i].color};
            if(!modelIndexed){index=model->indices();modelIndexed=true;indexedFor=size_t(-1);}
            return mesh;
        }
        buildRunner(pose,speedKmh);
        return mesh;
    }
};

} // namespace route3d
