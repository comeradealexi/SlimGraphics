#include "Camera.h"
#include <imgui.h>
#include <implot.h>

using namespace DirectX;

void Camera::Update(float fTimeDelta, float fTotalTime, const se::GameInput& input)
{
	if (true)
	{
		shader_data.screen_dimensions_and_depth_info = XMFLOAT4A(width, height, min_depth_plane, max_depth_plane);

		const XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

		direction = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		direction = XMVector3Transform(direction, XMMatrixRotationRollPitchYaw(0.0f, camera_rot_y, camera_rot_x));
		direction = XMVector3Normalize(direction);
		
		XMVECTOR Eye = XMVectorSet(position.x, position.y, position.z, 0.0f);
		XMVECTOR At = XMVectorSet(
			position.x + XMVectorGetX(direction),
			position.y + XMVectorGetY(direction),
			position.z + XMVectorGetZ(direction), 0.0f);

		shader_data.view_matrix = (XMMatrixLookAtLH(Eye, At, Up));
		if (projection_type == ProjectionType::Perspective)
		{
			shader_data.projection_matrix = XMMatrixPerspectiveFovLH(field_of_view, width / height, min_depth_plane, max_depth_plane);
		}
		else
		{
			shader_data.projection_matrix = XMMatrixOrthographicLH(ortho_view_width, ortho_view_height, ortho_near_z, ortho_far_z);
		}

		BoundingFrustum::CreateFromMatrix(bounding_frustum, shader_data.projection_matrix);
		// The frustum (near plane, far plane, fov) is in view space so multiplying it with an inverse view matrix will move it into world space
		DirectX::XMMATRIX inverseViewMatrix = DirectX::XMMatrixInverse(nullptr, shader_data.view_matrix);
		bounding_frustum.Transform(bounding_frustum, inverseViewMatrix);
		BoundingSphere::CreateFromFrustum(bounding_sphere, bounding_frustum);

		shader_data.camera_position = { position.x, position.y, position.z, 0.0f };
		XMStoreFloat4(&shader_data.camera_direction, XMVector3Normalize(XMVectorSubtract(Eye, At)));

		//Process input now.
		if (input.IsKeyDown(L'W'))
		{
			position.x += XMVectorGetX(direction) * fTimeDelta;
			position.y += XMVectorGetY(direction) * fTimeDelta;
			position.z += XMVectorGetZ(direction) * fTimeDelta;
		}
		if (input.IsKeyDown(L'S'))
		{
			position.x -= XMVectorGetX(direction) * fTimeDelta;
			position.y -= XMVectorGetY(direction) * fTimeDelta;
			position.z -= XMVectorGetZ(direction) * fTimeDelta;
		}
		if (input.IsKeyDown(L'A'))
		{
			::DirectX::XMVECTOR crossVector = ::DirectX::XMVector3Cross(direction, Up);
			crossVector = XMVector3Normalize(crossVector);
			position.x += XMVectorGetX(crossVector) * fTimeDelta;
			position.y += XMVectorGetY(crossVector) * fTimeDelta;
			position.z += XMVectorGetZ(crossVector) * fTimeDelta;
		}
		if (input.IsKeyDown(L'D'))
		{
			::DirectX::XMVECTOR crossVector = ::DirectX::XMVector3Cross(direction, Up);
			crossVector = XMVector3Normalize(crossVector);
			position.x -= XMVectorGetX(crossVector) * fTimeDelta;
			position.y -= XMVectorGetY(crossVector) * fTimeDelta;
			position.z -= XMVectorGetZ(crossVector) * fTimeDelta;
		}

		if (/*input.IsKeyDown(Input::Keys::NUMPAD_4_LEFT) ||*/ input.IsKeyDown(L'J'))
		{
			camera_rot_y -= 1.0f * fTimeDelta;
		}
		if (/*input.IsKeyDown(Input::Keys::NUMPAD_6_RIGHT) ||*/ input.IsKeyDown(L'L'))
		{
			camera_rot_y += 1.0f * fTimeDelta;
		}
		if (/*input.IsKeyDown(Input::Keys::NUMPAD_8_UP) ||*/ input.IsKeyDown(L'I'))
		{
			camera_rot_x -= 1.0f * fTimeDelta;
		}
		if (/*input.IsKeyDown(Input::Keys::NUMPAD_5_MIDDLE) ||*/ input.IsKeyDown(L'K'))
		{
			camera_rot_x += 1.0f * fTimeDelta;
		}

		if (input.IsKeyDown(L'Q'))
		{
			position.y += 1.0f * fTimeDelta;
		}

		if (input.IsKeyDown(L'E'))
		{
			position.y -= 1.0f * fTimeDelta;
		}
	}

	shader_data.view_projection_matrix = XMMatrixMultiply(shader_data.view_matrix, shader_data.projection_matrix);

	//ImGui
	{
		if (ImGui::CollapsingHeader("Camera"))
		{
			ImGui::DragFloat3("Position", &position.x, 0.001f);
			//ImGui::SliderFloat2("Rotation", &m_fCameraRotY, -360.0f, 360.0f);
			ImGui::DragFloat2("Rotation", &camera_rot_y, 0.001f);

			ImGui::Checkbox("Perspective", &imgui_perspective);
			if (imgui_perspective)
			{
				projection_type = ProjectionType::Perspective;
				ImGui::DragFloat("MinDepth", &min_depth_plane, 0.001f);
				ImGui::DragFloat("MaxDepth", &max_depth_plane, 0.1f);
				ImGui::DragFloat("FOV", &field_of_view, 0.001f);
			}
			else
			{
				projection_type = ProjectionType::Orthographic;
				ImGui::DragFloat("ViewWidth", &ortho_view_width, 0.001f);
				ImGui::DragFloat("ViewHeight", &ortho_view_height, 0.001f);
				ImGui::DragFloat("NearZ", &ortho_near_z, 0.001f);
				ImGui::DragFloat("FarZ", &ortho_far_z, 0.001f);
			}

			//ImGui::Checkbox("Update", &m_do_update);
			ImGui::Text("View");
			ImGui::PushID("Camera-View");
			ImGui::InputFloat4("C0", (float*)&(shader_data.view_matrix.r[0]), "%.3f");
			ImGui::InputFloat4("C1", (float*)&(shader_data.view_matrix.r[1]), "%.3f");
			ImGui::InputFloat4("C2", (float*)&(shader_data.view_matrix.r[2]), "%.3f");
			ImGui::InputFloat4("C3", (float*)&(shader_data.view_matrix.r[3]), "%.3f");
			ImGui::PopID();

			ImGui::Text("Proj");
			ImGui::PushID("Camera-Proj");
			ImGui::InputFloat4("C0", (float*)&(shader_data.projection_matrix.r[0]), "%.3f");
			ImGui::InputFloat4("C1", (float*)&(shader_data.projection_matrix.r[1]), "%.3f");
			ImGui::InputFloat4("C2", (float*)&(shader_data.projection_matrix.r[2]), "%.3f");
			ImGui::InputFloat4("C3", (float*)&(shader_data.projection_matrix.r[3]), "%.3f");
			ImGui::PopID();

			ImGui::Text("ViewProj");
			ImGui::PushID("Camera-ViewProj");
			ImGui::InputFloat4("C0", (float*)&(shader_data.view_projection_matrix.r[0]), "%.3f");
			ImGui::InputFloat4("C1", (float*)&(shader_data.view_projection_matrix.r[1]), "%.3f");
			ImGui::InputFloat4("C2", (float*)&(shader_data.view_projection_matrix.r[2]), "%.3f");
			ImGui::InputFloat4("C3", (float*)&(shader_data.view_projection_matrix.r[3]), "%.3f");
			ImGui::PopID();

		}
	}
}

void Camera::SetWidthHeight(float _width, float _height)
{
	width = _width;
	height = _height;
}

bool Camera::IsInFrustum_Accurate(const DirectX::BoundingSphere& sphere) const
{
	return bounding_frustum.Intersects(sphere);
}

bool Camera::IsInFrustum_Fast(const DirectX::BoundingSphere& sphere) const
{
	return bounding_sphere.Intersects(sphere);
}
