// SPDX-License-Identifier: Apache-2.0
// OpenXR stage-space joints -> OpenVR's 31-bone, parent-space hand skeleton.
#pragma once
#include <algorithm>
#include <cmath>
#include "skeleton.h"
#include "vrmath.h"

namespace svr {
namespace tracked_hand_detail {
inline Quat inverse(Quat q) { q = qnorm(q); return {-q.x, -q.y, -q.z, q.w}; }
inline Quat rotation(const float q[4]) { return qnorm({q[0], q[1], q[2], q[3]}); }
inline bool valid_joint(const svr_hand_joint &j) {
  if (!(j.flags & SVR_POSE_VALID)) return false;
  float norm = 0;
  for (float v : j.orientation) { if (!std::isfinite(v)) return false; norm += v*v; }
  for (float v : j.position) if (!std::isfinite(v)) return false;
  return norm > 0.25f && norm < 2.25f;
}
inline void relative(Quat parent_q, const float parent_p[3], Quat q, const float p[3], vr::VRBoneTransform_t &out) {
  Quat inv = inverse(parent_q), local = qnorm(qmul(inv, q));
  float v[3] = {p[0]-parent_p[0], p[1]-parent_p[1], p[2]-parent_p[2]}, local_p[3];
  qrot(inv, v, local_p);
  out.position = {{local_p[0], local_p[1], local_p[2], 1}};
  out.orientation = {local.w, local.x, local.y, local.z};
}
}

inline bool tracked_hand_bones(const svr_hand_rep &h, const svr_pose &grip,
                               vr::VRBoneTransform_t out[kHandBoneCount]) {
  using namespace tracked_hand_detail;
  if (!h.active || h.hand > 1 || !(grip.flags & SVR_POSE_VALID)) return false;
  for (const auto &j : h.joint) if (!valid_joint(j)) return false;
  // Bone indices 1..25 match OpenXR's wrist and finger joint indices. Palm (XR 0)
  // is not an OpenVR bone. OpenVR root stays at the actual tracked grip pose.
  const Quat basis = h.hand == 0 ? Quat{-0.70710678f, 0, 0.70710678f, 0}
                                : Quat{0, 0.70710678f, 0, 0.70710678f};
  const Quat to_xr = inverse(basis);
  const Quat wrist_axes = qmul(Quat{0,-0.70710678f,0,0.70710678f},
                               Quat{0.70710678f,0,0,0.70710678f});
  Quat model_q[26];
  for (int i=1; i<26; ++i)
    model_q[i] = qmul(rotation(h.joint[i].orientation), i == 1 ? qmul(wrist_axes, to_xr) : to_xr);
  out[0] = {{{0,0,0,1}}, {1,0,0,0}};
  const Quat grip_q = rotation(grip.orientation);
  relative(grip_q, grip.position, model_q[1], h.joint[1].position, out[1]);
  for (int i=2; i<26; ++i) {
    const int parent = i == 2 || i == 6 || i == 11 || i == 16 || i == 21 ? 1 : i-1;
    relative(model_q[parent], h.joint[parent].position, model_q[i], h.joint[i].position, out[i]);
  }
  // Auxiliary bones are the distal knuckles, directly parented to the grip/root.
  const int distal[5] = {4,9,14,19,24};
  for (int f=0; f<5; ++f) {
    int j=distal[f]; relative(grip_q, grip.position, model_q[j], h.joint[j].position, out[26+f]);
  }
  return true;
}

inline void tracked_hand_summary(const svr_hand_rep &h, vr::VRSkeletalSummaryData_t &out) {
  const int start[5] = {2,6,11,16,21}, end[5] = {5,10,15,20,25};
  for (int f=0; f<5; ++f) {
    float total=0;
    for (int j=start[f]+1; j<end[f]; ++j) {
      float a[3],b[3],aa=0,bb=0,ab=0;
      for(int k=0;k<3;++k) { a[k]=h.joint[j].position[k]-h.joint[j-1].position[k]; b[k]=h.joint[j+1].position[k]-h.joint[j].position[k]; aa+=a[k]*a[k];bb+=b[k]*b[k];ab+=a[k]*b[k]; }
      if(aa>1e-10f && bb>1e-10f) total+=std::acos(std::clamp(ab/std::sqrt(aa*bb),-1.f,1.f));
    }
    out.flFingerCurl[f]=std::clamp(total/3.14159265f,0.f,1.f);
  }
  for (int i=0;i<4;++i) out.flFingerSplay[i]=0.2f;
}
} // namespace svr
