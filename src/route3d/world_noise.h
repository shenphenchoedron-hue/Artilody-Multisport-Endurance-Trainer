#pragma once
#include <cmath>
#include <cstdint>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// Deterministic, non-repeating world-space noise. Used on the CPU for terrain
// shape, ground colour and vegetation placement, so a rebuilt tile always
// produces the same landscape (no popping when the camera moves).
namespace WorldNoise {
inline uint32_t hash(int x,int y,uint32_t seed){
    uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u+seed*2246822519u;
    h=(h^(h>>13))*1274126177u;return h^(h>>16);
}
inline double random(int x,int y,uint32_t seed){return double(hash(x,y,seed)&0xffffffu)/double(0xffffffu);}
inline double value(double x,double z,uint32_t seed){
    double fx=std::floor(x),fz=std::floor(z);int ix=int(fx),iz=int(fz);
    double u=x-fx,w=z-fz;u=u*u*(3-2*u);w=w*w*(3-2*w);
    double a=random(ix,iz,seed),b=random(ix+1,iz,seed),c=random(ix,iz+1,seed),d=random(ix+1,iz+1,seed);
    return (1-w)*((1-u)*a+u*b)+w*((1-u)*c+u*d);
}
// Fractal sum in [0,1], centred near 0.5.
inline double fbm(double x,double z,int octaves,uint32_t seed){
    double sum=0,amp=.5,norm=0;
    for(int i=0;i<octaves;++i){
        sum+=amp*value(x,z,seed+uint32_t(i)*101u);norm+=amp;
        x=x*2.03+17.1;z=z*2.03-9.7;amp*=.5;
    }
    return sum/norm;
}
inline double smoothstep(double a,double b,double x){
    double t=(x-a)/(b-a);t=t<0?0:(t>1?1:t);return t*t*(3-2*t);
}
}

} // namespace route3d
