// shader_effect.h -- zero-input Direct2D custom effect that runs the LUMEN procedural
// pixel shader (src/core/shaders/lumen_shader.hlsl). Internal to the library.
#pragma once
#include <d2d1_3.h>

namespace lumen {

// Must match the cbuffer layout in lumen_shader.hlsl (16-byte rows).
struct ShaderConstants {
    float size[2];
    float time;
    float kind;
    float scale;
    float intensity;
    float grain;
    float seed;
    float center[2];
    float dpi;
    float color_count;
    float tint[4];
    float back[4];
    float distortion;
    float swirl;
    float shift_red;
    float shift_blue;
    float repetition;
    float softness;
    float contour;
    float angle;
    float metal_distortion;
    float pad[3];
    float colors[8][4];
};
static_assert(sizeof(ShaderConstants) == 256, "cbuffer layout");

// Creates the effect on `dc`, registering the class with the context's factory on first use.
// Returns nullptr when the device cannot run it (e.g. feature level 9.x); callers fall back.
ID2D1Effect* CreateShaderEffect(ID2D1DeviceContext* dc);

// Pushes constants into an effect created by CreateShaderEffect.
bool SetShaderConstants(ID2D1Effect* effect, const ShaderConstants& constants);

} // namespace lumen

#include "lumen/win_undef.h"
