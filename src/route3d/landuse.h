#pragma once
#include "world_noise.h"

namespace route3d {

// Land use shared by the terrain (lakes stay out of forest), vegetation and
// buildings, so houses never stand in forest.
namespace Landuse {
inline double forest(double x,double z){
    return WorldNoise::smoothstep(.50,.62,WorldNoise::fbm(x/300,z/300,3,11));
}
// Hamlets: areas where farms and houses cluster along the road.
inline double hamlet(double x,double z){
    return WorldNoise::smoothstep(.50,.64,WorldNoise::fbm(x/650,z/650,3,91));
}
// Grazing land (cows, sheep, horses behind fences) and hay fields (bales).
inline double pasture(double x,double z){
    return WorldNoise::smoothstep(.53,.60,WorldNoise::fbm(x/240,z/240,3,131));
}
inline double hayfield(double x,double z){
    return WorldNoise::smoothstep(.55,.62,WorldNoise::fbm(x/280+7.1,z/280+3.3,3,137))*(1-pasture(x,z));
}
}

} // namespace route3d
