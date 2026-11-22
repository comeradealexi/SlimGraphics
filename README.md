# Slim Graphics
A lightweight graphics engine that serves as a playground for graphical techniques, and as a tool to show how graphics
and GPUs work under the hood.

The focus is NOT the most perfect and optimal rendering abstraction: it is designed to make new features easy to add
without getting bogged down in API specifics. For example, most of the pain surrounding barriers is avoided by returning
resources to a generic read state most of the time.

## Backends
A thin rendering abstraction (`sg::`) with two backends; a build contains one, chosen when the project files are generated.
* **D3D12** (D3D12MA): the default, and the only backend with hardware ray tracing and bindless resources so far.
* **Vulkan 1.4** (Vulkan-Hpp, VMA): descriptor heaps, shader objects and dynamic rendering, no pipeline objects. It
  renders the Model Viewer and GPU Driven Rendering scenes identically to D3D12, which the validation harness checks.

## Scenes
Pick what is drawn from the **Scenes** menu.

### Model Viewer
One model drawn with the vertex pipeline, mesh shaders, or amplification + mesh shaders.
* Model and texture loading via Assimp, cache optimisation (meshoptimizer, DirectXMesh) and simplification
* GPU meshlet culling (cone and frustum) and CPU frustum culling
* Visualisations of how the GPU shades: vertex and primitive order, wave intrinsics, pixel order, helper lanes,
  triangle size, meshlets and amplification
* Pipeline feedback through pipeline statistics queries and UAV readback

### GPU Driven Rendering (GDR)
Thousands of instances culled on the GPU and drawn without CPU involvement.
* Per meshlet culling in one compute pass, then one indirect `DispatchMesh` per render bucket (opaque, foliage, terrain),
  each with shaders specialised for it so shaders do not branch on material type
* Textured materials (base colour, normal, specular and opacity maps) in one texture array
* Procedural terrain built by the mesh shader from a height grid every frame
* Point and directional lights, hemisphere ambient, fog and tone mapping
* Generated test scenes (up to ~16k instances) with camera tracks, and a scene editor

### PBR Viewer (D3D12, needs hardware ray tracing)
Physically based rendering of real scenes: Sponza, Bistro (exterior by day and dusk, interior), Sun Temple, Emerald Square
and the Khronos glTF samples. Open them from **Scenes > Open PBR Scene**.
* **Geometry:** two phase occlusion culling (per instance in compute, per meshlet in the amplification shader, against a
  depth pyramid) into a visibility buffer, resolved to a G-buffer with exact barycentrics
* **Ray traced lighting** with inline ray queries: soft sun shadows, diffuse global illumination with a world space
  radiance cache for further bounces, and reflections
* **Emissive surfaces as lights:** a power weighted light table refined by resampled importance sampling, combined with
  reflection rays by multiple importance sampling
* **Denoising:** spatiotemporal variance guided filtering (SVGF) of the shadow, diffuse and specular signals
* **Sky:** a physically based atmosphere (single scattering plus a multiple scattering lookup table)
* **Image:** temporal anti-aliasing, histogram auto exposure, and ACES, AgX and Khronos Neutral tone mapping
* **Reference path tracer** using the same materials, BRDF, sun and sky, to compare the real time image against
* Every setting is a console variable (the **PBR Settings** window, `-cvar=` and test plans): around 100 tweakables
* Debug views of every lighting signal and G-buffer channel, instances, meshlets, triangles, motion vectors and the depth
  pyramid, each explained in the window; GPU pass timings with what each pass does, and a culling funnel
* Camera tracks that fly around each scene, with an in-app editor
* Ideas for what comes next are in [PBRViewer/ROADMAP.md](SlimGraphicsApplication/PBRViewer/ROADMAP.md)

## Using it
The **Help > Controls** window (F1, shown on first launch) lists everything; hover any setting for an explanation.

| Input | Action |
|---|---|
| Right mouse (drag) | Look around |
| W A S D / arrows, Q / E | Move, up / down |
| Shift / Ctrl | Move 5x faster / slower |
| Mouse wheel | Movement speed |
| Space | Play / pause the scene's camera track |
| Tab | Hide the interface (clean screenshots) |
| F12 | Screenshot (to `SlimGraphicsApplication/Screenshots`, or **Capture** menu) |
| F1 | Controls |

The menu bar also shows the frame rate and CPU / GPU times. **View > Performance graph** plots them over time, the
**Slim Graphics** window holds device information, pipeline statistics (each counter explained), post processing and the
magnifier, and **Help > About** lists the backend and the GPU features in use.

## Building & Running
Prerequisites: Visual Studio 2022 (C++ workload), the Vulkan SDK 1.4 (`VULKAN_SDK` set), the Windows "Graphics Tools"
optional feature (D3D12 debug layer), the submodules checked out and the patches in `SubModules/Patches` applied.
* Run `generate_project_files.bat` in the root (add `/vulkan` for the Vulkan backend).
* Open `slimgraphics_vs2022.sln`, set the configuration to `x64` and build and run `SlimGraphicsApplication` (its working
  directory is `SlimGraphicsApplication`).
* Shaders are listed in `SlimGraphicsApplication/*.slimshaderjson` and compiled by the C# shader compiler before each build.

Useful command line switches: `-pbr_scene=Sponza` (or another preset name), `-gdr_scene=PATH`, `-model=NAME`,
`-camera_track`, `-cvar=NAME=VALUE`, `-capture_frame=N -exit_after_capture`, and `-test_plan=PATH -test_output=DIR`.

## Testing
`Tools/validate.ps1` builds both backends and runs the test plan (`Tools/validation_tests.json`, about 140 cases covering
every scene and feature) with the D3D12 and Vulkan validation layers, then compares the two backends' images and
statistics. `-Extended` adds GPU-based, synchronisation and GPU-AV validation. See `CLAUDE.md` for the details.

## Sub Modules
* Sharpmake
* meshoptimizer, DirectXMesh
* D3D12MA, DirectX-Headers, DirectXTK12, DirectXMath
* Vulkan-Hpp, VulkanMemoryAllocator
* Dear ImGui (docking) & ImPlot
* nlohmann json, LodePNG, stb
