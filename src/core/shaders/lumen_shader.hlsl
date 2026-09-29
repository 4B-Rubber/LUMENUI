// lumen_shader.hlsl -- procedural light for ShaderView / Painter::DrawShader.
// Compiled at build time by fxc (Windows SDK) into a byte array; no runtime compiler.
// Zero-input Direct2D custom effect: D2D passes SV_POSITION + SCENE_POSITION only.
// Output is premultiplied. Light kinds: rgb = color * v, a = v (v includes intensity), where
// color is tint (monochrome, default) or a palette ramp. MeshGradient / LiquidMetal output
// their own premultiplied color.
//
// MeshGradient and LiquidMetal are HLSL ports of Paper Shaders
// (https://shaders.paper.design, Apache-2.0; see third_party/paper-shaders).
// Modified by LUMEN: GLSL -> HLSL, D2D constant layout, LUMEN intensity / grain / palette
// plumbing, LiquidMetal reduced to its full-canvas (no image / no shape) mode.

cbuffer Constants : register(b0)
{
    float2 size;        // output size in device pixels
    float time;         // seconds (wrapped by the caller)
    float kind;         // 0 mist, 1 glow, 2 dot grid, 3 rays, 4 flow, 5 liquid
    float scale;        // pattern scale multiplier (1 = default)
    float intensity;    // 0..1, already multiplied by theme glow intensity
    float grain;        // 0..1 film grain amount
    float seed;         // pattern offset
    float2 center;      // light center, relative 0..1 of the output rect
    float dpi;          // device pixels per DIP
    float colorCount;   // palette size (0 = monochrome tint)
    float4 tint;        // straight (non-premultiplied) light color, LUMEN keeps it white
    float4 back;        // LiquidMetal background (straight, a = coverage)
    float distortion;   // MeshGradient organic distortion
    float swirl;        // MeshGradient vortex
    float shiftRed;     // LiquidMetal R dispersion
    float shiftBlue;    // LiquidMetal B dispersion
    float repetition;   // LiquidMetal stripe density
    float softness;     // LiquidMetal transition softness
    float contour;      // LiquidMetal edge distortion
    float angle;        // LiquidMetal stripe direction (degrees)
    float metalDistortion;
    float3 pad2;
    float4 colors[8];   // palette, straight RGBA
};

#define PI 3.14159265358979323846

float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm(float2 p)
{
    float sum = 0.0;
    float amp = 0.5;
    // Rotate each octave so the lattice never lines up (no visible grid).
    const float2x2 rot = float2x2(0.80, 0.60, -0.60, 0.80);
    [unroll] for (int i = 0; i < 5; ++i) {
        sum += amp * ValueNoise(p);
        p = mul(rot, p) * 2.02 + 17.0;
        amp *= 0.5;
    }
    return sum;
}

// Three smooth octaves, normalized to ~0..1 (cheap; used by the large-scale fluid kinds).
float Fbm3(float2 p)
{
    float sum = 0.0;
    float amp = 0.5;
    const float2x2 rot = float2x2(0.80, 0.60, -0.60, 0.80);
    [unroll] for (int i = 0; i < 3; ++i) {
        sum += amp * ValueNoise(p);
        p = mul(rot, p) * 2.03 + 11.0;
        amp *= 0.5;
    }
    return sum * (1.0 / 0.875);
}

// Large silk-like folds of light (monochrome take on Paper's Warp / Mesh gradient).
float Flow(float2 uv, float aspect, float t)
{
    float2 p = uv * (1.25 * scale) + seed;
    float2 q = float2(Fbm3(p + float2(0.0, t * 0.11)), Fbm3(p + float2(4.3, 1.7) - t * 0.09));
    float2 r = float2(Fbm3(p + 1.8 * q + float2(1.7, 9.2) + t * 0.07),
                      Fbm3(p + 1.8 * q + float2(8.3, 2.8) - t * 0.08));
    float n = Fbm3(p + 1.6 * r);
    // Thin bright ridges where the warped field crosses iso-levels; soft body elsewhere.
    float ridge = 1.0 - abs(sin(n * 9.0 + t * 0.35));
    ridge = ridge * ridge * ridge;
    float body = smoothstep(0.30, 0.80, n);
    float light = body * (0.30 + 0.70 * ridge) + 0.22 * ridge;
    float2 dc = uv - center * float2(aspect, 1.0);
    light *= 0.35 + 0.65 * exp(-dot(dc, dc) * 0.9);
    return saturate(light);
}

// Chrome bands bending through a flow field (monochrome take on Paper's Liquid metal).
// Low-frequency single-octave warps keep the bands smooth (no lattice kinks).
float Liquid(float2 uv, float aspect, float t)
{
    float2 d = uv - center * float2(aspect, 1.0);
    float2 p = uv * (0.9 * scale) + seed;
    float w = ValueNoise(p * 1.3 + float2(t * 0.10, -t * 0.07));
    float w2 = ValueNoise(p * 0.8 + 1.4 * w + float2(-t * 0.05, t * 0.04));
    float s = (d.y * 1.2 + d.x * 0.3) * 2.2 + 1.6 * w2 + 0.6 * w - t * 0.18;
    float band = 0.5 + 0.5 * sin(s * 6.2831853);
    // Chrome ramp: soft body, bright crest.
    float b2 = band * band;
    float b4 = b2 * b2;
    float crest = b4 * b4;
    float body = smoothstep(0.1, 0.9, band) * 0.30;
    float vign = exp(-dot(d, d) * 0.7);
    return saturate((crest * 0.85 + body) * (0.30 + 0.70 * vign));
}

// Soft domain-warped clouds of light drifting slowly (mesh-gradient feel, monochrome).
float Mist(float2 uv, float t)
{
    // Whole field drifts sideways (reads as flow) while the warp layers churn in place.
    float2 p = uv * (2.2 * scale) + seed + float2(t * 0.10, t * 0.025);
    float2 q = float2(Fbm(p + float2(0.0, t * 0.19)), Fbm(p + float2(5.2, 1.3) - t * 0.15));
    float2 r = float2(Fbm(p + 3.2 * q + float2(1.7, 9.2) + t * 0.13),
                      Fbm(p + 3.2 * q + float2(8.3, 2.8) - t * 0.11));
    float n = Fbm(p + 2.6 * r);
    float light = smoothstep(0.26, 0.88, n);
    return light * light * (0.55 + 0.45 * saturate(length(q)));
}

// Radial light that slowly breathes and orbits around `center`.
float Glow(float2 uv, float aspect, float t)
{
    float2 c = center * float2(aspect, 1.0);
    c += 0.035 * float2(sin(t * 0.37 + seed), cos(t * 0.29 + seed * 1.7));
    float d = length(uv - c) / max(0.25, 0.75 * scale);
    float breathe = 0.92 + 0.08 * sin(t * 0.8);
    return exp(-d * d * 2.4) * breathe;
}

// Dot lattice; a travelling noise field lifts dot brightness and size.
float DotGrid(float2 px, float t)
{
    float pitch = 14.0 * scale * dpi;
    float2 cell = floor(px / pitch);
    float2 local = frac(px / pitch) - 0.5;
    // Noise field sweeps across the lattice (~4 cells/s) ...
    float field = Fbm(cell * 0.09 + float2(t * 0.36, -t * 0.22) + seed);
    // ... while rings of light expand from `center` (~4 cells/s, one every ~2 s, 8 cells apart).
    float2 origin = center * size / pitch;
    float ring = 0.5 + 0.5 * sin(length(cell + 0.5 - origin) * 0.78 - t * 3.14);
    ring = ring * ring * ring * ring;
    float wave = saturate(0.70 * smoothstep(0.40, 0.72, field) + 0.85 * ring);
    float radius = (0.75 + 1.85 * wave) * dpi;
    float dist = length(local * pitch);
    float dotMask = saturate(radius - dist + 0.5);
    return dotMask * (0.10 + 0.90 * wave);
}

// God rays fanning from `center`; rays drift with the clock.
float Rays(float2 uv, float aspect, float t)
{
    float2 c = center * float2(aspect, 1.0);
    float2 d = uv - c;
    // Whole fan sways slowly; individual rays slide and flicker faster.
    float ang = atan2(d.x, d.y) + 0.06 * sin(t * 0.45 + seed);
    float len = length(d);
    float rays = 0.0;
    rays += 0.55 * ValueNoise(float2(ang * 7.0 / scale + t * 0.75, seed));
    rays += 0.30 * ValueNoise(float2(ang * 15.0 / scale - t * 1.10, seed + 3.1));
    rays += 0.15 * ValueNoise(float2(ang * 31.0 / scale + t * 1.70, seed + 7.7));
    rays = smoothstep(0.35, 1.0, rays);
    // Motes of light travel outward along each ray.
    float motes = ValueNoise(float2(ang * 24.0 / scale, len * 9.0 / scale - t * 1.6 + seed));
    float fall = exp(-len * 1.35) * saturate(len * 6.0) * (0.55 + 0.75 * motes);
    float core = exp(-len * len * 40.0) * 0.6;
    return rays * fall + core;
}

// Palette ramp for the light kinds: dark -> colors[0], bright -> colors[n-1].
float3 PaletteRamp(float v)
{
    int n = (int)colorCount;
    if (n <= 1) return colors[0].rgb;
    float x = saturate(v) * (float)(n - 1);
    int i = min((int)floor(x), n - 2);
    float f = smoothstep(0.0, 1.0, x - (float)i);
    return lerp(colors[i].rgb, colors[i + 1].rgb, f);
}

// ---- Paper Shaders ports ----

// GLSL rotate(uv, th) = mat2(cos, sin, -sin, cos) * uv (column-major).
float2 Rotate2(float2 uv, float th)
{
    float c = cos(th);
    float s = sin(th);
    return float2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
}

float2 MeshPosition(int i, float t)
{
    float fi = (float)i;
    float a = fi * 0.37;
    float b = 0.6 + frac(fi / 3.0) * 0.9;
    float c = 0.8 + frac((fi + 1.0) / 4.0);
    return 0.5 + 0.5 * float2(sin(t * b + a), cos(t * c + a * 1.5));
}

// Paper Mesh Gradient: color spots on their own trajectories, organic + vortex distortion,
// inverse-distance (pow 3.5) weighted blend. Without a palette it draws a tint lightness ramp.
float4 MeshGradient(float2 px, float time_s)
{
    float2 uv = px / max(size, float2(1.0, 1.0));
    uv = (uv - 0.5) / max(scale, 0.05) + 0.5;
    float t = 0.5 * (time_s + 41.5 + seed * 7.0);

    float radius = smoothstep(0.0, 1.0, length(uv - 0.5));
    float centerW = 1.0 - radius;
    [unroll] for (int k = 1; k <= 2; ++k) {
        float fk = (float)k;
        uv.x += distortion * centerW / fk * sin(t + fk * 0.4 * smoothstep(0.0, 1.0, uv.y)) *
                cos(0.2 * t + fk * 2.4 * smoothstep(0.0, 1.0, uv.y));
        uv.y += distortion * centerW / fk * cos(t + fk * 2.0 * smoothstep(0.0, 1.0, uv.x));
    }
    float2 ur = Rotate2(uv - 0.5, -3.0 * swirl * radius) + 0.5;

    int n = (int)colorCount;
    bool mono = n < 1;
    if (mono) n = 4;
    float3 color = 0.0;
    float opacity = 0.0;
    float total = 0.0;
    [loop] for (int i = 0; i < 8; ++i) {
        if (i >= n) break;
        float4 c;
        if (mono) {
            // Monochrome fallback: four tint lightness steps.
            float lv = i == 0 ? 0.95 : (i == 1 ? 0.22 : (i == 2 ? 0.6 : 0.08));
            c = float4(tint.rgb * lv, 1.0);
        } else {
            c = colors[i];
        }
        float2 pos = MeshPosition(i, t);
        float d = pow(length(ur - pos), 3.5);
        float w = 1.0 / (d + 1e-3);
        color += c.rgb * c.a * w;
        opacity += c.a * w;
        total += w;
    }
    color /= max(1e-4, total);
    opacity = saturate(opacity / max(1e-4, total));
    return float4(color, opacity);   // premultiplied (Paper: rgb weighted by alpha)
}

float3 Mod289(float3 x) { return x - 289.0 * floor(x / 289.0); }
float2 Mod289v2(float2 x) { return x - 289.0 * floor(x / 289.0); }
float3 Permute(float3 x) { return Mod289(((x * 34.0) + 1.0) * x); }

float SimplexNoise(float2 v)
{
    const float4 C = float4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);
    float2 i = floor(v + dot(v, C.yy));
    float2 x0 = v - i + dot(i, C.xx);
    float2 i1 = (x0.x > x0.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    float4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = Mod289v2(i);
    float3 p = Permute(Permute(i.y + float3(0.0, i1.y, 1.0)) + i.x + float3(0.0, i1.x, 1.0));
    float3 m = max(0.5 - float3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0);
    m = m * m;
    m = m * m;
    float3 x = 2.0 * frac(p * C.www) - 1.0;
    float3 h = abs(x) - 0.5;
    float3 ox = floor(x + 0.5);
    float3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h);
    float3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

float MetalChannel(float c1, float c2, float stripe_p, float3 w, float blur, float bump, float tintC, float tintA)
{
    float ch = lerp(c2, c1, smoothstep(0.0, 2.0 * blur, stripe_p));
    float border = w.x;
    ch = lerp(ch, c2, smoothstep(border, border + 2.0 * blur, stripe_p));
    border = w.x + 0.4 * (1.0 - bump) * w.y;
    ch = lerp(ch, c1, smoothstep(border, border + 2.0 * blur, stripe_p));
    border = w.x + 0.5 * (1.0 - bump) * w.y;
    ch = lerp(ch, c2, smoothstep(border, border + 2.0 * blur, stripe_p));
    border = w.x + w.y;
    ch = lerp(ch, c1, smoothstep(border, border + 2.0 * blur, stripe_p));
    float gradient_t = (stripe_p - w.x - w.y) / w.z;
    float gradient = lerp(c1, c2, smoothstep(0.0, 1.0, gradient_t));
    ch = lerp(ch, gradient, smoothstep(border, border + 0.5 * blur, stripe_p));
    // Tint applied with color burn.
    ch = lerp(ch, 1.0 - min(1.0, (1.0 - ch) / max(tintC, 0.0001)), tintA);
    return ch;
}

// Paper Liquid Metal, full-canvas mode (u_isImage = false, u_shape = none).
float4 LiquidMetal(float2 px, float time_s)
{
    float t = 0.3 * (time_s + 2.8 + seed * 5.0);
    float2 box = max(size / max(dpi, 0.25), float2(1.0, 1.0));   // CSS-pixel-like extent
    float2 ruv = px / max(size, float2(1.0, 1.0)) - 0.5;          // responsive UV, -0.5..0.5

    // Aspect-corrected pattern UV (Paper: v_responsiveUV scaled by the box ratio, y flipped).
    float ratio = box.x / box.y;
    float2 uv = ruv / max(scale, 0.05);
    if (ratio > 1.0) uv.y /= ratio; else uv.x *= ratio;
    uv += 0.5;
    uv.y = 1.0 - uv.y;

    float cycleWidth = repetition * 2.0;

    float2 rotatedUV = uv - 0.5;
    float ang = (-angle + 70.0) * PI / 180.0;
    float cosA = cos(ang);
    float sinA = sin(ang);
    rotatedUV = float2(rotatedUV.x * cosA - rotatedUV.y * sinA, rotatedUV.x * sinA + rotatedUV.y * cosA) + 0.5;

    // Full-fill edge mask: 250 px soft frame.
    float2 borderUV = ruv + 0.5;
    float2 mask = min(borderUV, 1.0 - borderUV);
    float2 thickness = min(250.0 / box, float2(0.5, 0.5));
    float maskX = pow(smoothstep(0.0, thickness.x, mask.x), 0.25);
    float maskY = pow(smoothstep(0.0, thickness.y, mask.y), 0.25);
    float edge = saturate(1.0 - maskX * maskY);
    edge = lerp(smoothstep(0.9 - 2.0 * fwidth(edge), 0.9, edge), edge, smoothstep(0.0, 0.4, contour));

    float opacity = 1.0 - smoothstep(0.9 - 2.0 * fwidth(edge), 0.9, edge);
    edge = 1.2 * edge;

    float diagBLtoTR = rotatedUV.x - rotatedUV.y;
    float diagTLtoBR = rotatedUV.x + rotatedUV.y;
    // Monochrome (no palette) stays neutral grey chrome: no blue cast, no RGB dispersion.
    const bool colored = colorCount >= 0.5;
    float3 color1 = colored ? float3(0.98, 0.98, 1.0) : float3(0.98, 0.98, 0.98);
    float3 color2 = float3(0.1, 0.1, 0.1 + (colored ? 0.1 * smoothstep(0.7, 1.3, diagTLtoBR) : 0.0));

    float2 grad_uv = uv - 0.5;
    float dist = length(grad_uv + float2(0.0, 0.2 * diagBLtoTR));
    grad_uv = Rotate2(grad_uv, (0.25 - 0.2 * diagBLtoTR) * PI);
    float direction = grad_uv.x;

    float bump = pow(abs(1.8 * dist), 1.2);
    bump = 1.0 - bump;
    bump *= pow(saturate(uv.y), 0.3);

    float thin1 = 0.12 / cycleWidth * (1.0 - 0.4 * bump);
    float thin2 = 0.07 / cycleWidth * (1.0 + 0.4 * bump);
    float wide = 1.0 - thin1 - thin2;
    float thin1W = cycleWidth * thin1;
    float thin2W = cycleWidth * thin2;

    float noise = SimplexNoise(uv - t);
    edge += (1.0 - edge) * metalDistortion * noise;

    direction += diagBLtoTR;
    float contourV = 0.0;
    direction -= 2.0 * noise * diagBLtoTR * (smoothstep(0.0, 1.0, edge) * (1.0 - smoothstep(0.0, 1.0, edge)));
    direction *= lerp(1.0, 1.0 - edge, smoothstep(0.5, 1.0, contour));
    direction -= 1.7 * edge * smoothstep(0.5, 1.0, contour);
    direction += 0.2 * pow(contour, 4.0) * (1.0 - smoothstep(0.0, 1.0, edge));

    bump *= clamp(pow(saturate(uv.y), 0.1), 0.3, 1.0);
    direction *= (0.1 + (1.1 - edge) * bump);
    direction *= (0.4 + 0.6 * (1.0 - smoothstep(0.5, 1.0, edge)));
    direction += 0.18 * (smoothstep(0.1, 0.2, uv.y) * (1.0 - smoothstep(0.2, 0.4, uv.y)));
    direction += 0.03 * (smoothstep(0.1, 0.2, 1.0 - uv.y) * (1.0 - smoothstep(0.2, 0.4, 1.0 - uv.y)));
    direction *= (0.5 + 0.5 * pow(saturate(uv.y), 2.0));
    direction *= cycleWidth;
    direction -= t;

    float dispersion = saturate(1.0 - bump);
    float dispR = dispersion;
    dispR += 0.03 * bump * noise;
    dispR += 5.0 * (smoothstep(-0.1, 0.2, uv.y) * (1.0 - smoothstep(0.1, 0.5, uv.y))) *
             (smoothstep(0.4, 0.6, bump) * (1.0 - smoothstep(0.4, 1.0, bump)));
    dispR -= diagBLtoTR;
    float dispB = dispersion * 1.3;
    dispB += (smoothstep(0.0, 0.4, uv.y) * (1.0 - smoothstep(0.1, 0.8, uv.y))) *
             (smoothstep(0.4, 0.6, bump) * (1.0 - smoothstep(0.4, 0.8, bump)));
    dispB -= 0.2 * edge;
    dispR *= colored ? shiftRed / 20.0 : 0.0;
    dispB *= colored ? shiftBlue / 20.0 : 0.0;

    float blur = softness / 15.0 + 0.3 * contourV;
    float3 w = float3(thin1W, thin2W, wide);
    w.y -= 0.02 * smoothstep(0.0, 1.0, edge + bump);

    // Tint: palette[0] (alpha = burn strength); monochrome keeps pure chrome.
    float4 tintC = colored ? colors[0] : float4(1.0, 1.0, 1.0, 0.0);
    float sr = frac(direction + dispR);
    float sg = frac(direction);
    float sb = frac(direction - dispB);
    float r = MetalChannel(color1.r, color2.r, sr, w, blur + fwidth(sr), bump, tintC.r, tintC.a);
    float g = MetalChannel(color1.g, color2.g, sg, w, blur + fwidth(sg), bump, tintC.g, tintC.a);
    float b = MetalChannel(color1.b, color2.b, sb, w, blur + fwidth(sb), bump, tintC.b, tintC.a);

    float3 color = float3(r, g, b) * opacity;
    color += back.rgb * back.a * (1.0 - opacity);
    opacity = opacity + back.a * (1.0 - opacity);
    return float4(color, opacity);
}

float4 main(float4 pos : SV_POSITION, float4 scene : SCENE_POSITION) : SV_TARGET
{
    float2 px = scene.xy;
    float h = max(size.y, 1.0);
    float aspect = size.x / h;
    float2 uv = px / h;

    if (kind > 5.5) {
        float4 c = kind < 6.5 ? MeshGradient(px, time) : LiquidMetal(px, time);
        // Same animated film grain, applied to color (signed, keeps black black).
        float gn = Hash21(floor(px) + frac(time * 7.13) * 91.7) - 0.5;
        c.rgb = clamp(c.rgb + gn * grain * 0.16 * c.a, 0.0, c.a);
        return c * (intensity * tint.a);
    }

    float v;
    if (kind < 0.5) {
        v = Mist(uv, time);
    } else if (kind < 1.5) {
        v = Glow(uv, aspect, time);
    } else if (kind < 2.5) {
        v = DotGrid(px, time);
    } else if (kind < 3.5) {
        v = Rays(uv, aspect, time);
    } else if (kind < 4.5) {
        v = Flow(uv, aspect, time);
    } else {
        v = Liquid(uv, aspect, time);
    }

    // Animated film grain. Signed around 0 so it breaks 8-bit banding without lifting black.
    float g = Hash21(floor(px) + frac(time * 7.13) * 91.7) - 0.5;
    v = saturate(v + g * grain * 0.22 * (0.35 + v));

    float a = v * intensity * tint.a;
    float3 light = colorCount >= 0.5 ? PaletteRamp(v) : tint.rgb;
    return float4(light * a, a);
}