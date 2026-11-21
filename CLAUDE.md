# CLAUDE.md

Slim Graphics is a Windows C++20 graphics playground: a thin rendering abstraction (`sg::`) with two backends, D3D12
(`SlimGraphicsD3D12`) and Vulkan 1.4 (`SlimGraphicsVulkan`). A build contains exactly one backend, chosen when the project
files are generated. Both backends must stay functional and render identically, except for the PBR viewer (hardware ray
tracing and bindless resources), which is D3D12 only for now (see "PBR viewer").

## Repository map

| Path | Contents |
|---|---|
| `SlimEngine` | Core types (`sgTypes.h`: resource/pipeline/blend/depth descs, `BaseBinding`), logging and asserts (`seEngine.h`), window, input |
| `SlimGraphicsShared` | Backend independent helpers (`UploadHeap`, `sgUtils`) |
| `SlimGraphicsD3D12` | D3D12 backend (D3D12MA) |
| `SlimGraphicsVulkan` | Vulkan backend (Vulkan-Hpp RAII, VMA) |
| `SlimGraphicsApplication` | The app (model viewer, GPU driven rendering, post process, magnifier, texture viewer), HLSL and `*.slimshaderjson` |
| `SlimGraphicsApplication\PBRViewer` | Physically based viewer: visibility buffer, ray traced lighting, denoiser, path tracer (D3D12 only) |
| `SlimGraphicsApplication\CVars.h` | Console variables: self registering settings with an ImGui panel, `-cvar=` and test plan support |
| `SlimShaderCompiler` | C# tool that drives DXC. The build runs the copy in `Binaries\` |
| `Sharpmake` | Project generation (`main.sharpmake.cs`; `/vulkan` selects the Vulkan backend) |
| `SubModules` | Third party code. Local fixes live in `SubModules\Patches` (see below) |
| `SlimGraphicsAssets\GDRAssets` | GPU driven rendering scenes (`*Test.json`; the folder is gitignored, so `git add -f` new scenes) |
| `Tools\validate.ps1` | Build, validation layer and D3D12/Vulkan parity harness |
| `Tools\validation_tests.json` | The test plan `validate.ps1` runs (see "Test cases") |
| `Tools\generate_gdr_scenes.py` | Deterministic generator for the GDR test scenes and their camera tracks |

## Prerequisites

Visual Studio 2022 (C++ workload), the Vulkan SDK 1.4 (`VULKAN_SDK` set), the Windows "Graphics Tools" optional feature
(D3D12 debug layer), submodules checked out and the patches in `SubModules\Patches` applied.

## Building and running

Generate projects (D3D12 by default, `/vulkan` for Vulkan), then build. In automation call Sharpmake directly
(`generate_project_files.bat` pauses on error):

```powershell
cd Sharpmake; .\Sharpmake.Application\Debug\net6.0\Sharpmake.Application.exe "/sources('main.sharpmake.cs')" /generateDebugSolution "/debugSolutionPath('sharpmake_projects')" /vulkan; cd ..
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe
& $msbuild Sharpmake\projects\slimgraphicsapplication_vs2022_win64.vcxproj -p:Configuration=Debug -p:Platform=x64 -m -noconsolelogger "-flp:logfile=build.log;verbosity=minimal"
```

- Send MSBuild output to a file logger. Never pipe MSBuild (or any long running tool) into `Select-Object -First`: the
  tool blocks on the full pipe and the build hangs.
- The executable is `Sharpmake\projects\output\win64\<config>\slimgraphicsapplication.exe` and must run with
  `SlimGraphicsApplication` as the working directory.
- Validation layers, `seAssert` and `seWriteLine` logging exist only in Debug (`_DEBUG`). Release is a compile check.
- Command line switches (`main.cpp`): `-capture_frame=N`, `-capture_dir=PATH`, `-exit_after_capture`, `-model=TEXT`,
  `-mesh_mode=0|1|2` (vertex / mesh / amplification + mesh), `-gdr_scene=PATH`, `-camera_track` (play the scene's camera
  track), `-camera_track_time=T` (start it at T seconds), `-test_plan=PATH` / `-test_output=DIR` (run a test plan and
  quit, see "Test cases"), `-pbr_scene=NAME` (open the PBR viewer with a preset such as `Sponza` or `Bistro Exterior`, or a
  model path) and `-cvar=NAME=VALUE` (repeatable, e.g. `-cvar=pbr.mode=1`).
- Interface (`main.cpp`): a main menu bar (Scenes incl. "Open PBR Scene", Camera, Capture, View, Help) with FPS and CPU / GPU
  times. Keys: F1 controls, F12 screenshot (the tests' captures, to `SlimGraphicsApplication\Screenshots`, gitignored), Tab
  hides the interface (drawn transparent, ignores the mouse), Space plays the camera track. The camera (`Camera.cpp`)
  flies with right mouse look, WASD / arrows, Q / E, Shift / Ctrl and wheel speed, read from ImGui so typing into fields
  does not move it; PBR presets set its speed from the scene size. The Controls window opens on first launch (no
  `imgui.ini`). Windows get first use positions from `ImGuiLayout.h` (`place_window_first_use`); new windows should too,
  and anything a user sees should explain itself (tooltips, short descriptions, as the PBR views and passes do).
- Environment: `SE_LOG_FILE=path` copies engine output (including validation messages) to a file,
  `SG_VULKAN_VALIDATION_LOG=path` copies Vulkan validation messages, `SG_D3D12_GPU_VALIDATION=1` enables D3D12
  GPU-based validation, and `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` / `VK_KHRONOS_VALIDATION_GPUAV_ENABLE=true` /
  `VK_KHRONOS_VALIDATION_VALIDATE_BEST_PRACTICES=true` turn on the extra Vulkan layer checks.

## Validation: required before calling graphics work done

```powershell
powershell -ExecutionPolicy Bypass -File Tools\validate.ps1            # both backends, core validation, parity
powershell -ExecutionPolicy Bypass -File Tools\validate.ps1 -Extended  # + D3D12 GPU-based, Vulkan sync and GPU-AV passes
```

- The script builds each backend, then runs the whole test plan (`Tools\validation_tests.json`) in one application
  session per backend and validation pass (`-Extended` passes are separate sessions because layer settings are fixed at
  device creation). The work is done only when it prints `VALIDATION PASSED` (exit code 0). That requires:
  - clean builds (any warning in `SlimGraphicsVulkan` fails);
  - every session exiting with code 0, logging that its validation layer loaded and `[TEST] COMPLETE`;
  - every case completing and writing its captures, with zero `[Vulkan|D3D12 ERROR|WARNING|CORRUPTION]`, `Assert:` or
    `[TEST] ERROR` lines (messages are attributed to the case that was running, between `[TEST] BEGIN` and `END`);
  - D3D12 and Vulkan `_final`, `_output` and `_magnifier` captures differing in at most 0.05% of pixels, and their
    `[TEST] STAT` lines agreeing (GDR scene totals and input assembler counts exactly; culling feedback, other pipeline
    statistics and shader counters within 2% plus a small absolute slack). Cases marked `"nondeterministic"` (GPU
    scheduling dependent visualisations) are not compared.
  - cases limited to some `"backends"` are skipped (and must log `[TEST] SKIP`) on the others; they are not compared.
- Iterate with `-Backends Vulkan`, `-Cases "gdr_*","mv_mesh*"` (wildcards) and `-SkipBuild` (reuses the executables the
  last full run saved as `slimgraphicsapplication_<backend>.exe`), then finish with a full run.
- Use `-Extended` whenever backend code (barriers, descriptors, synchronisation, swap chain, memory) or shaders change,
  and `-BuildRelease` before committing backend changes.
- Look at the captures and diff images in the output directory (`%TEMP%\SlimGraphicsValidation`: `<Backend>\<pass>\` and
  `parity\`) when rendering changed. Back buffer captures include ImGui with live timings and are only reported.
- Never make a validation message go away by filtering it, disabling a layer or check, or loosening a threshold: fix the
  cause. The only filtered message is `D3D12_MESSAGE_ID_FENCE_ZERO_WAIT`, which ImGui's DX12 multi-viewport code
  triggers. Only add to that list with evidence that the message comes from outside the engine.
- New features need coverage: add a case to `Tools\validation_tests.json` (see "Test cases"). Things the plan does not
  cover (ImGui interaction, magnifier mouse input, camera track playback UI, screenshots, the menu bar, camera mouse and keyboard controls,
  window resize and minimise) must be checked
  by hand when touched.
- Best practices validation is advisory. Its known, accepted warnings: one memory block per resource (the D3D12 placed
  resource model) and UNDEFINED initial layout transitions.

## Test cases

- `TestRunner` (`SlimGraphicsApplication\TestRunner.h`) runs a plan: `{"defaults": {...}, "cases": [{"name": ...}, ...]}`.
  Each case is merged over the defaults, so it fully describes its state and does not depend on case order. Keys:
  `frames`, `model_viewer`, `gdr`, `gdr_scene` (empty clears it), `camera` (`"default"`, `{"position", "target", "near",
  "far"}` or `{"track_time": T}`), `depth_clear`, `debug_draw`, `model_viewer_settings` (`ModelViewer::ApplyTestSettings`),
  `gdr_settings` (`gdr::Scene::apply_test_settings`), `post_process`, `magnifier`, `capture_magnifier`,
  `nondeterministic`, `timing_dependent_stats`, `pbr`, `pbr_settings` (`{"scene": preset or path, "settings": {cvar:
  value}}`, `pbr::Renderer::apply_test_settings`; cvars not given are reset) and `backends` (e.g. `["D3D12"]` for features
  one backend does not implement yet). `camera` may also be `"scene"`: the PBR scene preset's view.
- PBR cases count their `frames` once the scene has loaded (large scenes take a while) and log `pbr_*` statistics.
- `timing_dependent_stats` lists statistics a case cannot compare between backends because they depend on GPU timing; its
  images and other statistics are still gated. Only use it with evidence (the same backend disagreeing with itself across
  runs). Today: `ps_invocations` for scenes with alpha tested materials, whose pixel shader can discard (no early depth
  rejection).
- To cover a new feature, expose its state through one of the settings functions, list the new key in `validate_case`
  (`TestRunner.cpp` rejects plans with unknown keys, so typos cannot silently test defaults), add cases and run
  `validate.ps1`. Check the new captures by eye: parity alone passes if a feature renders nothing on both backends.
- Run a plan by hand: `slimgraphicsapplication.exe -test_plan=..\Tools\validation_tests.json -test_output=<dir>` from
  `SlimGraphicsApplication`.

## GDR scenes and camera tracks

- `Tools\generate_gdr_scenes.py` writes `MixedAssetsTest`, `VillageTest`, `ForestTest` and `FractalTest` (each roughly
  4x the previous; FractalTest has ~16k instances and takes ~20 s to load in Debug) and adds tracks to `HeadsTest` and
  `SpheresTest`. Every scene gets a terrain (flat under the content and camera track, hills beyond), lights and an
  environment (`decorate_*`). Its output is deterministic: change the generator and regenerate rather than editing the JSON.
- A scene's optional root `camera_track` (`gdrJSON.h`: `duration`, `near_plane`, `far_plane`, `points` of `position` /
  `target`) is a closed centripetal Catmull-Rom spline played at constant speed (`gdrCameraTrack.h`). Playback
  (`CameraTrackPlayer`) advances a fixed 1/60 s per frame, so a given frame or `track_time` always gives the same view.
- Keep track points clear of geometry and targets at least a few units ahead; validation cases pose cameras on them.

## GDR rendering: materials, terrain and lighting

- Everything the GDR scene draws goes through one per-meshlet culling pass and one indirect `DispatchMesh` per render
  bucket (`GDR_BUCKET_*`): opaque, foliage (alpha tested and / or double sided: no back face culling, no cone culling,
  pixel shader with `discard`) and terrain. A mesh part's bucket comes from its material (`bucket_of_material`). Culling
  appends visible meshlets to their bucket's region of the visible list (one counter and indirect argument per bucket),
  each region is sorted on its own, and each bucket has mesh and pixel shaders specialised for it (`GDR_SHADER_TERRAIN`,
  `GDR_SHADER_FOLIAGE`), so shaders do not branch on the meshlet or material type. Add a bucket for new pipeline state;
  per-material data within a bucket is still looked up per meshlet / instance. `gdr_settings.render_buckets` turns buckets
  off for tests.
- Materials (`gdrMaterials.h`): each assimp material becomes a `MaterialData` (diffuse / base colour, specular, Blinn-Phong
  exponent, diffuse, specular, normal or bump (OBJ `map_bump`) and opacity textures, external or embedded). All textures
  are resampled to `MaterialTextures::kSliceSize` and stored as slices of one `Texture2DArray`
  (`sg::ResourceDimension::Texture2DArray`), so shaders never index resources non-uniformly. Slices upload within a per
  frame budget because the upload heap is per frame. DDS textures are not decoded (they fall back to untextured).
- Alpha testing happens only in the foliage bucket's pixel shader (`GDR_PixelShaderFoliage`): `discard` disables early
  depth rejection for the whole pipeline. Back faces of double sided materials are lit with the normal mirrored through
  the surface. Opacity is read at an explicit mip level with
  the trilinear sampler s3: anisotropic filtering of an explicit level is implementation defined and made D3D12 and Vulkan
  cut out different edge pixels. Samplers s0-s2 are anisotropic (registered in `main.cpp`).
- Terrain (`gdrTerrain.h`): a node's `terrain` component is a model whose meshlets are 7x7 quad tiles. Only the height grid
  is generated (at load, never saved); the mesh shader builds vertices, normals and triangles from it every frame.
  `gdr::terrain_tile_triangle` and `TerrainTileTriangle` (HLSL) must stay identical. Terrain shading (grass, rock, sand,
  snow, water) is procedural in `ShadeTerrain`.
- Lighting: a node's `light` component (point at the node's position, or directional along `direction` rotated by the
  node) plus the scene's `environment` (hemisphere ambient, fog, exposure). Up to `GDR_MAX_LIGHTS` lights are looped per
  pixel; lighting is linear with authored colours in gamma space, tone mapped at the end.
- Scene components are validated against their defaults on load (`matches_schema` in `gdrJSON.cpp`): unknown keys or
  wrong types fail the load instead of aborting. New component fields only need a default and an entry in the
  `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` list.
- Debug render modes (GDR window, test `gdr_settings.render_mode` with `debug_mode` 1): 1 sort order, 2 albedo, 3 normals,
  4 lighting on a white surface. Use them when checking texture, normal or lighting changes.

## PBR viewer (D3D12 only)

- `pbr::Renderer` (`PBRViewer\pbrRenderer.h`) with `pbr::Scene` (`pbrScene.h`). Presets (`scene_presets`): Sponza, Bistro
  Exterior / Interior, Sun Temple, Emerald Square, Damaged Helmet, Sci-Fi Helmet, Metal Rough Spheres, PICA PICA Robot.
  FBX presets use the file's camera (assimp's left handed conversion leaves its direction reversed; `import` corrects it).
- Loading: assimp import, meshlets and texture decoding on a worker thread, then uploads within a per frame budget, a GPU
  pass that finds alpha tested materials (FBX / OBJ files do not say; compressed DDS alpha needs the GPU), and one BLAS
  per mesh (packed into pool buffers) plus one TLAS. Materials: glTF metal / roughness, ORCA FBX "specular" textures as
  occlusion / roughness / metallic (`specular_is_orm`), and OBJ Phong maps converted. Textures are bindless
  (`ResourceDescriptorHeap`). Mostly transparent materials are cut out (there is no transparency).
- Frame: two phase occlusion culling (instance compute pass and per meshlet amplification shader against an HZB of the
  early pass, with per instance / meshlet visibility from the previous frame), one indirect DispatchMesh per bucket
  (opaque, masked) into a visibility buffer (instance and triangle ids), a resolve to a G-buffer (exact barycentrics and
  texture gradients from camera rays), ray traced sun shadows, diffuse GI and reflections (1 sample per pixel, inline ray
  queries) with a world space radiance cache for further bounces, SVGF denoising, composite, TAA, auto exposure and tone
  mapping. Sky: single scattering atmosphere plus Hillaire's multiple scattering LUT. Reverse Z, infinite far plane.
- The path tracer (`pbr.mode` "Path Traced") shares the materials, BRDF (`pbrSurface.hlsli`), sun and sky, so it is the
  reference: with a fixed exposure and the clamp tone mapper, real time lighting is within about 1% of it in sunlit
  areas and ~5-10% darker in deep shadow (the radiance cache only reaches what nearby rays find). Bistro Exterior (Dusk)
  (lamps and sky) is about 8% darker: the firefly clamp and the denoiser's luminance edge stopping both cut sparse, bright
  samples. Compare with `pbr.light.firefly_clamp` 0 in both modes (the path tracer clamps indirect paths too).
- Emissive surfaces are lights: triangles are sampled from a power weighted alias table, refined by resampled importance
  sampling (`pbr.light.emissive_candidates`, fewer at indirect hits), with MIS against BRDF rays that hit emitters. Both
  strategies' MIS weights use the table's density, so they sum to one and stay unbiased (the path tracer gives the same
  image with 1 or many candidates). Denoiser history is the unfiltered running mean by default
  (`pbr.denoise.filtered_history` restores SVGF's feedback). Per pixel moments miss rare samples, so the first a-trous
  step floors the variance with the 3x3 neighbourhood's (`pbr.denoise.spatial_variance`).
- Real time ray cost (Bistro Exterior, Debug, lighting pass ~25 ms by default): per pixel a sun shadow ray, a diffuse ray, a
  reflection ray and an emissive light sample (shadow ray), each indirect hit adding a sun shadow ray, plus cache
  propagation rays for 1 pixel in `pbr.cache.update_interval`. Cheaper options, measured against the path tracer:
  `pbr.light.specular_max_roughness` 0.6 (rough surfaces reuse the diffuse ray, about -10 ms, within 1-3%),
  `pbr.cache.propagation` off (-6 ms); `pbr.light.emissive_at_hits` (off by default, +9 ms for <1%) and
  `pbr.light.sun_shadows_at_hits` (off is 30% too bright) are for experiments. The radiance cache is cleared whenever
  history resets (preset switch, a cvar marked reset-history), or it would hold the old lighting for many frames.
- Camera tracks: each preset names a track file (`PBRViewer\CameraTracks\*.json`, the GDR `CameraTrack` format, metres)
  or orbits the scene's bounds. While the viewer is enabled its track replaces the GDR scene's in the Slim Graphics
  window's Camera Track player, `-camera_track` and test `{"track_time": T}` cameras. The PBR Viewer window's Camera
  Track header (`pbr::SceneCameraTrack`) edits and saves them ("Add point at camera"). Check edited tracks with captures along them: top down
  `pbr.debug_view` Albedo views (a camera `near` plane above the floor cuts roofs off interiors) make good maps.
- Every setting is a `pbr.*` cvar (window "PBR Settings"); debug views (`pbr.debug_view`) show lighting signals (raw and
  denoised, the radiance cache) and data (G-buffer channels, history length, HZB, ids); `PBR_DEBUG_*` keeps the lighting
  views before `PBR_DEBUG_FIRST_DATA_VIEW`, and the cvar's options follow that order. Statistics and pass timings are in
  the "PBR Viewer" window, and test cases log them (`pbr_gpu_ms`, `pbr_ms_<pass>`; timings vary a lot between runs,
  Debug runs at stable, low clocks).
- Code layout: every pass has a typed constant buffer in `pbrShaderStructures.h` (`LightingConstants`, `DenoiseConstants`,
  ...) shared with its shader. `Renderer` records through `dispatch` / `dispatch_screen` / `timed` with a `Binding` of
  inputs (t10..t15) and outputs (u0..u7); the scene (t0..t8) and sky (t9) are bound for every pass. Shared shader code:
  `pbrCommon.hlsli` (bindings, packing, sampling, camera) and `pbrSurface.hlsli` (materials, BRDF, ray queries, lights).
  Settings that change the image call `reset_history()` through `CVarResetHistory`.
- Alpha tested geometry (foliage) dominates ray cost: every candidate hit interrupts traversal. Real time rays stop alpha
  testing after `pbr.light.alpha_tests_shadow` / `alpha_tests_indirect` candidates (the rest count as solid; the path
  tracer tests all), and candidate tests only fetch texture coordinates (`Mesh::uv_density` gives the texture level).
- Backend support: ray tracing (`sg::Raytracing`, acceleration structure builds) and bindless descriptors
  (`Device::allocate_bindless_descriptor`, `BindingDesc::bindless`) are implemented by the D3D12 backend only; the Vulkan
  backend reports `supports_raytracing() == false` and has asserting stubs. The viewer's shaders are listed with
  `"platforms": ["PC_DXC"]` so SPIR-V builds skip them, and its test cases use `"backends": ["D3D12"]`. Adding Vulkan
  support means implementing those (VK_KHR_acceleration_structure + ray query, descriptor heap indexing) and dropping both.

## Shaders

- Shaders are listed in `SlimGraphicsApplication\*.slimshaderjson` (profile, entry point, variants whose ids become name
  suffixes such as `_EDS` or `_GS`). The pre-build step compiles all of them for the generated backend into
  `ShaderBin_<Config>` (`.PC_DXC` or `.PC_DXC_SPIRV`).
- SPIR-V options (`SlimShaderCompiler\Program.cs`): register shifts b→0, t→16, s→32, u→48 (all set 0),
  `-fspv-target-env=vulkan1.3` and `-fvk-use-dx-layout`. They must agree with `BindingShift` / `PushData` in
  `sgVulkanPipeline.h` and `Device::create_descriptor_mapping`.
- A shader (or group) with `"platforms": ["PC_DXC"]` is only compiled for those platforms (`Program.cs`).
- After changing `Program.cs`, rebuild `Sharpmake\csharp_projects\SlimShaderCompiler_vs2022_anycpu.csproj` (Release;
  it copies itself to `Binaries\`) and commit `Binaries\SlimShaderCompiler.exe/.pdb` with the source change.
- HLSL rules that keep both backends working:
  - registers b0–b3 (constant buffers), t0–t15, u0–u15, s0–s15 only;
  - no struct inheritance for stage input/output structs (DXC's SPIR-V output drops writes to inherited members of
    mesh shader outputs);
  - read/write textures whose format matters need `[[vk::image_format("...")]]` under `#ifdef __spirv__`;
  - use structured buffers, not typed buffers (`Buffer<T>` / `RWBuffer<T>` are unsupported in Vulkan);
  - buffer layouts are D3D layouts on both backends: keep shared structs in `ShaderSharedStructures.h`.

## Abstraction rules

- Application code uses only the `sg::` aliases from each backend's `sgPlatformForwardDeclare.h`. Any change to that
  interface is implemented in both backends in the same change, and D3D12 must keep working. The one exception is ray
  tracing and bindless descriptors (see "PBR viewer"): Vulkan has declarations that report no support.
- Resource states mirror D3D12: resources rest in a read state; command lists transition them for UAV binding, render
  targets and copies, then return them. Upload memory is always host coherent and needs no barriers; readback rests in
  `HostRead`.

### Vulkan backend design (keep it this way)

- Vulkan 1.4 with VK_EXT_descriptor_heap, VK_KHR_dynamic_rendering, VK_EXT_shader_object and
  VK_EXT_descriptor_indexing, plus VK_KHR_swapchain and VK_EXT_mesh_shader (when supported). Enable promoted features
  through their individual structs, never `VkPhysicalDeviceVulkan12Features` (VUID-VkDeviceCreateInfo-pNext-04748).
- The renderer has no `VkPipeline`, pipeline layout, descriptor set/pool, push descriptors or robustness2. ImGui's
  backend keeps its own.
- A `Pipeline` is a set of linked shader objects plus fixed function state. All of that state is dynamic and is set in
  `CommandList::set_graphics_state`: new state must be added there, because shader objects require every relevant state
  to be set before drawing. Rasterization must match D3D12's: lines and wireframe use Bresenham line rasterization
  (D3D12's aliased lines), which the parity check's pixel and pipeline statistics comparisons catch.
- Descriptors: CBVs are device addresses in push data; SRV/UAV tables are written into the command list's region of the
  resource heap; sampler `s<n>` is sampler heap slot n. Shaders need no `VkImageView` (descriptors are built from
  `VkImageViewCreateInfo`); views exist only for attachments and ImGui.
- Memory is allocated with VMA, then buffers/images are bound into it (the D3D12 placed resource model).
- After third party code records into our command buffer (ImGui), call `CommandList::invalidate_state()`.

### Error handling

- Vulkan-Hpp runs in no-exception mode. Every `vk::Result` / `VkResult` (Vulkan-Hpp, C API, VMA, ImGui) goes through
  `CHECKVK`, which reports the call and result in every configuration and stops the program, unless the code is an
  expected one handled on the spot (`eSuboptimalKHR`, `eErrorOutOfDateKHR`, `eNotReady`). Ignored results raise
  `[[nodiscard]]` warnings, which fail validation.
- D3D12 uses `CHECKHR`. `seAssert` is for programmer errors (Debug only).

## Code style

- Match the surrounding code: tabs, braces on their own lines, `snake_case` functions/variables/members, `PascalCase`
  types, `sg::D3D12` / `sg::Vulkan` namespaces, `SharedPtr`, const correctness, comments that explain why.
- No C++ exceptions (the code builds without `/EHsc`).
- `sgD3D12Device.cpp`, `Model.h` and `PostProcessComputeShader.hlsl` are not UTF-8. Preserve their encoding: edit them
  with an encoding-aware script, as the Edit tool can mangle them.
- `SlimGraphicsVulkan` builds warning free. Don't add warnings anywhere.

## Submodules

- Don't leave edits in submodules. Save local fixes as `SubModules\Patches\<SubmoduleFolder>-<description>.patch`
  (`git -C SubModules\<Name> diff > ...`). `validate.ps1` checks they are applied (`-ApplySubmodulePatches` applies them).
- `ImGuiDocking-vulkan-swapchain-acquire.patch` fixes two Vulkan validation errors in ImGui's backend: swap chain images
  transitioned before acquire (fixed upstream in 4cb4b17cb) and an unaligned flush when uploading font atlas updates.
  Re-check both when bumping ImGuiDocking, then drop what upstream has fixed.

## Git

- Work on a branch, not `main`. Commit only when asked, and never push unless asked.
