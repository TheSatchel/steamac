// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "session/pose_history.h"
#include <algorithm>
#include <mutex>

namespace vrhost
{
// Keep brief optical occlusion still, then ease only the reacquisition correction.
// Normal tracked motion is not filtered. Long loss still becomes an invalid pose.
class HandPoseRecovery
{
public:
	static constexpr ns hold_ns = 180'000'000, blend_ns = 160'000'000;
	void clear() { std::lock_guard lock(mutex); last.reset(); lost_at = blend_at = 0; }
	std::optional<TrackedPose> at(std::optional<TrackedPose> raw, ns query, ns now)
	{
		std::lock_guard lock(mutex);
		if (raw && !raw->hand_tracking) { last.reset(); lost_at = blend_at = 0; return raw; }
		if (!raw || !raw->orientation_valid || !raw->position_valid) {
			if (!lost_at) lost_at = now;
			blend_at = 0;
			if (last && now-lost_at <= hold_ns) {
				auto held = *last; held.time = query; held.inferred = true;
				held.linear_velocity = held.angular_velocity = {};
				return held;
			}
			return raw;
		}
		if (lost_at && last) {
			for (int i=0;i<3;i++) offset[i] = last->pose.position[i]-raw->pose.position[i];
			auto q = raw->pose.orientation;
			correction = multiply(Pose{.orientation=last->pose.orientation},
			                      Pose{.orientation={-q[0],-q[1],-q[2],q[3]}}).orientation;
			blend_at = now;
		}
		lost_at = 0;
		auto result = *raw;
		if (blend_at && now-blend_at < blend_ns) {
			float u = std::clamp(float(now-blend_at)/float(blend_ns),0.f,1.f);
			float remaining = 1-u*u*(3-2*u);
			for (int i=0;i<3;i++) result.pose.position[i] += offset[i]*remaining;
			auto delta = slerp({0,0,0,1},correction,remaining);
			result.pose.orientation = multiply(Pose{.orientation=delta},Pose{.orientation=raw->pose.orientation}).orientation;
			result.linear_velocity = result.angular_velocity = {};
			result.inferred = true;
		} else blend_at = 0;
		last = result;
		return result;
	}
private:
	std::mutex mutex;
	std::optional<TrackedPose> last;
	ns lost_at = 0, blend_at = 0;
	std::array<float,3> offset{};
	std::array<float,4> correction{0,0,0,1};
};
}
