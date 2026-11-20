// SPDX-License-Identifier: GPL-3.0-or-later
#include "pattern_server.h"
#include "fake_headset.h"
#include "check.h"
#include <cmath>
struct Observer : SessionListener {
 std::atomic<unsigned> heads{0}, hands{0}, bodies{0}, malformed{0}, inactive_hands{0}, invalid_grips{0}, valid_bodies{0};
 void on_tracking(std::span<const TrackedPose> p) override {
  if(!p.empty())++heads;
  for(const auto &v:p) if(v.device==Device::left_grip && !v.position_valid) ++invalid_grips;
 }
 void on_hand(const HandTracking &h) override {
  ++hands;if(!h.active) ++inactive_hands;
  if(h.hand>1 || std::abs(h.joint[5].pose.position[0]-.05f)>.001f || h.received_at<=0) ++malformed;
 }
 void on_body(const BodyTracking &b) override {
  ++bodies;if(!b.full_body || b.joint.size()!=9 || std::abs(b.joint[0].pose.position[0]-1.01f)>.001f)++malformed;
  if(b.confidence>0) ++valid_bodies;
 }
};
int main(){
 for(bool tcp : {false,true}) {
  Observer observer;
  auto region=std::make_unique<vra_region>();vra_region_init(region.get());
  Server server({.tcp_only=tcp,.encryption=WivrnConfig::Encryption::disabled},tcp ? region.get() : nullptr,&observer);
  auto result=run_fake_headset({.port=server.session.port(),.frames=15,.controllers=true,.skeletons=true});
  CHECK(result.error.empty());CHECK(result.requested_hands);CHECK(result.requested_body);CHECK(observer.heads>0);CHECK(observer.hands>0);CHECK(observer.bodies>0);CHECK(observer.malformed==0);
  CHECK(server.session.stats().last_tracking_received_ns>0);
  CHECK(observer.inactive_hands==0);
 }
 {
  Observer observer;Server server({.tcp_only=true,.encryption=WivrnConfig::Encryption::disabled},nullptr,&observer);
  auto result=run_fake_headset({.port=server.session.port(),.frames=20,.controllers=true,.skeletons=true,.untracked_hands=true});
  CHECK(result.error.empty());CHECK(observer.hands>0);CHECK(observer.inactive_hands==observer.hands);CHECK(observer.invalid_grips>0);
 }
 {
  Observer observer;Server server({.tcp_only=true,.encryption=WivrnConfig::Encryption::disabled},nullptr,&observer);
  auto result=run_fake_headset({.port=server.session.port(),.frames=360,.controllers=true,.skeletons=true,.body_stuck_until_restart=true});
  CHECK(result.error.empty());CHECK(result.body_suspensions==1);CHECK(result.body_restarts==1);
  CHECK(server.session.stats().body_restarts==1);CHECK(observer.valid_bodies>0);
 }
 {
  Observer observer;Server server({.tcp_only=true,.encryption=WivrnConfig::Encryption::disabled},nullptr,&observer);
  auto result=run_fake_headset({.port=server.session.port(),.frames=15,.controllers=true,.skeletons=true,.stale_tracking=true});
  CHECK(result.error.empty());CHECK(observer.heads==0);CHECK(observer.hands==0);CHECK(observer.bodies==0);
 }
 return test_result("tracking_packets");
}
