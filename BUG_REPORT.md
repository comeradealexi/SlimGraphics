# Slim Graphics – Bug & Functional Issue Report

Scope: everything first-party in the repo — `SlimEngine`, `SlimGraphicsShared`, `SlimGraphicsD3D12`, `SlimGraphicsApplication` (C++ and HLSL). I left out third-party code (`External/`, `SubModules/`, the PIX headers) and the Vulkan backend (see §6). This was a static review. I didn't build or run anything. I checked each item against the code, but runtime impact is my judgement and is marked where it depends on usage.

Severity key:
- **High**: crash, corruption, memory growth, or clearly wrong output in a normal workflow
- **Medium**: wrong behaviour in a reachable but less common path, D3D12 validation errors, or portability problems
- **Low**: edge cases, cosmetic issues, robustness
- **Latent**: the bug is real but the code path isn't currently called

---

## 1. High

### 1.1 GPU memory is never freed, and GDR leaks memory every frame
- [sgD3D12Memory.h](SlimGraphicsD3D12/sgD3D12Memory.h): `Memory` has no destructor. Nothing in the codebase ever calls `D3D12MA::Allocation::Release()`, and the allocator and pools are never released either.
- So when a `Buffer` or `Texture` is destroyed, the placed `ID3D12Resource` goes away but its heap memory stays allocated.
- [gdrScene.cpp:312-353](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:312): `instance_data.create_and_upload_to_gpu()` runs **every frame** and allocates a new GPU buffer each time ([gdrScene.h:50](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.h:50)). With a GDR scene loaded, VRAM grows without limit.
- Other leaks: every model reload, every `TextureViewer` render-target resize, every `create_pipelines()` call (which also calls `create_indirect_arguments()` twice, [gdrScene.cpp:199](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:199)), every `GPUBuffer::create` growth, and the test allocation at [main.cpp:153](SlimGraphicsApplication/main.cpp:153).
- **Fix:** add `~Memory() { if (alloc) alloc->Release(); }`. Separately, cache the instance buffer and only reallocate it when it needs to grow.

### 1.2 Wrong index in `CommandList::bind` UAV de-duplication
- [sgD3D12CommandList.cpp:93](SlimGraphicsD3D12/sgD3D12CommandList.cpp:93): `observed_resources_in[barrier_count_out] = ...` should use `barrier_count_in`.
- When the previous binding had UAVs (so `barrier_count_out > 0`), the "already transitioned" list for incoming resources is written at the wrong slot. That can let the same resource be transitioned twice (an invalid barrier), or overwrite a previous entry.
- It's most likely to trigger when several UAVs reference one resource, e.g. the 6 HZB mip UAVs ([gdrHZB.cpp:63-68](SlimGraphicsApplication/GPUDrivenRendering/gdrHZB.cpp:63)), or a binding switch between two compute passes with no `end_geometry_pass` in between.

### 1.3 ImGui `Begin()`/`End()` mismatches: collapsing these windows triggers ImGui asserts
ImGui requires `End()` after every `Begin()`, whatever `Begin()` returns. These places only call `End()` when `Begin()` returned true:
- [gdrScene.cpp:228-265](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:228) ("GDR Scene")
- [gdrScene.cpp:267-290](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:267) ("GDR Statistics")
- [gdrEditor.cpp:29-32](SlimGraphicsApplication/GPUDrivenRendering/gdrEditor.cpp:29) (`if (!Begin) return;`, "GDR Scene Editor")
- [TextureViewer.cpp:33-63](SlimGraphicsApplication/TextureViewer/TextureViewer.cpp:33) ("Texture Viewer")

Collapsing any of these windows breaks the window stack.

### 1.4 `ImPlot::BeginPlot` has a stray semicolon
- [main.cpp:395](SlimGraphicsApplication/main.cpp:395): `if (ImPlot::BeginPlot(...));` so the block always runs. `PlotLine`/`EndPlot` get called even when `BeginPlot` returned false, which hits ImPlot asserts when "Expand Stats" is ticked and the plot is clipped or collapsed.

### 1.5 Combo item strings are missing the final double-NUL (out-of-bounds read)
- [ModelViewer.cpp:579](SlimGraphicsApplication/ModelViewer.cpp:579) and [gdrScene.cpp:260](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:260): `"...GreaterEqual\0Always"` needs a trailing `\0`.
- ImGui counts items by scanning until it finds two NULs in a row, so it reads past the end of the string literal. The trailing `9` argument doesn't help: that parameter is `popup_max_height_in_items`, not the item count.

### 1.6 Model Viewer depth comparison "None" builds an invalid PSO
- [ModelViewer.cpp:904/934/954](SlimGraphicsApplication/ModelViewer.cpp:904): `(ComparisonFunction)depth_comparison` passes `None` straight through as `D3D12_COMPARISON_FUNC_NONE` for `DepthFunc` with depth enabled. `CreatePipelineState` fails and `CHECKHR` asserts.
- The GDR code already maps 0 to `Never` ([gdrScene.cpp:215](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:215)); ModelViewer needs the same mapping.

### 1.7 Model Viewer readback copy uses the wrong barrier state when nothing was drawn
- [ModelViewer.cpp:875](SlimGraphicsApplication/ModelViewer.cpp:875) → [sgD3D12CommandList.cpp:430-434](SlimGraphicsD3D12/sgD3D12CommandList.cpp:430): the readback branch of `copy_buffer_to_buffer` assumes the source is in `UNORDERED_ACCESS`.
- That's only true if `uav_buffer` is currently bound. If the render list is empty ("Models To Render" = 0, "Cull All", every part culled, or a model that failed to load), `bind()` never runs. `clear_buffer_uint` leaves the buffer in its read state, so the barrier's before-state is wrong (a debug-layer error).
- More generally, this copy overload silently depends on bind state. It should look up the source's real state instead.

### 1.8 Texture uploads from PNG/JPG/TGA/BMP are garbled unless the width is a multiple of 128
- [sgD3D12CommandList.cpp:521](SlimGraphicsD3D12/sgD3D12CommandList.cpp:521) reads source rows with `RowPitch = AlignUp(footprint.RowPitch, 512)`.
- The stb/lodepng paths ([Model.cpp:529-531](SlimGraphicsApplication/Model.cpp:529), [Model.cpp:551-553](SlimGraphicsApplication/Model.cpp:551), [Terrain.cpp:61-64](SlimGraphicsApplication/Scene/Terrain.cpp:61)) write tightly packed rows (`w*4` bytes).
- Any texture whose width isn't a multiple of 128 comes out sheared. Small textures (e.g. 64×64) are badly corrupted, and the copy can read past the bytes that were written. Only the DDS path pads rows to match ([sgD3D12Device.cpp:846-863](SlimGraphicsD3D12/sgD3D12Device.cpp:846)).
- **Fix:** write rows at the footprint's pitch, and use the 256-byte `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` rather than 512.

### 1.9 Meshes that produce zero meshlets crash
- [gdrScene.cpp:582-586](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:582) and [Model.cpp:348-352](SlimGraphicsApplication/Model.cpp:348): `meshlets[meshlet_count - 1]` underflows when `meshlet_count == 0`, and `&vertices[0]` is undefined behaviour on an empty vector.
- This happens with point/line-only meshes (non-triangle faces are dropped), and in Model Viewer when "Simplify" with threshold 0 or a large error reduces a part to zero indices.
- Point/line meshes also have `mNormals == nullptr`, which is dereferenced unconditionally ([gdrScene.cpp:496](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:496), [Model.cpp:149](SlimGraphicsApplication/Model.cpp:149)).
- On the GPU side, a part with 0 meshlets would also divide by zero in `per_instance_processing_main` ([gdrShaderCompute.hlsl:183](SlimGraphicsApplication/GPUDrivenRendering/gdrShaderCompute.hlsl:183)).

### 1.10 GDR: a bad model path reloads every frame, then crashes
- [gdrScene.cpp:536-538](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:536): when Assimp fails, no `ModelData` is added. `model_instance_map.size() != models.size()` then stays true forever, so every frame clears and reloads all models ([gdrScene.cpp:701](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:701)).
- `find_model` then asserts and returns `models[0]` ([gdrScene.cpp:805-806](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:805)), which is undefined behaviour when `models` is empty.

### 1.11 GDR editor "Duplicate" leaves dangling parent pointers
- [gdrEditor.cpp:258](SlimGraphicsApplication/GPUDrivenRendering/gdrEditor.cpp:258): `children.push_back(*it)` deep-copies the subtree, but the copied children's `parent` pointers still point into the **original** subtree.
- Moving the duplicate doesn't move its children, and deleting the original leaves dangling pointers that `process_scene_graph` walks every frame (use-after-free).
- **Fix:** re-run the parent fix-up (`scene_node_traverse` from gdrJSON.cpp) on the new node.

---

## 2. Medium

### 2.1 GDR: `DispatchMesh` group count isn't clamped
- [gdrShaderCompute.hlsl:233](SlimGraphicsApplication/GPUDrivenRendering/gdrShaderCompute.hlsl:233): `IndirectArgumentBuffer[0].x = AtomicCounter[0].x`, one mesh group per visible meshlet.
- D3D12 caps `ThreadGroupCountX` at 65,535. Large instanced scenes exceed that, giving an invalid dispatch (dropped work or device removal).
- **Fix:** spread the count over Y/Z, or loop.

### 2.2 GDR descending sort uses the wrong sentinel
- [gdrShaderSort.hlsl:11-12](SlimGraphicsApplication/GPUDrivenRendering/gdrShaderSort.hlsl:11): the descending sort uses `FLT_MIN` (1.17e-38, the smallest *positive* float) as the "sort to end" key.
- Real keys can be exactly `0.0` (`max(0, distance_to_edge)`, e.g. camera inside a meshlet's sphere). Those sort *after* the sentinels, so sentinel entries (`instance_index = 0xFFFFFFFF`) end up inside the first `count` entries that the mesh shader reads. Visible meshlets get dropped and out-of-range indices get read.
- **Fix:** use `-FLT_MAX`.

### 2.3 GDR bounding-sphere radius is wrong under rotation
- [gdrShaderCompute.hlsl:131/146](SlimGraphicsApplication/GPUDrivenRendering/gdrShaderCompute.hlsl:146) (also [ModelViewer_Header.hlsli:184](SlimGraphicsApplication/ModelViewer_Header.hlsli:184)): the radius scale is `max(m[0][0], m[1][1], m[2][2])`.
- With rotation those diagonal terms shrink or go negative, so meshlets are wrongly frustum-culled (popping). The GDR editor allows arbitrary rotations; the Model Viewer only uses yaw, which keeps `m[1][1]` intact, so it's mostly unaffected.
- **Fix:** use the maximum length of the three basis rows.

### 2.4 GDR meshlet AABB/OBB only covers about a third of the triangles
- [gdrScene.cpp:647-651](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:647): the loop runs `i < triangle_count` and reads `tmp_meshlet_triangles[triangle_offset + i]`. That's only the first `triangle_count` bytes of a `3 × triangle_count` index list.
- Not used by the current culling, but any OBB/AABB or HZB test built on it will cull visible meshlets.
- **Fix:** loop over `vertex_count` instead.

### 2.5 HZB build (work in progress) isn't conservative
- [gdrHZB.hlsl:45-135](SlimGraphicsApplication/GPUDrivenRendering/gdrHZB.hlsl:45): the downsample uses `min` (despite the variable name `max_depth`). With standard depth (clear 1.0, `LessEqual`), occlusion culling needs the *farthest* depth, i.e. `max`. Using `min` will cull visible objects.
- Threads past the texture edge (1080 isn't a multiple of 32) `Load` 0, which then dominates `min`, so edge tiles in coarser mips become 0.
- Odd mip sizes drop the last row/column.
- Only mips 0–5 are written; mips 6+ stay uninitialised ([gdrHZB.cpp:55-60](SlimGraphicsApplication/GPUDrivenRendering/gdrHZB.cpp:55)), which the Texture Viewer "Top Mip" slider shows as garbage.

### 2.6 Vertex buffer is read through an SRV while in the vertex-buffer state
- [Model.cpp:585-587](SlimGraphicsApplication/Model.cpp:585): `vb` is a `BufferType::Vertex` resource, kept in `VERTEX_AND_CONSTANT_BUFFER`. The mesh-shader path reads it through `vb_srv` ([ModelViewer.cpp:721](SlimGraphicsApplication/ModelViewer.cpp:721)), which needs `NON_PIXEL_SHADER_RESOURCE`.
- GPU-based validation flags it, and it isn't guaranteed coherent. Combine the states, or use `GeneralDataBuffer`.

### 2.7 Amplification shader assumes a wave size of at least 32
- [ModelViewer_Header.hlsli:212-261](SlimGraphicsApplication/ModelViewer_Header.hlsli:212): the payload is compacted with `WavePrefixCountBits`/`WaveActiveCountBits` across a 32-thread group.
- On wave8/16 hardware (some Intel GPUs) the group spans several waves: payload indices collide and the per-wave `visibleCount` passed to `DispatchMesh` is wrong (`DispatchMesh` must be group-uniform).
- **Fix:** use groupshared atomics, or check `WaveGetLaneCount()`.

### 2.8 Model Viewer CPU frustum culling ignores part position
- [ModelViewer.cpp:683](SlimGraphicsApplication/ModelViewer.cpp:683), [ModelViewer.cpp:986-989](SlimGraphicsApplication/ModelViewer.cpp:986): every part is tested as a sphere centred at the **origin**, with radius = the largest absolute *component* of `max_extent`.
- That ignores the part's centre and rotation, and underestimates the radius by up to √3. Parts near the bounding-box corners or away from the origin get culled while still visible.
- **Fix:** use `sphere_centre`/`sphere_radius` transformed by `model_matrix`.

### 2.9 DebugDraw has no capacity check
- [DebugDraw.cpp:252-257](SlimGraphicsApplication/DebugDraw.cpp:252): vertices and indices are `memcpy`'d into fixed-size upload buffers (`MAX_TRIANGLES = 100000`) with no bounds check.
- "Meshlet Parts" draws a ~190-vertex sphere per meshlet, so a model with about 1.6k+ meshlets writes past the mapped upload resource (heap corruption). The GPU-side copy only asserts in debug builds.

### 2.10 Upload-heap fence starts at 0 (currently masked)
- [sgUploadHeap.h:24](SlimGraphicsShared/sgUploadHeap.h:24) / [sgUploadHeap.cpp:101](SlimGraphicsShared/sgUploadHeap.cpp:101): `counter` starts at 0, so slot 0's first `Signal` is value 0. Fences start at 0, so the later `fence_wait_cpu(0)` returns immediately even if the GPU hasn't finished, and the allocator and upload memory get reused while in flight.
- It's harmless today only because `main.cpp` waits for the GPU every frame ([main.cpp:612-614](SlimGraphicsApplication/main.cpp:612) even notes this for its own fence). It will break as soon as frames overlap.

### 2.11 Debug builds assert at startup without Windows Developer Mode
- [sgD3D12Device.cpp:206-207](SlimGraphicsD3D12/sgD3D12Device.cpp:206): `SetStablePowerState(true)` fails unless Developer Mode is enabled, and `CHECKHR` asserts. The log line after it suggests failure was meant to be tolerated.

### 2.12 Shader-visible sampler heap is larger than the classic limit
- [sgD3D12Device.cpp:1318-1320](SlimGraphicsD3D12/sgD3D12Device.cpp:1318): `DESCRIPTOR_COUNT = 4080` is used for the shader-visible sampler heap. The traditional limit is 2048 (`D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE`); larger sizes need newer drivers (Options19). Heap creation fails on older GPUs/drivers.
- The comment "4080 // D3D12 Limit" is also inaccurate for CBV/SRV/UAV heaps.

### 2.13 Per-command-list descriptor budget can run out
- [sgD3D12CommandList.cpp:117](SlimGraphicsD3D12/sgD3D12CommandList.cpp:117): each `bind()` takes new descriptors from a 4080-entry linear heap that is only reset per frame.
- GDR binds 13 descriptors per mesh part per frame ([gdrScene.cpp:441-454](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:441)), the sort binds once per pass, and Model Viewer binds once per part. Scenes with a few hundred parts will hit the assert (and silently overrun in release, where asserts are compiled out).

### 2.14 GameInput callback races with `update()`
- [seGameInput.cpp:10-31](SlimEngine/Win32/seGameInput.cpp:10): connect/disconnect callbacks arrive on a GameInput thread and modify `keyboards` (a `std::set`) while `update()` iterates it on the main thread, which is a data race.
- The callback token is never unregistered, and the devices and `IGameInput` are never released.

### 2.15 Camera breaks when looking straight up or down
- [Camera.cpp:15-25](SlimGraphicsApplication/Camera.cpp:15): pitch (`camera_rot_x`) isn't clamped. At 0 or π the look direction is parallel to `Up`, so `XMMatrixLookAtLH` produces NaNs (or asserts in debug DirectXMath). Easy to reach by holding I or K.

---

## 3. Low

- **GDR JSON load:** [gdrJSON.cpp:29](SlimGraphicsApplication/GPUDrivenRendering/gdrJSON.cpp:29): `nlohmann::json::parse` throws on malformed JSON and nothing catches it, so the app crashes.
- **GDR sort with fewer than 32 elements:** [gdrSort.cpp:61](SlimGraphicsApplication/GPUDrivenRendering/gdrSort.cpp:61): `maximum_elements / 32` is 0, so nothing is sorted (no corruption). Also, `AllocateAndWrite` is called twice per pass ([gdrSort.cpp:74-75](SlimGraphicsApplication/GPUDrivenRendering/gdrSort.cpp:74)).
- **GDR stats:** [gdrScene.cpp:282](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:282): divides by `total_meshlets`, so it shows NaN% when that is 0. The debug-render-mode combo is mislabelled "Depth Comparison" ([gdrScene.cpp:240](SlimGraphicsApplication/GPUDrivenRendering/gdrScene.cpp:240)).
- **Duplicate cbuffer name:** two cbuffers are both named `DebugConstantBufferData` (b2 and b3) in [gdrShaderCompute.hlsl:67-75](SlimGraphicsApplication/GPUDrivenRendering/gdrShaderCompute.hlsl:67).
- **Sampler border colour:** [sgTypes.h:289](SlimEngine/sgTypes.h:289): `SamplerDesc::border_color` is uninitialised, so the `Border` address mode gets a garbage colour.
- **`write_file`:** [seEngineBasicFileIO.cpp:25-34](SlimEngine/seEngineBasicFileIO.cpp:25): the success path has no `return true` (undefined behaviour), and the file is opened in text mode, so binary data gets CRLF-translated.
- **`find_files_recursive`:** [seEngineBasicFileIO.cpp:53](SlimEngine/seEngineBasicFileIO.cpp:53): uses `continue` where `break` was meant, so a file matching several extensions is added more than once.
- **Window size:** [seWindow.cpp:168-170](SlimEngine/Win32/seWindow.cpp:168): `AdjustWindowRect` uses `WS_OVERLAPPEDWINDOW` but the window is created with a different style, so the client area isn't exactly 1920×1080. (The swap chain uses the real client size, so this is cosmetic.)
- **Shutdown:** [seWindow.cpp:44-46](SlimEngine/Win32/seWindow.cpp:44): `exit(0)` in `WM_DESTROY` skips all cleanup and the GPU idle wait. It's already marked as a hack.
- **Linear constant buffer:** [LinearConstantBuffer.cpp:31-34](SlimGraphicsApplication/LinearConstantBuffer.cpp:31): there's an off-by-one (`>=`), and on overflow it returns an empty CBV but `AllocateAndWrite` still writes past the region in release builds.
- **`projectBox`:** [ModelViewer.cpp:130-136](SlimGraphicsApplication/ModelViewer.cpp:130): uses `std::max` where `min` was meant, so the near-plane rejection check is wrong. This only affects the debug info panel.
- **Model Viewer with no assets:** [ModelViewer.cpp:78](SlimGraphicsApplication/ModelViewer.cpp:78): `model_file_list[0]` crashes if no models are found.
- **Texture Viewer target size:** [TextureViewer.cpp:37-38](SlimGraphicsApplication/TextureViewer/TextureViewer.cpp:37): a manual target size of 0 or a negative value makes texture creation fail.
- **Post-process normalise:** [PostProcessComputeShader.hlsl:165-169](SlimGraphicsApplication/PostProcessComputeShader.hlsl:165): divides by zero when Colour Min equals Colour Max. Also, `#ifdef DXC_STATIC_DISPATCH_GRID_DIM` is always true because the macro is defined as 0; `#if` was presumably intended.
- **Camera direction:** [Camera.cpp:42](SlimGraphicsApplication/Camera.cpp:42): `camera_direction` is `Eye - At`, which points backwards. Unused by shaders today.
- **Missing mipmaps:** loaded PNG/JPG textures get none, so the default anisotropic sampler has nothing to filter.
- **Unsupported texture formats:** `.tif` is routed to stb, which can't decode TIFF, and embedded Assimp textures (`*0`) aren't supported. Both silently fall back to the default white texture.
- **`Buffer::read_memory`:** [sgD3D12Buffer.cpp:54](SlimGraphicsD3D12/sgD3D12Buffer.cpp:54): the `Map` read range ignores `offset`.
- **Query pools:** [sgD3D12GPUTimestampPool.cpp:82](SlimGraphicsD3D12/sgD3D12GPUTimestampPool.cpp:82) / [sgD3D12GPUStatisticPool.cpp:75](SlimGraphicsD3D12/sgD3D12GPUStatisticPool.cpp:75): no bounds check against `max_count`, so overflow writes past `*_copy` in `resolve()`.
- **Adapter selection:** [sgD3D12Device.cpp:181-188](SlimGraphicsD3D12/sgD3D12Device.cpp:181): there's no null check if no hardware adapter is found, and the `QueryInterface` result isn't checked.
- **Barrier edge case:** in `bind()` ([sgD3D12CommandList.cpp:74-98](SlimGraphicsD3D12/sgD3D12CommandList.cpp:74)), transitions are compared slot by slot. If a resource stays bound as a UAV in one slot and also newly appears in another, it gets a read→UAV transition while already in UAV state.
- **Terrain retry loop:** [Terrain.cpp:33-36](SlimGraphicsApplication/Scene/Terrain.cpp:33): if the heightmap fails to load, it retries (and asserts) every frame.
- **Path display:** `StringPathToFilename` ([gdrEditor.cpp:9](SlimGraphicsApplication/GPUDrivenRendering/gdrEditor.cpp:9), [ModelViewer.cpp:14](SlimGraphicsApplication/ModelViewer.cpp:14)) only looks for `/`, but `std::filesystem` returns `\` paths, so combos show full paths. The `strcpy` into a 256-byte buffer ([gdrEditor.cpp:46](SlimGraphicsApplication/GPUDrivenRendering/gdrEditor.cpp:46)) can overflow.
- **Resource and performance notes (not bugs):**
  - The upload heap reserves 3 × 256 MB of upload memory ([main.cpp:283](SlimGraphicsApplication/main.cpp:283)).
  - The main loop does a full CPU↔GPU sync every frame, and the `fence_wait_gpu` right after the `Signal` does nothing ([main.cpp:612-614](SlimGraphicsApplication/main.cpp:612)).
  - The amplification shader marks out-of-range threads as visible ([ModelViewer_Header.hlsli:233](SlimGraphicsApplication/ModelViewer_Header.hlsli:233)); the mesh shader rejects them, but MS groups are wasted.

---

## 4. Latent (bug exists, code path currently unused)

- **Wrong heap in `free_depth_stencil_view`:** [sgD3D12Device.cpp:955](SlimGraphicsD3D12/sgD3D12Device.cpp:955) frees the DSV index into the **RTV** heap. Also, `free_render_target_view`/`free_depth_stencil_view` would double-free alongside the view destructors, which already free.
- **Blend op translation falls through:** [sgD3D12TypesTranslator.h:321-324](SlimGraphicsD3D12/sgD3D12TypesTranslator.h:321): `Subtract`/`ReverseSubtract` are missing `return`, so they fall through and return `D3D12_BLEND_OP_MIN`.
- **`clear_buffer_float`:** [sgD3D12CommandList.cpp:366](SlimGraphicsD3D12/sgD3D12CommandList.cpp:366): `ClearUnorderedAccessViewFloat` on a raw `R32_TYPELESS` view is invalid (float clears only work on float/UNORM/SNORM views).
- **Buffer SRV `desc_uint`:** [sgD3D12Device.cpp:994](SlimGraphicsD3D12/sgD3D12Device.cpp:994): `NumElements = element_count / 4` ignores the element size (the UAV version gets it right), and `FirstElement`/`StructureByteStride` aren't set.
- **`copy_buffer_to_buffer(dest, src)`:** [sgD3D12CommandList.cpp:416](SlimGraphicsD3D12/sgD3D12CommandList.cpp:416): asserts `dest >= src`, but `CopyResource` requires identical sizes. The assert also dereferences before its own null check ([sgD3D12CommandList.cpp:417](SlimGraphicsD3D12/sgD3D12CommandList.cpp:417)).
- **Unfinished RTV/DSV format override:** [sgD3D12Device.cpp:914-919/941-946](SlimGraphicsD3D12/sgD3D12Device.cpp:914): the explicit-format path asserts and leaves the view description incomplete.
- **ImGui descriptor free:** [sgD3D12Device.cpp:276-280](SlimGraphicsD3D12/sgD3D12Device.cpp:276): `SrvDescriptorFreeFn` asserts. It would fire on `ImGui_ImplDX12_Shutdown`, which is never called today.
- **Non-2D texture SRVs:** [sgD3D12Device.cpp:1016-1019](SlimGraphicsD3D12/sgD3D12Device.cpp:1016): SRVs always fill the `Texture2D` union, which is wrong for arrays/cubes. DDS-loaded cube maps and 3D textures keep `dimension = Texture2D` ([sgD3D12Device.cpp:818-822](SlimGraphicsD3D12/sgD3D12Device.cpp:818)), and the DDS row-padding loop only handles one depth slice.

---

## 5. Suggested fix order
1. §1.1 (memory release and the per-frame GDR leak). It's the only one that degrades a long session on its own.
2. §1.2, §1.7 (barrier correctness). Enable GPU-based validation once to catch the rest of this class.
3. §1.3–§1.6 (ImGui/PSO crashes). Each is a one-line fix.
4. §1.8 (texture row pitch). It affects any non-128-aligned texture.
5. §2.1–§2.4 before taking GDR to larger scenes; §2.5 before relying on HZB occlusion.

## 6. Note on the Vulkan backend
`SlimGraphicsVulkan` is still largely a copy of the D3D12 backend: `sgVulkanCommandList.cpp` and `sgVulkanDevice.cpp` contain 75 and 236 D3D12/`ID3D12` references respectively, and the headers have drifted (e.g. the new `create_shader_resource_view(texture, top_mip, mip_count)` signature isn't mirrored). Building with `/vulkan` will fail, so I didn't review it for logic bugs.
