#pragma once

#include "math3d.h"
#include "pixbuf.h"
#include "threadpool.h"

namespace cliviz {

// SDF scene description: function pointer that returns signed distance
using SdfFn = float (*)(vec3 pos, float time);

// Built-in SDF scenes
float sdf_sphere(vec3 pos, float time);
float sdf_rounded_cube(vec3 pos, float time);
float sdf_scene_default(vec3 pos, float time); // composed scene

// Raytrace the SDF scene into the pixel buffer.
// Camera looks from `eye` toward `center`.
void sdf_render(PixelBuffer& pb, SdfFn scene, float time,
                vec3 eye, vec3 center, vec3 up);

// Parallel version using thread pool (row-band partitioning).
void sdf_render_parallel(PixelBuffer& pb, SdfFn scene, float time,
                         vec3 eye, vec3 center, vec3 up,
                         ThreadPool& pool);

} // namespace cliviz
