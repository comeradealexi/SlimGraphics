#include "gdrCameraTrack.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace gdr
{
	namespace
	{
		XMVECTOR load(const Float3& f)
		{
			return XMVectorSet(f.x, f.y, f.z, 0.0f);
		}

		// Barry-Goldman evaluation of a centripetal (alpha = 0.5) Catmull-Rom segment from p1 to p2 at t in [0, 1]
		XMVECTOR catmull_rom(XMVECTOR p0, XMVECTOR p1, XMVECTOR p2, XMVECTOR p3, float t)
		{
			// Knot spacing is the square root of the distance; a minimum keeps coincident points from dividing by zero
			auto knot = [](XMVECTOR a, XMVECTOR b) { return std::max(std::sqrt(XMVectorGetX(XMVector3Length(XMVectorSubtract(b, a)))), 1e-4f); };
			const float t0 = 0.0f;
			const float t1 = t0 + knot(p0, p1);
			const float t2 = t1 + knot(p1, p2);
			const float t3 = t2 + knot(p2, p3);
			const float u = t1 + (t2 - t1) * t;

			auto mix = [](XMVECTOR a, XMVECTOR b, float ta, float tb, float x) { return XMVectorLerp(a, b, (x - ta) / (tb - ta)); };
			const XMVECTOR a1 = mix(p0, p1, t0, t1, u);
			const XMVECTOR a2 = mix(p1, p2, t1, t2, u);
			const XMVECTOR a3 = mix(p2, p3, t2, t3, u);
			const XMVECTOR b1 = mix(a1, a2, t0, t2, u);
			const XMVECTOR b2 = mix(a2, a3, t1, t3, u);
			return mix(b1, b2, t1, t2, u);
		}

		template <typename Get>
		XMVECTOR sample_loop(size_t count, float u, Get get)
		{
			const float segment_f = std::floor(u);
			const size_t segment = static_cast<size_t>(segment_f) % count;
			const float t = u - segment_f;
			return catmull_rom(get((segment + count - 1) % count), get(segment), get((segment + 1) % count), get((segment + 2) % count), t);
		}
	}

	XMVECTOR CameraSpline::sample_position(float u) const
	{
		return sample_loop(track.points.size(), u, [this](size_t i) { return load(track.points[i].position); });
	}

	XMVECTOR CameraSpline::sample_target(float u) const
	{
		return sample_loop(track.points.size(), u, [this](size_t i) { return load(track.points[i].target); });
	}

	void CameraSpline::build(const CameraTrack& new_track)
	{
		track = new_track;
		valid = track.is_valid();
		duration = track.duration;
		arc_length.clear();
		if (!valid)
		{
			return;
		}

		const size_t sample_count = track.points.size() * SAMPLES_PER_SEGMENT;
		arc_length.resize(sample_count + 1);
		arc_length[0] = 0.0f;
		XMVECTOR previous = sample_position(0.0f);
		for (size_t i = 1; i <= sample_count; i++)
		{
			const XMVECTOR current = sample_position(static_cast<float>(i) / SAMPLES_PER_SEGMENT);
			arc_length[i] = arc_length[i - 1] + XMVectorGetX(XMVector3Length(XMVectorSubtract(current, previous)));
			previous = current;
		}
		valid = arc_length.back() > 0.0f;
	}

	void CameraSpline::evaluate(float time, XMFLOAT3& out_position, XMFLOAT3& out_target) const
	{
		if (!valid)
		{
			return;
		}
		// Distance along the loop, then the spline parameter at that distance (piecewise linear between samples)
		float loop = std::fmod(time / duration, 1.0f);
		if (loop < 0.0f) loop += 1.0f;
		const float distance = loop * arc_length.back();
		const auto it = std::upper_bound(arc_length.begin(), arc_length.end(), distance);
		const size_t i = static_cast<size_t>(std::clamp<ptrdiff_t>(it - arc_length.begin(), 1, static_cast<ptrdiff_t>(arc_length.size() - 1)));
		const float span = arc_length[i] - arc_length[i - 1];
		const float fraction = span > 0.0f ? (distance - arc_length[i - 1]) / span : 0.0f;
		const float u = (static_cast<float>(i - 1) + fraction) / SAMPLES_PER_SEGMENT;

		XMStoreFloat3(&out_position, sample_position(u));
		XMStoreFloat3(&out_target, sample_target(u));
	}
}
