// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.h"
#include "session/pose_history.h"
#include "session/body_recovery.h"
#include "session/hand_pose_recovery.h"
#include <cmath>

using namespace vrhost;

static bool near(float a, float b, float eps = 1e-3f)
{
	return std::abs(a - b) < eps;
}

static Pose yaw(float a, float x = 0)
{
	return Pose{.orientation = {0, std::sin(a / 2), 0, std::cos(a / 2)}, .position = {x, 0, 0}};
}

int main()
{
	PoseHistory h;
	CHECK(not h.at(0));

	h.add({.device = Device::head, .time = 1000, .pose = yaw(0, 0), .orientation_valid = true});
	h.add({.device = Device::head, .time = 3000, .pose = yaw(1.0f, 2), .orientation_valid = true});

	// interpolation at the midpoint: yaw 0.5, x = 1
	auto m = h.at(2000);
	REQUIRE(m);
	CHECK(near(m->pose.orientation[1], std::sin(0.25f)));
	CHECK(near(m->pose.position[0], 1));

	// exact sample
	CHECK(near(h.at(3000)->pose.position[0], 2));
	// before the oldest: held
	CHECK(near(h.at(0)->pose.position[0], 0));

	// extrapolation with angular velocity 1 rad/s around +Y and linear 1 m/s on X, 50 ms ahead
	PoseHistory e;
	e.add({.device = Device::head, .time = 0, .pose = yaw(0), .linear_velocity = {1, 0, 0}, .angular_velocity = {0, 1, 0}, .orientation_valid = true});
	auto x = e.at(50'000'000);
	REQUIRE(x);
	CHECK(near(x->pose.position[0], 0.05f));
	CHECK(near(x->pose.orientation[1], std::sin(0.025f)));
	// bounded at max_extrapolation (100 ms)
	auto far = e.at(1'000'000'000);
	CHECK(near(far->pose.position[0], 0.1f));
	CHECK(!far->orientation_valid);
	CHECK((far->linear_velocity == std::array<float, 3>{}));
	CHECK((far->angular_velocity == std::array<float, 3>{}));

	// A new loss overrides an older prediction even when the prediction targets a later time.
	PoseHistory lost;
	lost.add({.device=Device::left_grip, .time=140'000'000, .pose=yaw(0,1),
	          .linear_velocity={1,0,0}, .orientation_valid=true, .position_valid=true, .source_time=100'000'000});
	lost.add({.device=Device::left_grip, .time=110'000'000, .pose=yaw(0,1),
	          .orientation_valid=false, .position_valid=false, .source_time=110'000'000});
	CHECK(!lost.at(150'000'000)->orientation_valid);
	CHECK(near(lost.at(150'000'000)->pose.position[0],1));
	lost.add({.device=Device::left_grip, .time=150'000'000, .pose=yaw(0,9),
	          .orientation_valid=true, .position_valid=true, .source_time=100'000'000});
	CHECK(!lost.at(160'000'000)->orientation_valid); // reordered old packet cannot resurrect the hand
	lost.add({.device=Device::left_grip, .time=170'000'000, .pose=yaw(0,2),
	          .orientation_valid=true, .position_valid=true, .source_time=160'000'000});
	CHECK(lost.at(170'000'000)->position_valid);
	CHECK(near(lost.at(170'000'000)->pose.position[0],2)); // reacquire without sweeping through stale positions
	CHECK(!lost.at(400'000'000)->position_valid);

	// Brief optical loss holds still. Reacquisition blends the correction, not normal hand motion.
	HandPoseRecovery hand;
	TrackedPose visible{.device=Device::left_grip,.time=1'000'000'000,.pose=yaw(0,0),
	                    .linear_velocity={2,0,0},.orientation_valid=true,.position_valid=true,.hand_tracking=true};
	CHECK(near(hand.at(visible,visible.time,visible.time)->pose.position[0],0));
	auto hidden=visible;hidden.orientation_valid=hidden.position_valid=false;
	auto held=hand.at(hidden,1'010'000'000,1'010'000'000);
	CHECK(held->position_valid && held->inferred);
	CHECK(near(held->pose.position[0],0) && near(held->linear_velocity[0],0));
	CHECK(!hand.at(hidden,1'200'000'000,1'200'000'000)->position_valid);
	visible.pose=yaw(1,1);
	auto start=hand.at(visible,1'210'000'000,1'210'000'000);
	CHECK(near(start->pose.position[0],0) && near(start->pose.orientation[1],0));
	visible.pose.position[0]=1.2f;
	auto middle=hand.at(visible,1'290'000'000,1'290'000'000);
	CHECK(near(middle->pose.position[0],.7f));
	CHECK(middle->inferred);
	CHECK(near(hand.at(visible,1'380'000'000,1'380'000'000)->pose.position[0],1.2f));
	visible.pose.position[0]=1.8f;
	CHECK(near(hand.at(visible,1'390'000'000,1'390'000'000)->pose.position[0],1.8f));
	hidden.hand_tracking=false; // physical controllers must retain their own tracking behavior
	CHECK(!hand.at(hidden,1'400'000'000,1'400'000'000)->position_valid);
	hand.clear();
	CHECK(!hand.at(std::nullopt,1'410'000'000,1'410'000'000));

	// Removing the headset must not trigger retries. Live-head/invalid-body recreates only BODY.
	BodyRecovery recovery;
	using Action = BodyRecovery::Action;
	for (int i=1;i<=100;++i) CHECK(recovery.update(i*1'000'000'000LL,false,false)==Action::none);
	CHECK(recovery.update(101'000'000'000LL,true,false)==Action::none);
	CHECK(recovery.update(103'000'000'000LL,true,false)==Action::suspend);
	CHECK(recovery.update(103'400'000'000LL,true,false)==Action::none);
	CHECK(recovery.update(103'500'000'000LL,true,false)==Action::resume);
	CHECK(recovery.update(110'000'000'000LL,true,false)==Action::none);
	CHECK(recovery.update(113'000'000'000LL,true,false)==Action::suspend);
	CHECK(recovery.update(114'000'000'000LL,true,false)==Action::resume);
	CHECK(recovery.update(123'000'000'000LL,true,false)==Action::suspend);
	CHECK(recovery.update(124'000'000'000LL,true,false)==Action::resume);
	CHECK(recovery.update(140'000'000'000LL,true,false)==Action::none); // bounded retry budget
	CHECK(recovery.update(141'000'000'000LL,true,true)==Action::none);
	CHECK(recovery.update(142'000'000'000LL,true,false)==Action::none);
	CHECK(recovery.update(145'000'000'000LL,true,false)==Action::suspend); // valid data rearms recovery

	// multiply: head yawed 90 degrees, eye offset +X in head space -> world -Z
	Pose head = yaw(float(M_PI / 2));
	Pose eye{.position = {0.032f, 0, 0}};
	Pose w = multiply(head, eye);
	CHECK(near(w.position[0], 0));
	CHECK(near(w.position[2], -0.032f));

	// history keeps at most max_samples, sorted
	PoseHistory big;
	for (int i = 0; i < 200; ++i)
		big.add({.device = Device::head, .time = 200 - i, .pose = yaw(0, float(200 - i)), .orientation_valid = true});
	CHECK(near(big.at(200)->pose.position[0], 200));
	return test_result("pose_history");
}
