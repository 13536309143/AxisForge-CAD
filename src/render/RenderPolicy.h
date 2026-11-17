#pragma once
#include "core/Geometry.h"
#include <array>
#include <cmath>
namespace af {
// Homogeneous clip-space test: reject only if all eight corners lie outside one plane.
inline bool outsideFrustum(const Bounds &bounds, const QMatrix4x4 &mvp) {
    if (!bounds.valid) return false;
    std::array<QVector4D, 8> points;
    for (int i=0;i<8;++i) {
        points[i]=mvp*QVector4D(i&1?bounds.max.x():bounds.min.x(),
                              i&2?bounds.max.y():bounds.min.y(),
                              i&4?bounds.max.z():bounds.min.z(),1);
        for(int j=0;j<4;++j) if(!std::isfinite(points[i][j])) return false;
    }
    for(int plane=0;plane<6;++plane) {
        bool outside=true;
        for(const auto &p:points) {
            const float d=p.w()+(plane&1?-p[plane/2]:p[plane/2]);
            if(d>=-1e-5f) {outside=false;break;}
        }
        if(outside) return true;
    }
    return false;
}
inline int screenLod(float diameter, int previous) {
    // Hysteresis avoids rapid switching near a threshold.
    if(previous==0 && diameter>=160) return 0;
    if(previous==2 && diameter<=88) return 2;
    if(diameter>200) return 0;
    if(diameter<64) return 2;
    return 1;
}
}
