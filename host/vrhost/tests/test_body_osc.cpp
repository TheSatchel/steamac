// SPDX-License-Identifier: GPL-3.0-or-later
#include "session/body_osc.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <arpa/inet.h>
#include <cmath>
#include <limits>
#include <string>
#include <cstdio>
using namespace vrhost;
static uint32_t word(const std::vector<uint8_t> &b,size_t p){uint32_t v;std::memcpy(&v,b.data()+p,4);return ntohl(v);}
static float real(const std::vector<uint8_t>&b,size_t p){uint32_t v=word(b,p);float f;std::memcpy(&f,&v,4);return f;}
int main(){
 BodyTracking body;body.confidence=1;body.joint.push_back({.tracker=0,.pose={.position={1,2,3}},.flags=1});
 body.joint.push_back({.tracker=1,.pose={.orientation={0,.70710678f,0,.70710678f},.position={4,5,6}},.flags=1});
 auto b=body_osc_bundle(body);assert(b.size()>16);assert(!std::memcmp(b.data(),"#bundle",7));
 int messages=0;size_t p=16;bool yaw=false;
 while(p<b.size()){
  auto size=word(b,p);p+=4;assert(p+size<=b.size());std::string address((const char*)b.data()+p);
  size_t off=p+((address.size()+1+3)&~size_t(3));assert(!std::memcmp(b.data()+off,",fff",4));off+=8;
  assert(off+12==p+size);
  if(address=="/tracking/trackers/head/position"){assert(real(b,off)==1&&real(b,off+4)==2&&real(b,off+8)==-3);}
  if(address=="/tracking/trackers/1/rotation"){assert(std::abs(real(b,off+4)+90)<.01);yaw=true;}
  assert(address.find("/3/")==std::string::npos&&address.find("/4/")==std::string::npos);p+=size;++messages;
 }
 assert(messages==4&&yaw);
 body.joint[0].pose.position[0]=std::numeric_limits<float>::quiet_NaN();b=body_osc_bundle(body);assert(!b.empty());
 body.confidence=0;assert(body_osc_bundle(body).empty());
 std::puts("PASS: OSC framing, right/left-handed position and rotation, missing legs, invalid confidence");
}
