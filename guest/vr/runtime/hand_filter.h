// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "skeleton.h"
#include "vrmath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace svr {
// Filter only parent-local hand bones. The headset and tracked grip poses are untouched.
class HandFilter {
 public:
  void reset() { time_=0; }
  void apply(int64_t time, vr::VRBoneTransform_t bones[kHandBoneCount]) {
    if(!time_ || time<time_ || time-time_>200000000) {
      std::copy(bones,bones+kHandBoneCount,bones_);time_=time;return;
    }
    if(time==time_) {std::copy(bones_,bones_+kHandBoneCount,bones);return;}
    float dt=std::max(float(time-time_)*1e-9f,0.001f);
    for(int i=1;i<kHandBoneCount;i++) {
      auto &old=bones_[i];auto &next=bones[i];
      float dx=next.position.v[0]-old.position.v[0],dy=next.position.v[1]-old.position.v[1],dz=next.position.v[2]-old.position.v[2];
      float speed=std::sqrt(dx*dx+dy*dy+dz*dz)/dt;
      float position_alpha=1-std::exp(-6.2831853f*((i==1 ? 8.f : 12.f)+speed*12.f)*dt);
      for(int k=0;k<3;k++) old.position.v[k]+=position_alpha*(next.position.v[k]-old.position.v[k]);
      Quat a=qnorm({old.orientation.x,old.orientation.y,old.orientation.z,old.orientation.w});
      Quat b=qnorm({next.orientation.x,next.orientation.y,next.orientation.z,next.orientation.w});
      float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
      if(dot<0) {b={-b.x,-b.y,-b.z,-b.w};dot=-dot;}
      float rate=2*std::acos(std::clamp(dot,0.f,1.f))/dt;
      float alpha=1-std::exp(-6.2831853f*(10.f+2.f*rate)*dt);
      Quat q=qnorm({a.x+alpha*(b.x-a.x),a.y+alpha*(b.y-a.y),a.z+alpha*(b.z-a.z),a.w+alpha*(b.w-a.w)});
      old.orientation={q.w,q.x,q.y,q.z};
      next=old;
    }
    bones_[0]=bones[0];time_=time;
  }
 private:
  int64_t time_=0;
  vr::VRBoneTransform_t bones_[kHandBoneCount]{};
};
}
