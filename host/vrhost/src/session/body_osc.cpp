// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/body_osc.h"
#include "session/clock.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
namespace vrhost {
namespace {
void string(std::vector<uint8_t> &out, const std::string &s) {
 out.insert(out.end(), s.begin(), s.end()); out.push_back(0);
 while(out.size()%4) out.push_back(0);
}
void word(std::vector<uint8_t> &out, uint32_t n) { n=htonl(n); const auto *p=reinterpret_cast<uint8_t *>(&n); out.insert(out.end(),p,p+4); }
void real(std::vector<uint8_t> &out,float f) { uint32_t n; std::memcpy(&n,&f,4); word(out,n); }
void message(std::vector<uint8_t> &bundle, const std::string &address, const std::array<float,3> &v) {
 std::vector<uint8_t> out; string(out,address); string(out,",fff"); for(float f:v) real(out,f);
 word(bundle,uint32_t(out.size())); bundle.insert(bundle.end(),out.begin(),out.end());
}
bool valid(const BodyJoint &j) {
 if(!(j.flags&1) || j.tracker>8) return false;
 float n=0; for(float v:j.pose.orientation) { if(!std::isfinite(v)) return false; n+=v*v; }
 for(float v:j.pose.position) if(!std::isfinite(v)) return false;
 return n>0.25f && n<2.25f;
}
std::array<float,3> unity_angles(const std::array<float,4> &q) {
 // Reflection across Z converts OpenXR to Unity: q' = (-x,-y,z,w).
 float x=-q[0],y=-q[1],z=q[2],w=q[3];
 float n=std::sqrt(x*x+y*y+z*z+w*w);x/=n;y/=n;z/=n;w/=n;
 float m02=2*(x*z+w*y),m12=2*(y*z-w*x),m22=1-2*(x*x+y*y);
 float m10=2*(x*y+w*z),m11=1-2*(x*x+z*z);
 float ax=std::asin(std::clamp(-m12,-1.f,1.f)),ay,az;
 if(std::abs(std::cos(ax))>1e-5f) { ay=std::atan2(m02,m22);az=std::atan2(m10,m11); }
 else { ay=std::atan2(-2*(x*z-w*y),1-2*(y*y+z*z));az=0; }
 constexpr float degrees=57.2957795131f;return {ax*degrees,ay*degrees,az*degrees};
}
}
std::vector<uint8_t> body_osc_bundle(const BodyTracking &body) {
 if(!std::isfinite(body.confidence) || body.confidence<=0) return {};
 std::vector<uint8_t> out; string(out,"#bundle");word(out,0);word(out,1); // immediate OSC timetag
 size_t count=0;
 for(const auto &j:body.joint) {
  if(!valid(j)) continue;
  const std::string base="/tracking/trackers/"+(j.tracker ? std::to_string(j.tracker) : "head");
  message(out,base+"/position",{j.pose.position[0],j.pose.position[1],-j.pose.position[2]});
  message(out,base+"/rotation",unity_angles(j.pose.orientation));++count;
 }
 return count ? out : std::vector<uint8_t>{};
}
BodyOsc::BodyOsc(uint16_t p):port(p) { if(p) fd=socket(AF_INET,SOCK_DGRAM,0); }
BodyOsc::~BodyOsc() { if(fd>=0) close(fd); }
bool BodyOsc::send(const BodyTracking &b) {
 ns now=now_ns();
 if(fd<0 || b.received_at<=0 || now<b.received_at || now-b.received_at>200'000'000 || b.time<=last_time) return false;
 auto packet=body_osc_bundle(b);if(packet.empty()) return false;
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 auto n=sendto(fd,packet.data(),packet.size(),MSG_DONTWAIT,reinterpret_cast<sockaddr *>(&address),sizeof(address));
 if(n!=ssize_t(packet.size())) return false;
 last_time=b.time;++packets;return true;
}
}
