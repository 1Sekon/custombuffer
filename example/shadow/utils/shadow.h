// utils/shadow.h  - the only shadow file; included by materials/RenderChunk and materials/Actor
//   vertex shaders:   #include <utils/shadow.h>
//   fragment shaders: #define SHADOW_RECEIVER 1   (before the include, adds shadowLit())
// Transparent shadows work like Iris/OptiFine: ShadowMap = opaque depth (shadowtex1), ShadowAll = depth of everything
// (shadowtex0), ShadowColor = colour + alpha of the nearest caster (shadowcolor0).
#ifndef UTILS_SHADOW_H
#define UTILS_SHADOW_H

uniform vec4 CameraPosition;       // camera world position
uniform vec4 LastCameraPosition;   // camera world position of the previous frame
uniform vec4 GameTimeOfDay;        // x = time of day (fraction of a day)
uniform vec4 ShadowPass;           // x = 1 while the mod redraws this OPAQUE geometry into the shadow map
uniform vec4 ShadowTransparentPass;// x = 1 while the mod redraws this TRANSPARENT geometry (glass, water) into the shadow map
uniform vec4 ShadowRes;            // x = map resolution (texels per side), y = depth bits of the map (16 default, 24, 32). z, w unused. (bgfx has no vec2 uniforms)
uniform vec4 ShadowCull;           // x = map radius (blocks), y = depth range (blocks), z = 1: the mod skips chunk draws outside the map (uniforms/ShadowCull.json)

// ---- settings ----
#define SHADOW_RADIUS_NEAR 16.0
#define SHADOW_RADIUS      ShadowCull.x   // blocks from the camera to the map edge (set in uniforms/ShadowCull.json, the mod culls with the same value)
#define SHADOW_DEPTH_RANGE ShadowCull.y   // blocks along the sun direction (+/-)
#define SHADOW_BRIGHTNESS  0.55    // light level inside shadow (1 = no shadows)
#define SHADOW_BIAS        0.12    // blocks. Raise for acne, lower for peter panning
#define SHADOW_SLOPE_BIAS  0.6     // extra bias on faces nearly parallel to the sun
#define SHADOW_TAPS        4       // samples per pixel: 1 = hard edge, 8-16 = soft edge
#define SHADOW_BLUR        0.12    // soft edge radius in blocks
#define SHADOW_FACE_FADE   0.15    // how fast a face goes dark as it turns away from the sun
#define SHADOW_EDGE_FADE   0.85    // where (fraction of the radius) the shadow fades out
#define SHADOW_GLASS_TINT  0.85    // how strongly glass/water colours the light (0 = none, 1 = full colour)
#define SHADOW_GLASS_DARK  0.30    // constant darkening of light passing through glass (same everywhere, ignores texture alpha)
#define SHADOW_TRANSPARENT_RECEIVE 0  // 1 = glass/water also receive shadows (extra lookups on every overdrawn layer), 0 = faster
#define SHADOW_COLORED     1       // 1 = transparent things (stained glass, water) tint the light, 0 = off (saves two samplers)
#define SHADOW_RES         ShadowRes.x        // map resolution (default set in uniforms/ShadowRes.json, the mod reads the same value)
#define SHADOW_DISTORT     0.10    // 0 = even texels, small = more texels near the player (0.05-0.2)
#define SHADOW_SUN_STEP    0.5     // degrees per sun update. The map is read one frame late, so each step is a one-frame error of (step x distance); smaller = smaller pop, more frequent
#define SHADOW_SUN_GLIDE   0.0     // 0 = hard steps (recommended: no moving sun between steps, so no edge crawl and no map/lookup mismatch). >0 = ease share of each step (reintroduces crawl)
#define SHADOW_DEBUG_FREEZE_SUN -1.0  // DIAGNOSTIC: 0..1 freezes the sun at that time of day (0.25 = afternoon). -1 = off. If flicker stays with the sun frozen it is NOT the sun
#define SHADOW_SUN_FADE    vec2(0.10, 0.35)  // sun height (L.y) where shadows fade in. Wide, so the day/night flip of L happens while shadows are invisible
#define pi 3.14159265359

// Which passes take part. The caster uniforms must only be USED in the pass they belong to:
// unused uniforms are removed by the compiler, and the mod only redraws programs that still contain them.
//   SHADOW              opaque / alpha-tested: casts into the opaque map (ShadowPass) and receives
//   SHADOW_TRANSPARENT  transparent: casts its colour into the all-casters map (ShadowTransparentPass) and receives
//   SHADOW_RECEIVE      either of the two
#if !defined(DEPTH_ONLY) && !defined(DEPTH_ONLY_OPAQUE)
  #if defined(OPAQUE) || defined(ALPHA_TEST)
    #define SHADOW 1
    #define SHADOW_RECEIVE 1
  #elif defined(TRANSPARENT)
    #define SHADOW_TRANSPARENT 1
    #if SHADOW_TRANSPARENT_RECEIVE
      #define SHADOW_RECEIVE 1
    #endif
  #endif
#endif

vec3 sunDirection() {
  float steps = 360.0 / SHADOW_SUN_STEP;
  float t = ((SHADOW_DEBUG_FREEZE_SUN >= 0.0) ? SHADOW_DEBUG_FREEZE_SUN : fract(GameTimeOfDay.x)) * steps;
  float f = floor(t + 0.001);
  if (SHADOW_SUN_GLIDE > 0.0) { f += smoothstep(1.0 - SHADOW_SUN_GLIDE, 1.0, fract(t)); }
  float a = 2.0 * pi * (f / steps);
  vec3 s = vec3(-sin(a), cos(a), 0.0);
  return (s.y > 0.0) ? s : -s;
}

vec4 shadowClip(vec3 q, vec3 camAbs, vec3 L, float radius) {
  vec3 right = vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(L.y, -L.x, 0.0);
  float texel = 2.0 * radius / SHADOW_RES;
  vec2 snap = fract(vec2(dot(camAbs, right), dot(camAbs, up)) / texel) * texel;
  vec2 xy = vec2(dot(q, right), dot(q, up)) + snap;
  return vec4(xy / radius, -dot(q, L) / SHADOW_DEPTH_RANGE, 1.0);
}

vec4 shadowCaster(vec3 rel, vec3 L, float cascade) {
  float radius = (cascade < 0.5) ? SHADOW_RADIUS_NEAR : SHADOW_RADIUS;
  return shadowClip(rel, CameraPosition.xyz, L, radius);
}

vec4 shadowReceiver(vec3 rel, vec3 L) {
  vec3 q = rel + (CameraPosition.xyz - LastCameraPosition.xyz);
  vec4 sc = shadowClip(q, LastCameraPosition.xyz, L, SHADOW_RADIUS);
  return vec4(sc.xyz * 0.5 + 0.5, smoothstep(SHADOW_SUN_FADE.x, SHADOW_SUN_FADE.y, L.y));
}

#if defined(SHADOW_RECEIVE) && defined(SHADOW_RECEIVER)

SAMPLER2D_AUTOREG(s_ShadowMap);
#if SHADOW_COLORED
SAMPLER2D_AUTOREG(s_ShadowAll);
SAMPLER2D_AUTOREG(s_ShadowColor);
#endif

highp vec3 shadowTap(highp vec2 uv, highp float z) {
  #if SHADOW_COLORED
    if (step(z, texture2D(s_ShadowAll, uv).r) > 0.5) { return vec3_splat(1.0); }
    if (step(z, texture2D(s_ShadowMap, uv).r) < 0.5) { return vec3_splat(0.0); }
    highp vec4 c = texture2D(s_ShadowColor, uv);
    highp vec3 hue = c.rgb / max(max(c.r, max(c.g, c.b)), 0.001);
    return mix(vec3_splat(1.0), hue, SHADOW_GLASS_TINT) * (1.0 - SHADOW_GLASS_DARK);
  #else
    return vec3_splat(step(z, texture2D(s_ShadowMap, uv).r));
  #endif
}

highp vec3 sampleCascadePCF(highp vec2 uvBase, highp float z, highp vec2 uvMin, highp vec2 uvMax) {
  highp vec2 stepUV = vec2(0.25 / SHADOW_RES, 0.5 / SHADOW_RES);
  highp vec3 sum = shadowTap(clamp(uvBase + vec2(-stepUV.x,  stepUV.y), uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2( stepUV.x, -stepUV.y), uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2(-stepUV.x, -stepUV.y), uvMin, uvMax), z);
  sum += shadowTap(clamp(uvBase + vec2( stepUV.x,  stepUV.y), uvMin, uvMax), z);
  return sum * 0.25;
}

highp vec3 shadowLit(highp vec4 s, highp float ndl) {
  if (s.w <= 0.0) { return vec3_splat(1.0); }

  highp float face = mix(1.0, smoothstep(0.0, SHADOW_FACE_FADE, ndl), s.w);
  if (face <= 0.0) { return vec3_splat(0.0); }

  highp vec2 p1 = s.xy * 2.0 - 1.0;
  if (abs(p1.x) >= 1.0 || abs(p1.y) >= 1.0) { return vec3_splat(face); }

  vec3 L = sunDirection();
  vec3 right = vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(L.y, -L.x, 0.0);
  vec2 camLight = vec2(dot(LastCameraPosition.xyz, right), dot(LastCameraPosition.xyz, up));
  float texel0 = 2.0 * SHADOW_RADIUS_NEAR / SHADOW_RES;
  float texel1 = 2.0 * SHADOW_RADIUS / SHADOW_RES;
  vec2 snap0 = fract(camLight / texel0) * texel0;
  vec2 snap1 = fract(camLight / texel1) * texel1;
  highp vec2 p0 = p1 * (SHADOW_RADIUS / SHADOW_RADIUS_NEAR) + (snap0 - snap1) / SHADOW_RADIUS_NEAR;

  highp float slope = clamp(sqrt(max(1.0 - ndl * ndl, 0.0)) / max(ndl, 0.1), 0.0, 5.0);
  highp float z0 = s.z - (SHADOW_BIAS + texel0 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);
  highp float z1 = s.z - (SHADOW_BIAS + texel1 * slope * SHADOW_SLOPE_BIAS) / (2.0 * SHADOW_DEPTH_RANGE);

  highp vec2 uv0 = vec2((p0.x * 0.5 + 0.5) * 0.5, p0.y * 0.5 + 0.5);
  highp vec2 uv1 = vec2((p1.x * 0.5 + 0.5) * 0.5 + 0.5, p1.y * 0.5 + 0.5);

  highp vec2 uvMin0 = vec2(0.25 / SHADOW_RES, 0.5 / SHADOW_RES);
  highp vec2 uvMax0 = vec2(0.5 - 0.25 / SHADOW_RES, 1.0 - 0.5 / SHADOW_RES);
  highp vec2 uvMin1 = vec2(0.5 + 0.25 / SHADOW_RES, 0.5 / SHADOW_RES);
  highp vec2 uvMax1 = vec2(1.0 - 0.25 / SHADOW_RES, 1.0 - 0.5 / SHADOW_RES);

  highp float d0 = max(abs(p0.x), abs(p0.y));
  highp vec3 lit;
  if (d0 < 1.0) {
    highp vec3 lit0 = sampleCascadePCF(uv0, z0, uvMin0, uvMax0);
    if (d0 > 0.80) {
      highp vec3 lit1 = sampleCascadePCF(uv1, z1, uvMin1, uvMax1);
      lit = mix(lit0, lit1, smoothstep(0.80, 1.0, d0));
    } else {
      lit = lit0;
    }
  } else {
    lit = sampleCascadePCF(uv1, z1, uvMin1, uvMax1);
  }

  highp vec2 c = abs(p1);
  highp float edge = 1.0 - smoothstep(SHADOW_EDGE_FADE, 1.0, max(c.x, c.y));
  return min(mix(vec3_splat(1.0), lit, edge * s.w), vec3_splat(face));
}

#endif // SHADOW_RECEIVE && SHADOW_RECEIVER
#endif // UTILS_SHADOW_H
