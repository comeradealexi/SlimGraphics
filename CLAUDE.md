# CLAUDE.md

Slim Graphics is a Windows C++20 graphics playground: a thin rendering abstraction (`sg::`) with two backends, D3D12
(`SlimGraphicsD3D12`) and Vulkan 1.4 (`SlimGraphicsVulkan`). A build contains exactly one backend, chosen when the project
files are generated. Both backends must stay functional and render identically.

## Repository map

| Path | Contents |
|---|---|
| `SlimEngine` | Core types (`sgTypes.h`: resource/pipeline/blend/depth descs, `BaseBinding`), logging and asserts (`seEngine.h`), window, input |
| `SlimGraphicsShared` | Backend independent helpers (`UploadHeap`, `sgUtils`) |
| `SlimGraphicsD3D12` | D3D12 backend (D3D12MA) |
| `SlimGraphicsVulkan` | Vulkan backend (Vulkan-Hpp RAII, VMA) |
| `SlimGraphicsApplication` | The app (model viewer, GPU driven rendering, post process, magnifier, texture viewer), HLSL and `*.slimshaderjson` |
| `SlimShaderCompiler` | C# tool that drives DXC. The build runs the copy in `Binaries\` |
| `Sharpmake` | Project generation (`main.sharpmake.cs`; `/vulkan` selects the Vulkan backend) |
| `SubModules` | Third party code. Local fixes live in `SubModules\Patches` (see below) |
| `Tools\validate.ps1` | Build, validation layer and D3D12/Vulkan parity harness |

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
  `-mesh_mode=0|1|2` (vertex / mesh / amplification + mesh), `-gdr_scene=PATH`.
- Environment: `SE_LOG_FILE=path` copies engine output (including validation messages) to a file,
  `SG_VULKAN_VALIDATION_LOG=path` copies Vulkan validation messages, `SG_D3D12_GPU_VALIDATION=1` enables D3D12
  GPU-based validation, and `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` / `VK_KHRONOS_VALIDATION_GPUAV_ENABLE=true` /
  `VK_KHRONOS_VALIDATION_VALIDATE_BEST_PRACTICES=true` turn on the extra Vulkan layer checks.

## Validation: required before calling graphics work done

```powershell
powershell -ExecutionPolicy Bypass -File Tools\validate.ps1            # both backends, core validation, parity
powershell -ExecutionPolicy Bypass -File Tools\validate.ps1 -Extended  # + D3D12 GPU-based, Vulkan sync and GPU-AV passes
```

- The work is done only when the script prints `VALIDATION PASSED` (exit code 0). It requires:
  - clean builds (any warning in `SlimGraphicsVulkan` fails);
  - every scenario exiting with code 0 and writing its captures;
  - every scenario logging that its validation layer loaded, with zero `[Vulkan|D3D12 ERROR|WARNING|CORRUPTION]` or
    `Assert:` lines;
  - D3D12 and Vulkan final renders differing in at most 0.05% of pixels.
- Use `-Extended` whenever backend code (barriers, descriptors, synchronisation, swap chain, memory) or shaders change,
  and `-BuildRelease` before committing backend changes.
- Look at the captures and diff images in the output directory (`%TEMP%\SlimGraphicsValidation` by default) when
  rendering changed. Back buffer diffs that only cover ImGui hover highlights are expected.
- Never make a validation message go away by filtering it, disabling a layer or check, or loosening a threshold: fix the
  cause. The only filtered message is `D3D12_MESSAGE_ID_FENCE_ZERO_WAIT`, which ImGui's DX12 multi-viewport code
  triggers. Only add to that list with evidence that the message comes from outside the engine.
- New features need coverage: add a command line switch and a scenario to `Tools\validate.ps1`. Things the scenarios do
  not cover yet (ImGui toggles such as wireframe or the geometry shader path, magnifier interaction, window resize and
  minimise) must be checked by hand when touched.
- Best practices validation is advisory. Its known, accepted warnings: one memory block per resource (the D3D12 placed
  resource model) and UNDEFINED initial layout transitions.

## Shaders

- Shaders are listed in `SlimGraphicsApplication\*.slimshaderjson` (profile, entry point, variants whose ids become name
  suffixes such as `_EDS` or `_GS`). The pre-build step compiles all of them for the generated backend into
  `ShaderBin_<Config>` (`.PC_DXC` or `.PC_DXC_SPIRV`).
- SPIR-V options (`SlimShaderCompiler\Program.cs`): register shifts b→0, t→16, s→32, u→48 (all set 0),
  `-fspv-target-env=vulkan1.3` and `-fvk-use-dx-layout`. They must agree with `BindingShift` / `PushData` in
  `sgVulkanPipeline.h` and `Device::create_descriptor_mapping`.
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
  interface is implemented in both backends in the same change, and D3D12 must keep working.
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
  to be set before drawing.
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
