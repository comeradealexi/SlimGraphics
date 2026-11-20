#pragma once
#include "gdrJSON.h"
#include <DirectXMath.h>
#include <vector>

namespace gdr
{
	// Evaluates a CameraTrack as a closed centripetal Catmull-Rom spline (no cusps or self intersections between points)
	// through the point positions, and another through the targets. Time is mapped by arc length along the position spline,
	// so the camera moves at a constant speed. The result depends only on the track and the time, so it is deterministic.
	class CameraSpline
	{
	public:
		void build(const CameraTrack& track);
		bool is_valid() const { return valid; }
		float get_duration() const { return duration; }
		const CameraTrack& get_track() const { return track; }

		// time wraps around the loop
		void evaluate(float time, DirectX::XMFLOAT3& out_position, DirectX::XMFLOAT3& out_target) const;

	private:
		static constexpr unsigned SAMPLES_PER_SEGMENT = 64;

		// Position / target at parameter u in [0, point count): integer part = segment, fraction = position in segment
		DirectX::XMVECTOR sample_position(float u) const;
		DirectX::XMVECTOR sample_target(float u) const;

		CameraTrack track;
		bool valid = false;
		float duration = 0.0f;
		// Arc length at parameter u = i / SAMPLES_PER_SEGMENT (one extra entry closes the loop)
		std::vector<float> arc_length;
	};
}
