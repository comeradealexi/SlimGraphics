#pragma once
#include <DirectXMath.h>
#include "ShaderSharedStructures.h"
#include <Win32/seGameInput.h>

class Camera
{
public:
	enum class ProjectionType
	{
		Perspective,
		Orthographic
	};

	void Update(float fTimeDelta, float fTotalTime, const se::GameInput& input);
	void SetWidthHeight(float _width, float _height);
	const ShaderStructs::CameraData& GetCameraShaderData() const { return shader_data; }

private:
	ShaderStructs::CameraData shader_data;

private:
	ProjectionType projection_type = ProjectionType::Perspective;
	bool imgui_perspective = true;

	float width = 0.0f;
	float height = 0.0f;
	DirectX::XMFLOAT3A position = {};
	DirectX::XMVECTOR direction = {};
	float camera_rot_y = 1.5708f;
	float camera_rot_x = 1.5708f;
	float min_depth_plane = 0.1f;
	float max_depth_plane = 100.0f;
	float field_of_view = ::DirectX::XM_PIDIV4;

	float ortho_view_width = 16.0f;
	float ortho_view_height = 9.0f;
	float ortho_near_z = 0.1f;
	float ortho_far_z = 100.0f;
};
