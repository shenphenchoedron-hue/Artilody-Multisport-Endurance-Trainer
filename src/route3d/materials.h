#pragma once
#include <QImage>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Ported from the Route3D prototype (0.7). Geometry only: no Qt Widgets / GL.
namespace route3d {

// Small repeatable material maps generated locally; no external images or downloads.
namespace Materials {
inline uint32_t hash(int x,int y,uint32_t seed){
    uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u+seed*2246822519u;
    h=(h^(h>>13))*1274126177u;return h^(h>>16);
}
inline float random(int x,int y,uint32_t seed){return float(hash(x,y,seed)&65535u)/65535.f;}
inline float noise(float u,float v,int cells,uint32_t seed){
    float x=u*cells,y=v*cells;int ix=int(std::floor(x)),iy=int(std::floor(y));
    float a=x-ix,b=y-iy;a=a*a*(3-2*a);b=b*b*(3-2*b);
    auto at=[&](int i,int j){return random((i%cells+cells)%cells,(j%cells+cells)%cells,seed);};
    return (1-b)*((1-a)*at(ix,iy)+a*at(ix+1,iy))+b*((1-a)*at(ix,iy+1)+a*at(ix+1,iy+1));
}
inline QImage create(int material){
    constexpr int size=512;QImage image(size,size,QImage::Format_RGBA8888);
    for(int y=0;y<size;y++)for(int x=0;x<size;x++){
        float u=float(x)/size,v=float(y)/size;
        float grain=random(x,y,19+material),coarse=noise(u,v,16,41),fine=noise(u,v,64,17);
        float r,g,b;
        if(material==0){ // Asphalt aggregate: small neutral grains, no giant cracks.
            float value=61+(grain-.5f)*19+(fine-.5f)*13+(coarse-.5f)*8;
            if(grain>.97f)value+=16;
            r=value;g=value+3;b=value+5;
        }else if(material==1){
            float broad=noise(u,v,5,71),blades=noise(u,v,128,31);
            float value=(grain-.5f)*18+(fine-.5f)*20+(blades-.5f)*18;
            r=91+value+(broad-.5f)*21;g=120+value+(broad-.5f)*19;b=49+value*.65f;
        }else if(material==3){
            // Tileable multi-scale noise for clouds and large-scale colour
            // variation: R = broad, G = fine, B = medium. Contrast stretched.
            auto fbm=[&](int base,uint32_t seed){float s=0,a=.5f,n=0;
                for(int o=0;o<4;o++){s+=a*noise(u,v,base<<o,seed+uint32_t(o));n+=a;a*=.5f;}
                return std::clamp((s/n-.5f)*2.3f+.5f,0.f,1.f);};
            r=fbm(4,101)*255;g=fbm(16,203)*255;b=fbm(8,307)*255;
        }else{
            float pebbles=noise(u,v,100,37);
            float value=(pebbles-.5f)*64+(grain-.5f)*23+(coarse-.5f)*14;
            r=155+value;g=145+value;b=118+value*.85f;
        }
        auto byte=[](float c){return static_cast<uchar>(std::clamp(c,0.f,255.f));};
        uchar* pixel=image.scanLine(y)+x*4;pixel[0]=byte(r);pixel[1]=byte(g);pixel[2]=byte(b);pixel[3]=255;
    }
    return image;
}
}

} // namespace route3d
