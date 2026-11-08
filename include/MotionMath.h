#pragma once

#include <cmath>
#include <numbers>

namespace AMF
{
	inline bool UndoPitch(float a_pitch, float& a_y, float& a_z) noexcept
	{
		if (!std::isfinite(a_pitch) || !std::isfinite(a_y) || !std::isfinite(a_z) ||
			std::abs(a_pitch) >= std::numbers::pi_v<float> / 2.0f) {
			return false;
		}
		if (a_pitch == 0.0f) {
			return true;
		}
		const auto cosine = std::cos(a_pitch);
		if (cosine < 1.0e-4f) {
			return false;
		}
		const auto y = a_y / cosine;
		const auto z = a_z - y * std::sin(a_pitch);
		if (!std::isfinite(y) || !std::isfinite(z)) {
			return false;
		}
		a_y = y;
		a_z = z;
		return true;
	}
}
