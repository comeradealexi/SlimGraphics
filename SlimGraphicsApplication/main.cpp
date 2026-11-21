#include <sgPlatformInclude.h>
#include <seEngineBasicFileIO.h>
#include <Win32/seWindow.h>
#include <Win32/seGameInput.h>
#include <imgui.h>
#include <implot.h>

#include <imgui_impl_win32.h>
// Manually externed as per imgui comment in imgui_impl_win32.h
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <DirectXMath.h>
#include <lodepng.h>
#include <shellapi.h>
#include "AverageTimer.h"
#include "BasicGrid.h"
#include <sgUtils.h>
#include "Utils.h"

//Application headers
#include "Model.h"
#include "sgUploadHeap.h"
#include "LinearConstantBuffer.h"
#include "Camera.h"
#include "CameraTrackPlayer.h"
#include "TestRunner.h"
#include "ModelViewer.h"
#include "DebugDraw.h"
#include "MagnifyingGlass/MagnifyingGlass.h"
#include "TextureViewer/TextureViewer.h"	
#include "PostProcess/PostProcess.h"
#include "BitonicSort.h"

#include "GPUDrivenRendering/gdrScene.h"
#include "PBRViewer/pbrRenderer.h"
#include "CVars.h"
#include "ImGuiLayout.h"
#include <filesystem>
#include <ctime>

void OverrideImguiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// Corners
	style.WindowRounding = 8.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 6.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding = 6.0f;
	style.TabRounding = 6.0f;

	// Colors
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.43f, 0.50f, 0.56f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.50f, 0.56f, 0.50f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.56f, 1.00f, 0.80f);
	colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.22f, 0.36f, 1.00f);
	//colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
	//colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

using namespace sg;

// ---------------------------------------------------------------------------------------------------------------------
// Interface windows that explain the tool (Help menu)

namespace help_windows
{
	constexpr const char* kBackend =
#ifdef SG_PLATFORM_D3D12
		"D3D12";
#else
		"Vulkan";
#endif

	void key_line(const char* keys, const char* action)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%-22s", keys);
		ImGui::SameLine(170.0f);
		ImGui::TextUnformatted(action);
	}

	void controls_window(bool* open)
	{
		centre_window_first_use(560.0f, 0.0f);
		if (ImGui::Begin("Controls", open))
		{
			ImGui::SeparatorText("Camera");
			key_line("Right mouse (drag)", "Look around");
			key_line("W A S D / arrows", "Move forward, left, back, right");
			key_line("Q / E", "Move up / down");
			key_line("Shift / Ctrl", "Move 5x faster / slower");
			key_line("Mouse wheel", "Change the movement speed (over the scene)");
			key_line("I J K L", "Turn with the keyboard");
			key_line("Space", "Play / pause the scene's camera track");
			ImGui::SeparatorText("Interface");
			key_line("Tab", "Hide / show the interface (clean screenshots)");
			key_line("F12", "Save a screenshot (Capture menu)");
			key_line("F1", "This window");
			key_line("Hover", "Most settings explain themselves in a tooltip");
			key_line("Ctrl + click a slider", "Type an exact value");
			key_line("Right click a setting", "Reset it (PBR Settings window)");
			key_line("Drag a window's tab", "Dock it next to or inside another window");
			ImGui::SeparatorText("Scenes (Scenes menu)");
			ImGui::TextWrapped("Model Viewer: one model drawn with the vertex pipeline, mesh shaders or amplification + mesh shaders; "
				"its debug views show how the GPU shades vertices, primitives and pixels (wave order, helper lanes, triangle size).");
			ImGui::TextWrapped("GPU Driven Rendering: thousands of instances culled per meshlet on the GPU and drawn with one indirect "
				"dispatch per render bucket, with procedural terrain and textured materials.");
			ImGui::TextWrapped("PBR Viewer: physically based materials with ray traced shadows, global illumination and reflections, "
				"denoised in real time, next to a reference path tracer (needs ray tracing; D3D12).");
		}
		ImGui::End();
	}

	void about_window(bool* open, sg::Device& device, bool raytracing)
	{
		centre_window_first_use(0.0f, 0.0f);
		if (ImGui::Begin("About Slim Graphics", open, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Slim Graphics: a playground for rendering techniques on a thin layer over D3D12 and Vulkan.");
			ImGui::Separator();
			ImGui::Text("Backend: %s", kBackend);
#ifdef _DEBUG
			ImGui::TextUnformatted("Build: Debug (validation layers and asserts on; GPU timings are not representative)");
#else
			ImGui::TextUnformatted("Build: Release");
#endif
			ImGui::Text("Mesh shaders: %s", device.SupportsMeshShaders() ? "yes" : "no");
			ImGui::Text("Hardware ray tracing (PBR viewer): %s", raytracing ? "yes" : "no");
			ImGui::TextDisabled("The adapter, memory and feature details are in the Slim Graphics window's Device header.");
		}
		ImGui::End();
	}

	// GPU pipeline statistics with what each counter means
	void pipeline_statistics(const sg::PipelineStatistics& q)
	{
		const struct { const char* name; uint64_t value; const char* meaning; } rows[] =
		{
			{ "Input assembler vertices", q.ia_vertices, "Vertices read by the input assembler (vertex pipeline only; mesh shaders read their own data)" },
			{ "Input assembler primitives", q.ia_primitives, "Triangles assembled from the index buffer" },
			{ "Vertex shader invocations", q.vs_invocations, "Vertex shader runs; below the vertex count when the post transform cache reuses results" },
			{ "Geometry shader invocations", q.gs_invocations, "Geometry shader runs (one per input primitive)" },
			{ "Geometry shader primitives", q.gs_primitives, "Primitives the geometry shader emitted" },
			{ "Clipper invocations", q.clipping_invocations, "Primitives sent to the clipper" },
			{ "Clipper primitives", q.clipping_primitives, "Primitives left after clipping and culling, sent to the rasteriser" },
			{ "Pixel shader invocations", q.ps_invocations, "Pixel shader runs: covered pixels (with 2x2 quad helpers), minus those rejected by early depth testing" },
			{ "Hull shader invocations", q.hs_invocations, "Tessellation control (hull) shader runs" },
			{ "Domain shader invocations", q.ds_invocations, "Tessellation evaluation (domain) shader runs" },
			{ "Compute shader invocations", q.cs_invocations, "Compute threads launched" },
			{ "Amplification shader invocations", q.as_invocations, "Amplification (task) shader threads: usually one per meshlet tested for culling" },
			{ "Mesh shader invocations", q.ms_invocations, "Mesh shader threads launched for the meshlets that survived" },
		};
		if (ImGui::BeginTable("PipelineStatistics", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			for (const auto& row : rows)
			{
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(row.name);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", row.meaning);
				}
				ImGui::TableNextColumn();
				// Thousands separated
				char digits[32];
				sprintf_s(digits, "%llu", static_cast<unsigned long long>(row.value));
				std::string text;
				for (size_t i = 0, n = strlen(digits); i < n; i++)
				{
					text += digits[i];
					if ((n - i - 1) % 3 == 0 && i + 1 < n) text += ',';
				}
				ImGui::TextUnformatted(text.c_str());
			}
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Hover a counter for what it measures. Counted over the Model Viewer and GDR passes.");
	}
}

// Optional command line switches, mainly to make automated captures for comparing rendering backends:
//   -capture_frame=N      Save the final render target and back buffer (after ImGui) as PNGs at frame N
//   -capture_dir=PATH     Directory to write captures to (default: working directory)
//   -exit_after_capture   Quit once the capture has been written
//   -gdr_scene=PATH       Load a GPU driven rendering scene file on startup
//   -model=TEXT           Model viewer: load the first model whose path contains TEXT
//   -mesh_mode=N          Model viewer: 0 = vertex shader, 1 = mesh shader, 2 = amplification + mesh shader
//   -camera_track         Play the GDR scene's camera track (deterministic: a fixed time step per frame)
//   -camera_track_time=T  Start the camera track at T seconds
//   -test_plan=PATH       Run an automated test plan in this session, then quit (see TestRunner.h)
//   -test_output=PATH     Directory the test plan's captures are written to (default: working directory)
//   -pbr_scene=NAME       Open the PBR viewer with a scene preset (pbr::scene_presets, e.g. "Sponza") or a model file
//   -cvar=NAME=VALUE      Set a cvar (repeatable), e.g. -cvar=pbr.mode=1
struct CommandLineOptions
{
	std::string test_plan;
	std::string test_output = ".";
	int capture_frame = -1;
	std::string capture_dir = ".";
	bool exit_after_capture = false;
	std::string gdr_scene;
	std::string model;
	int mesh_mode = -1;
	bool camera_track = false;
	float camera_track_time = 0.0f;
	std::string pbr_scene;
	std::vector<std::string> cvars;
};

static CommandLineOptions ParseCommandLine()
{
	CommandLineOptions options;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; argv && i < argc; i++)
	{
		std::wstring wide_arg(argv[i]);
		const std::string arg(wide_arg.begin(), wide_arg.end()); // Switches are ASCII
		// Matches "<name><value>" switches, pointing 'v' at the value
		const char* v = nullptr;
		auto match = [&arg, &v](const char* name)
			{
				const size_t len = strlen(name);
				v = arg.compare(0, len, name) == 0 ? arg.c_str() + len : nullptr;
				return v != nullptr;
			};
		if (match("-capture_frame=")) options.capture_frame = atoi(v);
		else if (match("-capture_dir=")) options.capture_dir = v;
		else if (arg == "-exit_after_capture") options.exit_after_capture = true;
		else if (match("-gdr_scene=")) options.gdr_scene = v;
		else if (match("-model=")) options.model = v;
		else if (match("-mesh_mode=")) options.mesh_mode = atoi(v);
		else if (match("-camera_track_time=")) { options.camera_track = true; options.camera_track_time = static_cast<float>(atof(v)); }
		else if (arg == "-camera_track") options.camera_track = true;
		else if (match("-test_plan=")) options.test_plan = v;
		else if (match("-test_output=")) options.test_output = v;
		else if (match("-pbr_scene=")) options.pbr_scene = v;
		else if (match("-cvar=")) options.cvars.push_back(v);
	}
	LocalFree(argv);
	return options;
}

// Writes a readback buffer holding a copied texture (rows 'row_pitch' bytes apart) to a PNG
static void SaveReadbackToPNG(Buffer& readback, u32 row_pitch, u32 width, u32 height, DXGI_FORMAT format, const std::string& path)
{
	std::vector<uint8_t> pixels((size_t)width * height * 4);
	const uint8_t* mapped = static_cast<const uint8_t*>(readback.map_memory());
	const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	for (u32 y = 0; y < height; y++)
	{
		const uint8_t* src_row = mapped + (size_t)y * row_pitch;
		uint8_t* dst_row = pixels.data() + (size_t)y * width * 4;
		for (u32 x = 0; x < width; x++)
		{
			dst_row[x * 4 + 0] = src_row[x * 4 + (bgra ? 2 : 0)];
			dst_row[x * 4 + 1] = src_row[x * 4 + 1];
			dst_row[x * 4 + 2] = src_row[x * 4 + (bgra ? 0 : 2)];
			dst_row[x * 4 + 3] = 255; // Ignore alpha so captures are easy to compare/view
		}
	}
	readback.unmap_memory();
	const unsigned error = lodepng::encode(path, pixels, width, height);
	seAssert(error == 0, "Failed to write capture %s", path.c_str());
	(void)error;
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
#if false
	while (!IsDebuggerPresent())
	{
		Sleep(0);
	}
#endif
	const DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	const u32 frame_count = 3;
	u64 w = 1920;
	u64 h = 1080;
	u64 x_off = 0;
	u64 y_off = 0;
	const CommandLineOptions command_line = ParseCommandLine();
	se::Ptr<se::Window> wnd(new se::Window(hInstance, nCmdShow, w,h));
	wnd->window_callbacks.push_back(ImGui_ImplWin32_WndProcHandler);

	{
		RECT cr;
		GetClientRect(wnd->g_hWnd, &cr);
		w = cr.right - cr.left;
		h = cr.bottom - cr.top;

		POINT xy;
		xy.x = cr.left;
		xy.y = cr.top;
		ClientToScreen(wnd->g_hWnd, &xy);
		x_off = xy.x;
		y_off = xy.y;
	}
	se::Ptr<se::GameInput> input(new se::GameInput());

	SharedPtr<Device> device(new Device());
	SharedPtr<CommandQueue> queue = device->create_command_queue();
	SharedPtr<CommandList> command_buffer = device->create_command_buffer();
	auto fence = device->create_queue_fence();

	{
		sg::SamplerDesc default_sampler;
		device->AddSamplers("Diffuse", default_sampler);
		device->AddSamplers("Specular", default_sampler);
		device->AddSamplers("Normals", default_sampler);
		// s3: trilinear without anisotropy (GDR alpha testing: anisotropic filtering of an explicit mip level is implementation defined)
		sg::SamplerDesc trilinear_sampler;
		trilinear_sampler.filter = sg::Filter::MinMagMipLinear;
		device->AddSamplers("Trilinear", trilinear_sampler);
		// s4, s5: clamped linear and point sampling (PBR viewer: sky, history and HZB reads)
		sg::SamplerDesc linear_clamp;
		linear_clamp.filter = sg::Filter::MinMagMipLinear;
		linear_clamp.address_u = linear_clamp.address_v = linear_clamp.address_w = sg::TextureAddressMode::Clamp;
		device->AddSamplers("LinearClamp", linear_clamp);
		sg::SamplerDesc point_clamp = linear_clamp;
		point_clamp.filter = sg::Filter::MinMagMipPoint;
		device->AddSamplers("PointClamp", point_clamp);
	}

	SharedPtr<Memory> mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	mem = nullptr;

	//ImGui
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGui::StyleColorsDark();
#ifdef SE_IMGUI_DOCKING
		ImGui::GetIO().ConfigFlags |= (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable);
#endif
		ImGui_ImplWin32_Init(wnd->g_hWnd);

		ShowWindow(wnd->g_hWnd, SW_SHOWNORMAL);
		OverrideImguiStyle();
	}
	
	{//Res size test
		ResourceCreateDesc rd;
		rd.width = 128;
		rd.height = 128;
		rd.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.usage_flags = ResourceUsageFlags::UnorderedAccess;
		SizeAndAlignment sal = device->calculate_resource_size_alignment(rd);
	}

	//Load Shaders
	SharedPtr<VertexShader> vs = sg::create_vertex_shader(*device, get_shader_path("Basic_VertexShader"));
	SharedPtr<PixelShader> ps = sg::create_pixel_shader(*device, get_shader_path("Basic_PixelShader"));
	SharedPtr<PixelShader> ps_cb = sg::create_pixel_shader(*device, get_shader_path("Basic_ConstantBuffer_PixelShader"));
	SharedPtr<PixelShader> ps_b = sg::create_pixel_shader(*device, get_shader_path("Basic_Buffer_PixelShader"));
	SharedPtr<ComputeShader> cs = sg::create_compute_shader(*device, get_shader_path("Basic_ComputeShader"));

	SharedPtr<RenderTargetView> backbuffer_rtvs[frame_count];
	u32 current_frame_idx = device->create_swap_chain(wnd->g_hWnd, queue.get(), frame_count, back_buffer_format, w, h, backbuffer_rtvs);

	// Initialised after the swap chain so backends that build their ImGui pipeline up front know the real back buffer format.
	device->imgui_init(frame_count, back_buffer_format, DXGI_FORMAT_UNKNOWN, queue.get());

	// Depth buffer
	SharedPtr<DepthStencilView> dsv;
	SharedPtr<Texture> dsv_tex;
	ShaderResourceView depth_srv;
	{
		ResourceCreateDesc rd(w, h, DXGI_FORMAT_D32_FLOAT, ResourceUsageFlags::DepthStencil);
		dsv_tex = sg::create_texture(*device, rd);
		dsv = device->create_depth_stencil_view(dsv_tex);
		depth_srv = device->create_shader_resource_view(dsv_tex);
	}

	// Output render target
	SharedPtr<Texture> final_render_target;
	SharedPtr<RenderTargetView> final_rtv;
	ShaderResourceView final_srv;

	SharedPtr<Texture> intermediate_render_target;
	ShaderResourceView intermediate_srv;
	UnorderedAccessView intermediate_uav;
	SharedPtr<RenderTargetView> intermediate_rtv;
	{
		ResourceCreateDesc rcd = {};
		rcd.width = w;
		rcd.height = h;
		rcd.format = back_buffer_format;
		rcd.usage_flags = ResourceUsageFlags::RenderTarget | ResourceUsageFlags::UnorderedAccess;
		rcd.optimised_clear_colour = { 0.0f, 1.0f, 0.0f, 1.0f }; // The colour both targets are cleared to each frame
		final_render_target = create_texture(*device, rcd);
		final_rtv = device->create_render_target_view(final_render_target);
		final_srv = device->create_shader_resource_view(final_render_target);

		intermediate_render_target = create_texture(*device, rcd);
		intermediate_uav = device->create_unordered_access_view(intermediate_render_target);
		intermediate_srv = device->create_shader_resource_view(intermediate_render_target);
		intermediate_rtv = device->create_render_target_view(intermediate_render_target);

	}
	// The PBR viewer writes its tone mapped image straight into the final render target
	UnorderedAccessView final_uav = device->create_unordered_access_view(final_render_target);

	//Pipeline
	SharedPtr<Pipeline> pipeline;
	SharedPtr<Pipeline> pipeline_cb;
	SharedPtr<Pipeline> pipeline_b;
	SharedPtr<Pipeline> pipeline_compute;
	{
		BindingDesc bd;
		//bd.cbv_binding_count = 1;

		PipelineDesc::Graphics desc;
		desc.pixel_shader = ps;
		desc.vertex_shader = vs;
		desc.render_target_count = 1;
		desc.render_target_format_list[0] = back_buffer_format;
		desc.depth_stencil_desc.depth_enable = false;
		desc.depth_stencil_desc.depth_write = false;
		pipeline = device->create_pipeline(desc, bd);

		bd.cbv_binding_count = 1;
		desc.pixel_shader = ps_cb;
		pipeline_cb = device->create_pipeline(desc, bd);

		bd.cbv_binding_count = 0;
		bd.srv_binding_count = 1;
		desc.pixel_shader = ps_b;
		pipeline_b = device->create_pipeline(desc, bd);
	}
	{
		BindingDesc bd;
		bd.uav_binding_count = 1;
		PipelineDesc::Compute desc;
		desc.compute_shader = cs;
		pipeline_compute = device->create_pipeline(desc, bd);
	}

	//CB
	mem = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	SharedPtr<Buffer> cbfr = device->create_buffer(mem, 64ull * 1024, BufferType::Constant, false);
	ConstantBufferView cbv = device->create_constant_buffer_view(cbfr.get(), 0, 256);

	SharedPtr<Memory> upload_heap = device->allocate_memory(MemoryType::Upload, MemorySubType::None, 64ull * 1024);
	SharedPtr<Buffer> upload_buffer = device->create_buffer(upload_heap, 64ull * 1024, BufferType::Upload, false);
	{
		float data[4] = { 1.0f,0.0f,0.0f,1.0f };
		upload_buffer->write_memory(0, data, sizeof(data));
	}

	SharedPtr<GPUTimestampPool> timestamp_pool = device->create_gpu_timestamp_pool(queue.get(), 1024);
	SharedPtr<GPUStatisticPool> stats_pool = device->create_gpu_statistic_pool(queue.get(), 32);

	SharedPtr<Memory> mem_uav = device->allocate_memory(MemoryType::GPUOptimal, MemorySubType::Buffer, 64ull * 1024);
	SharedPtr<Buffer> bfr_uav = device->create_buffer(mem_uav, 64ull * 1024, BufferType::GeneralDataBuffer, true);
	UnorderedAccessView uav_uav = device->create_unordered_access_view(bfr_uav, sizeof(float) * 4, 1);
	ShaderResourceView srv_uav = device->create_shader_resource_view(bfr_uav, sizeof(float) * 4, 1);

	Ptr<sg::UploadHeap> frame_upload_heap(new sg::UploadHeap(device.get(), frame_count, 1024 * 1024 * 256));
	Ptr<SimpleLinearConstantBuffer> linear_cb(new SimpleLinearConstantBuffer(device, 1024 * 1024));

	Camera camera;
	camera.SetWidthHeight((float)w, (float)h);
	camera.SetPosition({ 0.0f, 0.25f, -2.0f });

	sg::Ptr<DebugDraw> debug_draw = sg::Ptr<DebugDraw>(new DebugDraw(*device));

	sg::SharedPtr<sg::Texture> dds_texture;

	BasicGrid basic_grid(device);
	Ptr<ModelViewer> model_viewer = Ptr<ModelViewer>(new ModelViewer(device));
	Ptr<MagnifyingGlass> magnifying_glass(new MagnifyingGlass(device));
	Ptr< PostProcess> post_process(new PostProcess(device));
	device->set_imgui_viewer_texture(magnifying_glass->render_texture);
	Ptr< BitonicSort> bitonic_sort = Ptr< BitonicSort>(new BitonicSort(device));
	volatile bool run = true;

	Ptr<TextureViewer> texture_viewer(new TextureViewer(device));


	// I start this at value of 1 because the D3D12 fence doesn't like waiting on a value of 0...
	u32 total_frame_idx = 1;

	Viewport vp;
	vp.width = (u32)w;
	vp.height = (u32)h;
	ScissorRect sc;
	sc.right = (u32)w;
	sc.bottom = (u32)h;

	AverageTimer gpu_timer;
	AverageTimer cpu_timer;

	float depth_clear_value = 1.0f;

	gdr::Scene gdr_scene(device, dsv_tex);
	CameraTrackPlayer camera_track_player;
	u64 camera_track_key = ~0ull;

	texture_viewer->set_texture(gdr_scene.get_hzb_texture());

	bool enable_model_viewer = true;
	bool enable_gdr = true;

	pbr::Renderer pbr_viewer(device, queue, static_cast<u32>(w), static_cast<u32>(h));
	bool enable_pbr = false;
	for (const std::string& assignment : command_line.cvars)
	{
		std::string cvar_error;
		if (!CVars::apply_assignment(assignment, cvar_error))
		{
			seWriteLine("-cvar: %s", cvar_error.c_str());
		}
	}
	if (!command_line.pbr_scene.empty() && pbr_viewer.is_supported())
	{
		enable_pbr = true;
		enable_model_viewer = false;
		enable_gdr = false;
		pbr_viewer.load_scene(command_line.pbr_scene, &camera);
	}

	bool bOpen = true;
	float delta_time; // seconds
	float total_time; // seconds
	auto total_time_start = std::chrono::high_resolution_clock::now();
	auto last_frame_start = std::chrono::high_resolution_clock::now();
	sg::PipelineStatistics query_data = {};

	if (!command_line.model.empty())
	{
		model_viewer->SelectModel(command_line.model);
	}
	if (command_line.mesh_mode >= 0)
	{
		model_viewer->SetMeshMode(command_line.mesh_mode);
	}
	if (!command_line.gdr_scene.empty())
	{
		gdr_scene.load_scene(command_line.gdr_scene.c_str());
	}

	// Automated test plan (see TestRunner.h)
	TestRunner test_runner;
	TestRunner::Systems test_systems{ camera, camera_track_player, *model_viewer, gdr_scene, *post_process, *magnifying_glass, *debug_draw, enable_model_viewer, enable_gdr, depth_clear_value, pbr_viewer, enable_pbr };
	if (!command_line.test_plan.empty() && !test_runner.load(command_line.test_plan, command_line.test_output))
	{
		run = false;
	}

	// Screenshot captures (see CommandLineOptions and TestRunner): copies recorded at the end of a frame, saved once the GPU
	// finished it. "final" is the scene render target, "output" the image presented (after post processing, before ImGui),
	// "backbuffer" includes ImGui.
	struct PendingCapture
	{
		SharedPtr<Buffer> readback;
		u32 row_pitch = 0;
		u32 width = 0;
		u32 height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		std::string path;
	};
	std::vector<PendingCapture> pending_captures;
	auto record_capture = [&](CommandList& list, Texture& texture, std::string path)
		{
			PendingCapture capture;
			capture.width = texture.resource_create_desc.width;
			capture.height = texture.resource_create_desc.height;
			capture.format = texture.resource_create_desc.format;
			capture.readback = sg::create_readback_buffer(*device, AlignUp(capture.width * 4u, 256u) * capture.height);
			capture.row_pitch = list.copy_texture_to_buffer(capture.readback.get(), &texture);
			capture.path = std::move(path);
			pending_captures.push_back(std::move(capture));
		};
	// Screenshots from the "Slim Graphics" window (or F12): the same captures as the test runner's, written to a directory
	// with a time stamped name once the frame has finished on the GPU
	struct Screenshot
	{
		char directory[260] = "Screenshots";
		bool back_buffer = true;		// With ImGui, like the tests' _backbuffer capture
		bool output = false;			// Presented image without ImGui
		bool final = false;				// Scene render target before post processing
		bool requested = false;
		std::string last_message;
	} screenshot;
	// Interface: windows opened from the menu bar, and the hidden interface of clean screenshots (Tab)
	struct Interface
	{
		bool visible = true;
		bool controls = false;
		bool about = false;
		bool performance = false;
	} ui;
	// First launch (ImGui has not saved a layout yet): show the controls
	{
		std::error_code error;
		const char* ini = ImGui::GetIO().IniFilename;
		ui.controls = ini != nullptr && !std::filesystem::exists(ini, error) && command_line.test_plan.empty();
	}
	while (run)
	{
		auto cpu_start_time = std::chrono::high_resolution_clock::now();
		total_time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - total_time_start).count();
		delta_time = std::chrono::duration<float>(cpu_start_time - last_frame_start).count();
		last_frame_start = cpu_start_time;

		frame_upload_heap->begin_frame(queue);
		linear_cb->BeginFrame(frame_upload_heap.get());
		
		input->update();
		
device->imgui_new_frame();
		ImGui_ImplWin32_NewFrame();

		ImGui::NewFrame();
		//ImPlot::ShowDemoWindow();
		//ImGui::ShowStyleEditor();

		test_runner.begin_frame(test_systems);

		if (enable_gdr)
		{
			gdr_scene.update(frame_upload_heap);
		}

		// Keys (ignored while a text field or slider is being edited)
		if (!ImGui::GetIO().WantCaptureKeyboard)
		{
			ui.visible ^= ImGui::IsKeyPressed(ImGuiKey_Tab, false);
			ui.controls ^= ImGui::IsKeyPressed(ImGuiKey_F1, false);
			screenshot.requested |= ImGui::IsKeyPressed(ImGuiKey_F12, false);
			if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
			{
				camera_track_player.toggle();
			}
		}
		// A hidden interface is drawn fully transparent and ignores the mouse (its logic still runs)
		ImGui::GetStyle().Alpha = ui.visible ? 1.0f : 0.0f;
		ImGui::GetIO().ConfigFlags = ui.visible ? (ImGui::GetIO().ConfigFlags & ~ImGuiConfigFlags_NoMouse) : (ImGui::GetIO().ConfigFlags | ImGuiConfigFlags_NoMouse);

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("Scenes"))
			{
				ImGui::MenuItem("Model Viewer", nullptr, &enable_model_viewer);
				ImGui::MenuItem("GPU Driven Rendering", nullptr, &enable_gdr);
				if (pbr_viewer.is_supported())
				{
					ImGui::MenuItem("PBR Viewer", nullptr, &enable_pbr);
					if (ImGui::BeginMenu("Open PBR Scene"))
					{
						for (const pbr::ScenePreset& preset : pbr::scene_presets())
						{
							if (ImGui::MenuItem(preset.name))
							{
								// The PBR viewer alone: the other scenes would draw over it
								enable_pbr = true;
								enable_model_viewer = enable_gdr = false;
								pbr_viewer.load_scene(preset.name, &camera);
							}
						}
						ImGui::EndMenu();
					}
				}
				else
				{
					ImGui::MenuItem("PBR Viewer (needs hardware ray tracing, D3D12)", nullptr, false, false);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Camera"))
			{
				ImGui::SetNextItemWidth(220.0f);
				camera.SpeedSlider();
				ImGui::SetNextItemWidth(220.0f);
				ImGui::SliderFloat("Look Sensitivity", &camera.LookSensitivity(), 0.1f, 4.0f, "%.2f");
				if (enable_pbr && ImGui::MenuItem("Back to the scene's starting view"))
				{
					pbr_viewer.apply_preset_camera(camera);
				}
				if (ImGui::MenuItem(camera_track_player.is_playing() ? "Pause camera track" : "Play camera track", "Space", false, camera_track_player.has_track()))
				{
					camera_track_player.toggle();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Capture"))
			{
				screenshot.requested |= ImGui::MenuItem("Save screenshot", "F12");
				ImGui::MenuItem("Back buffer (with the interface)", nullptr, &screenshot.back_buffer);
				ImGui::MenuItem("Output (without the interface)", nullptr, &screenshot.output);
				ImGui::MenuItem("Scene render target", nullptr, &screenshot.final);
				if (ImGui::MenuItem("Open the screenshot folder"))
				{
					std::error_code error;
					std::filesystem::create_directories(screenshot.directory, error);
					ShellExecuteA(nullptr, "open", std::filesystem::absolute(screenshot.directory, error).string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
				if (!screenshot.last_message.empty())
				{
					ImGui::Separator();
					ImGui::TextDisabled("%s", screenshot.last_message.c_str());
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem("Hide interface", "Tab"))
				{
					ui.visible = false;
				}
				ImGui::MenuItem("Performance graph", nullptr, &ui.performance);
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("Controls", "F1", &ui.controls);
				ImGui::MenuItem("About Slim Graphics", nullptr, &ui.about);
				ImGui::EndMenu();
			}
			// Frame rate and CPU / GPU frame times, right aligned
			char stats[128];
			const float frame_ms = std::max(cpu_timer.get_average(), gpu_timer.get_average()) / 1000.0f;
			sprintf_s(stats, "%s   %.0f FPS   CPU %.2f ms   GPU %.2f ms", help_windows::kBackend, frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f, cpu_timer.get_average() / 1000.0f, gpu_timer.get_average() / 1000.0f);
			ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(stats).x - 16.0f);
			ImGui::TextUnformatted(stats);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Averaged over recent frames. The frame rate follows the slower of the CPU and GPU (the application waits for the GPU every frame).");
			}
			ImGui::EndMainMenuBar();
		}
		if (ui.controls)
		{
			help_windows::controls_window(&ui.controls);
		}
		if (ui.about)
		{
			help_windows::about_window(&ui.about, *device, pbr_viewer.is_supported());
		}
		if (ui.performance)
		{
			place_window_first_use(490.0f, layout_area().w - 280.0f, 560.0f, 270.0f);
			if (ImGui::Begin("Performance", &ui.performance))
			{
				ImGui::Text("CPU %.2f ms (min %.2f, max %.2f)   GPU %.2f ms (min %.2f, max %.2f)", cpu_timer.get_average() / 1000.0f, cpu_timer.min() / 1000.0f, cpu_timer.max() / 1000.0f,
					gpu_timer.get_average() / 1000.0f, gpu_timer.min() / 1000.0f, gpu_timer.max() / 1000.0f);
				if (ImPlot::BeginPlot("##frame_times", ImVec2(-1.0f, -1.0f), ImPlotFlags_NoTitle))
				{
					ImPlot::SetupAxes("Frame", "Microseconds", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
					ImPlot::SetNextLineStyle(ImVec4(247.0f / 255.0f, 227.0f / 255.0f, 121.0f / 255.0f, 1.0f));
					ImPlot::PlotLine<float>("GPU", gpu_timer.get_history_buffer(), gpu_timer.get_history_count(), 1.0, 0.0, 0, gpu_timer.get_history_idx());
					ImPlot::SetNextLineStyle(ImVec4(68.0f / 255.0f, 206.0f / 255.0f, 27.0f / 255.0f, 1.0f));
					ImPlot::PlotLine<float>("CPU", cpu_timer.get_history_buffer(), cpu_timer.get_history_count(), 1.0, 0.0, 0, cpu_timer.get_history_idx());
					ImPlot::EndPlot();
				}
			}
			ImGui::End();
		}

		place_window_first_use(-10.0f, 10.0f, 380.0f, 430.0f);
		ImGui::Begin("Slim Graphics", &bOpen, 0);
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		ImGui::TextWrapped("Scenes menu: what is drawn. Help > Controls (F1): camera keys. Tab hides the interface.");
		ImGui::PopStyleColor();

		device->imgui_update();

		// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
		
		debug_draw->Update();
		if (enable_model_viewer)
		{
			model_viewer->Update(delta_time, total_time, camera, *debug_draw);
		}
		magnifying_glass->Update(wnd->g_hWnd, *input, delta_time, total_time, camera, *debug_draw);
		basic_grid.Update(delta_time, total_time, camera);
		post_process->Update(wnd->g_hWnd, *input, delta_time, total_time, camera, *debug_draw);

		// The camera track: the PBR viewer's scene track while it is enabled, else the loaded GDR scene's
		const u64 track_key = enable_pbr ? (1ull << 32) | pbr_viewer.get_camera_track_version() : gdr_scene.get_scene_load_count();
		if (track_key != camera_track_key && !test_runner.is_active())
		{
			camera_track_key = track_key;
			camera_track_player.set_track(enable_pbr ? pbr_viewer.get_camera_track() : gdr_scene.get_camera_track());
			if (command_line.camera_track)
			{
				camera_track_player.play(command_line.camera_track_time);
			}
		}
		camera_track_player.imgui();
		camera_track_player.update(camera);

		camera.Update(delta_time, total_time, *input);
		test_runner.after_update(test_systems);
		if (enable_pbr)
		{
			pbr_viewer.update(delta_time, camera);
		}
		if (ImGui::CollapsingHeader("GPU Pipeline Statistics"))
		{
			help_windows::pipeline_statistics(query_data);
		}
		if (ImGui::CollapsingHeader("Screenshot"))
		{
			ImGui::PushID("Screenshot");
			ImGui::InputText("Directory", screenshot.directory, sizeof(screenshot.directory));
			ImGui::TextDisabled("Save from the Capture menu or with F12");
			ImGui::PopID();
		}
		if (ImGui::CollapsingHeader("Clear Values"))
		{
			ImGui::SliderFloat("Depth Clear", &depth_clear_value, 0.0f, 1.0f);
		}
		ImGui::End();

		// Texture Viewer
		{
			//texture_viewer->imgui_update();
		}
	
		command_buffer->start_recording();
		timestamp_pool->begin_frame();
		stats_pool->begin_frame();
		GPUTimestampPool::Index gpu_timestamp_idx = timestamp_pool->allocate_new_timestamp();
		GPUStatisticPool::Index stat_query_idx = stats_pool->allocate_new_query();
		timestamp_pool->begin_timestamp(gpu_timestamp_idx, command_buffer.get());
		{

			//Compute Dispatch
			{
				Binding b;
				b.uav_binding_count = 1;
				b.set_uav(uav_uav, 0);
				command_buffer->set_pipeline(pipeline_compute.get());
				command_buffer->bind(b, PipelineType::Compute);
				command_buffer->dispatch();
			}

			// Bitonic Sort
			{
				//bitonic_sort->sort(*command_buffer, *linear_cb);
			}

			command_buffer->start_geometry_pass(1, &intermediate_rtv, vp, sc);
			{
				float4 colour = { 0.0f,1.0f,0.0f,1.0f };
				command_buffer->clear_render_target_view(intermediate_rtv, colour);
			}
			command_buffer->end_geometry_pass();

			command_buffer->start_geometry_pass(1, &final_rtv, vp, sc);
			{
				float4 colour = { 0.0f,1.0f,0.0f,1.0f };
				command_buffer->clear_render_target_view(final_rtv, colour);
			}
			command_buffer->clear_depth_stencil_view(dsv, true, false, depth_clear_value);
			{
				command_buffer->copy_buffer_to_buffer(cbfr.get(), upload_buffer.get());
			}
			{
				command_buffer->set_pipeline(pipeline.get());
				command_buffer->draw_instanced(6, 1, 0, 0);
			}
			{
				command_buffer->set_pipeline(pipeline_cb.get());
				Binding b;
				b.cbv_binding_count = 1;
				b.set_cbv(cbv, 0);
				command_buffer->bind(b, PipelineType::Geometry);
				command_buffer->draw_instanced(6, 1, 0, 0);
			}
			{
				command_buffer->set_pipeline(pipeline_b.get());
				Binding b;
				b.srv_binding_count = 1;
				b.set_srv(srv_uav, 0);
				command_buffer->bind(b, PipelineType::Geometry);
				command_buffer->draw_instanced(6, 1, 0, 0);
			}

			command_buffer->end_geometry_pass();

			sg::ConstantBufferView cbv_cam = linear_cb->AllocateAndWrite<ShaderStructs::CameraData>(camera.GetCameraShaderData());

			command_buffer->start_geometry_pass(1, &final_rtv, vp, sc, dsv);
			{ // Model Viewer
				stats_pool->begin_query(stat_query_idx, command_buffer.get());
				if (enable_model_viewer)
				{
					model_viewer->Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb, *debug_draw);
				}
				if (enable_gdr)
				{
					gdr_scene.render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb, *debug_draw, depth_srv);
				}				
				stats_pool->end_query(stat_query_idx, command_buffer.get());
				basic_grid.Render(*command_buffer, camera, cbv_cam, frame_upload_heap, *linear_cb);
				debug_draw->Render(*command_buffer, cbv_cam);
			}
			command_buffer->end_geometry_pass();

			gdr_scene.generate_hzb(*command_buffer, depth_srv, *linear_cb);

			if (enable_pbr)
			{
				pbr_viewer.render(*command_buffer, camera, frame_upload_heap, *linear_cb, final_uav);
			}

			SharedPtr<Texture> copy_to_swap_chain_target = final_render_target;
			ShaderResourceView copy_to_swap_chain_target_view = final_srv;
			// Post Process
			{
			
				if (post_process->Render(*command_buffer, cbv_cam, intermediate_uav, final_srv, depth_srv, *linear_cb, w, h))
				{
					copy_to_swap_chain_target = intermediate_render_target;
					copy_to_swap_chain_target_view = intermediate_srv;
				}
			}

			command_buffer->copy_texture_to_texture(*(backbuffer_rtvs[current_frame_idx]->texture_resource), *copy_to_swap_chain_target);

			// Magnify & Texture Viewer
			{
				magnifying_glass->Render(*command_buffer, cbv_cam, copy_to_swap_chain_target_view, *linear_cb);
				//texture_viewer->render(*command_buffer, *linear_cb);
			}

			command_buffer->start_geometry_pass(1, &backbuffer_rtvs[current_frame_idx], vp, sc);
			{
				ImGui::Render();
				device->imgui_render(command_buffer.get());
			}
			command_buffer->end_geometry_pass();

			const bool test_capture = !test_runner.capture_name().empty();
			if (test_capture || (!test_runner.is_active() && (int)total_frame_idx == command_line.capture_frame))
			{
				auto path = [&](const char* suffix) { return test_capture ? test_runner.capture_path(suffix) : command_line.capture_dir + "/capture" + suffix; };
				record_capture(*command_buffer, *final_render_target, path("_final.png"));
				record_capture(*command_buffer, *copy_to_swap_chain_target, path("_output.png"));
				record_capture(*command_buffer, *backbuffer_rtvs[current_frame_idx]->texture_resource, path("_backbuffer.png"));
				if (test_capture && test_runner.capture_magnifier() && magnifying_glass->active)
				{
					record_capture(*command_buffer, *magnifying_glass->render_texture, path("_magnifier.png"));
				}
			}
			else if (screenshot.requested && !test_runner.is_active())
			{
				screenshot.requested = false;
				std::error_code error;
				std::filesystem::create_directories(screenshot.directory, error);
				if (error)
				{
					screenshot.last_message = std::string("Could not create ") + screenshot.directory + ": " + error.message();
				}
				else
				{
					const std::time_t now = std::time(nullptr);
					std::tm local = {};
					localtime_s(&local, &now);
					char name[64];
					std::strftime(name, sizeof(name), "SlimGraphics_%Y%m%d_%H%M%S", &local);
					const std::string base = std::string(screenshot.directory) + "/" + name + "_" + std::to_string(total_frame_idx);
					const size_t first = pending_captures.size();
					if (screenshot.back_buffer)
					{
						record_capture(*command_buffer, *backbuffer_rtvs[current_frame_idx]->texture_resource, base + "_backbuffer.png");
					}
					if (screenshot.output)
					{
						record_capture(*command_buffer, *copy_to_swap_chain_target, base + "_output.png");
					}
					if (screenshot.final)
					{
						record_capture(*command_buffer, *final_render_target, base + "_final.png");
					}
					screenshot.last_message.clear();
					for (size_t i = first; i < pending_captures.size(); i++)
					{
						std::error_code absolute_error;
						const std::filesystem::path absolute = std::filesystem::absolute(pending_captures[i].path, absolute_error);
						screenshot.last_message += "Saved " + (absolute_error ? pending_captures[i].path : absolute.string()) + "\n";
					}
					if (first == pending_captures.size())
					{
						screenshot.last_message = "Nothing selected to save";
					}
				}
			}
		}


		timestamp_pool->end_timestamp(gpu_timestamp_idx, command_buffer.get());
		timestamp_pool->end_frame(command_buffer.get());
		stats_pool->end_frame(command_buffer.get());
		command_buffer->end_recording();

		linear_cb->EndFrame();
		frame_upload_heap->end_frame();
		
		queue->submit_command_list(command_buffer.get());
		current_frame_idx = device->present_swap_chain(queue.get());
		queue->fence_signal(*fence, total_frame_idx);
		queue->fence_wait_cpu(*fence, total_frame_idx);

		//Timestamps
		timestamp_pool->resolve();
		gpu_timer.add_time(timestamp_pool->collect_timestamp_us(gpu_timestamp_idx));

		stats_pool->resolve();
		query_data = stats_pool->collect_query(stat_query_idx);

		if (!pending_captures.empty())
		{
			for (PendingCapture& capture : pending_captures)
			{
				SaveReadbackToPNG(*capture.readback, capture.row_pitch, capture.width, capture.height, capture.format, capture.path);
			}
			pending_captures.clear();
			if (command_line.exit_after_capture && !test_runner.is_active() && (int)total_frame_idx == command_line.capture_frame)
			{
				run = false;
			}
		}
		test_runner.end_frame(test_systems, query_data);
		if (test_runner.is_finished())
		{
			run = false;
		}

		total_frame_idx++;
		wnd->Poll();
		if (wnd->quit_requested)
		{
			run = false;
		}

		cpu_timer.add_time(std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - cpu_start_time).count() * 1000000.0);
	}

	// Make sure the GPU is idle before anything it may still reference is destroyed.
	queue->fence_signal(*fence, total_frame_idx);
	queue->fence_wait_cpu(*fence, total_frame_idx);

device->imgui_shutdown();
	ImGui_ImplWin32_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	return 0;
}