// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.h"
#include "compositor/vrlink_server.h"
#include "session/clock.h"
extern "C" {
#include "wmtr_vr.h"
}
#include "vrlink.h"
#include "steamac_vr_guest.h"
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <chrono>
using namespace vrhost;
int main(){
 int fd[2];REQUIRE(socketpair(AF_UNIX,SOCK_DGRAM,0,fd)==0);
 std::string name="com.thesatchel.tracking-test."+std::to_string(getpid());
 VrLinkServer server(fd[1],name,[](const LinkFrame&){});
 wmtr_vr *bridge=wmtr_vr_create(fd[0],name.c_str());REQUIRE(bridge);
 svr_hello_rep hello{};reinterpret_cast<svr_hello_req&>(hello)={{SVR_HELLO,sizeof(svr_hello_req)},SVR_VERSION,0,{0}};
 CHECK(wmtr_vr_query(bridge,&hello,sizeof hello)==0);CHECK(hello.pad&SVR_CAP_HAND_JOINTS);
 vrl_state state{};state.h={VRL_STATE,sizeof state};state.connected=1;state.refresh_hz=90;REQUIRE(server.send(&state,sizeof state));
 vrl_hand hand{};hand.h={VRL_HAND,sizeof hand};hand.hand=0;hand.active=1;hand.time=hand.received_at=now_ns();
 for(auto &j:hand.joint){j.flags=VRL_POSE_VALID;j.orientation[3]=1;j.position[0]=.123f;}
 REQUIRE(server.send(&hand,sizeof hand));std::this_thread::sleep_for(std::chrono::milliseconds(20));
 auto query=[&](){svr_hand_rep r{};reinterpret_cast<svr_hand_req&>(r)={{SVR_HAND,sizeof(svr_hand_req)},0,0};CHECK(wmtr_vr_query(bridge,&r,sizeof r)==0);return r;};
 auto r=query();CHECK(r.active);CHECK(r.joint[5].position[0]==.123f);
 // No graphics frame is submitted: a fresh independent tracking packet must still reach the guest.
 hand.time=hand.received_at=now_ns();hand.joint[5].position[0]=.456f;REQUIRE(server.send(&hand,sizeof hand));
 std::this_thread::sleep_for(std::chrono::milliseconds(20));r=query();CHECK(r.active);CHECK(r.joint[5].position[0]==.456f);
 std::this_thread::sleep_for(std::chrono::milliseconds(210));r=query();CHECK(!r.active);
 hand.time=hand.received_at=now_ns();REQUIRE(server.send(&hand,sizeof hand));std::this_thread::sleep_for(std::chrono::milliseconds(10));CHECK(query().active);
 state.connected=0;REQUIRE(server.send(&state,sizeof state));std::this_thread::sleep_for(std::chrono::milliseconds(10));CHECK(!query().active);
 state.connected=1;REQUIRE(server.send(&state,sizeof state));
 vrl_body body{};body.h={VRL_BODY,sizeof body};body.time=body.received_at=now_ns();body.valid_mask=0xc3;
 for(auto &pose:body.pose){pose.flags=VRL_POSE_VALID|VRL_POSE_TRACKED;pose.orientation[3]=1;pose.position[1]=1.23f;}
 REQUIRE(server.send(&body,sizeof body));std::this_thread::sleep_for(std::chrono::milliseconds(10));
 svr_body_rep br{};reinterpret_cast<svr_body_req&>(br)={{SVR_BODY,sizeof(svr_body_req)}};
 CHECK(wmtr_vr_query(bridge,&br,sizeof br)==0);CHECK(br.valid_mask==0xc3);CHECK(br.pose[0].position[1]==1.23f);
 std::this_thread::sleep_for(std::chrono::milliseconds(210));
 reinterpret_cast<svr_body_req&>(br)={{SVR_BODY,sizeof(svr_body_req)}};CHECK(wmtr_vr_query(bridge,&br,sizeof br)==0);CHECK(br.valid_mask==0);
 wmtr_vr_destroy(bridge);
 return test_result("tracking_bridge");
}
