# PBR viewer roadmap

Possible improvements to the PBR viewer and its ray tracing, roughly in order of impact. Measurements refer to the
Bistro Exterior and Sponza presets in Debug builds (see the "PBR viewer" section of `CLAUDE.md`).

## Lighting quality

1. **ReSTIR DI (spatiotemporal reuse of light samples).** Lamp lit scenes are still about 8% darker than the path
   tracer at dusk because the denoiser and firefly clamp cut sparse, bright light samples. Reusing reservoirs across
   frames and neighbouring pixels fixes the cause instead of tuning the filter, and makes thousands of lights affordable.
2. **ReSTIR GI or a probe based irradiance volume.** Deep shadow is 5-10% darker than the reference because the radiance
   cache only learns what nearby rays find. Reused indirect samples would give interiors (Sun Temple, Bistro Interior)
   real multiple bounce light.
3. **A hit distance guided denoiser (ReBLUR / ReLAX style).** Separate diffuse and specular filters whose radius follows
   the hit distance would keep contact shadows and sharp reflections that the a-trous filter softens.
4. **Transparency.** Glass, water and wine are cut out or opaque today. Ray traced refraction with tinted shadow rays would
   make Bistro's windows and bottles believable.
5. **Richer materials.** Clear coat (car paint, varnish), sheen (fabric), transmission, multiple scattering energy
   compensation (rough metals are slightly too dark) and textured specular.

## Atmosphere and look

6. **Volumetrics.** Ray marched or froxel fog with ray traced shadows: god rays through Sponza's arches and Sun Temple's
   doors, and aerial perspective so distant geometry fades into the sky.
7. **Physically based camera effects.** Bloom, depth of field, motion blur and lens flare driven by the exposure the viewer
   already computes.
8. **Time of day and clouds.** An animated sun with cloud shadows; the history reset and radiance cache already handle
   changing light.

## Performance and scale

9. **Lower resolution tracing.** Half resolution or checkerboarded rays with upsampling would recover most of the ~25 ms
   the lighting pass costs; DLSS / FSR / XeSS would do the same for the whole frame.
10. **Cheaper alpha tests.** Opacity micromaps (DXR 1.2), or opaque proxies for foliage in shadow rays, would remove the
    per candidate alpha test cost that dominates ray time in foliage.
11. **Compressed textures and streaming.** BC7 / BC5 encoding and streaming would cut Bistro's and Emerald Square's load
    times and memory.
12. **Vulkan support.** VK_KHR_acceleration_structure, ray queries and descriptor heap indexing, so the viewer runs on both
    backends (see "Backend support" in `CLAUDE.md`).

## Showcase

13. **Animated content and night scenes.** Moving objects exercise the motion vectors and reprojection; a night Bistro with
    every lamp lit becomes practical after ReSTIR DI.
14. **A benchmark mode.** Play a preset's camera track at a fixed rate and write per pass timings to a CSV, to track
    performance over time.

The largest single step is ReSTIR DI (1): it closes the biggest remaining gap to the path tracer and makes night scenes
possible. Lower resolution tracing (9) and a better denoiser (3) follow for frame time and image stability.
