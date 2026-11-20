// SPDX-License-Identifier: Apache-2.0
#include "tracked_hand.h"
#include "hand_filter.h"
#include "wmtr_pose.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>
using namespace svr;
static bool near(float a,float b) {return std::abs(a-b)<0.0001f;}
int main() {
 for(int hand=0;hand<2;hand++) {
  svr_hand_rep h{}; h.active=1;h.hand=hand;
  for(int i=0;i<26;i++) {h.joint[i].flags=SVR_POSE_VALID|SVR_POSE_TRACKED;h.joint[i].orientation[3]=1;}
  h.joint[1].position[1]=1;
  const int start[]={2,6,11,16,21},end[]={5,10,15,20,25};
  for(int f=0;f<5;f++)for(int i=start[f];i<=end[f];i++) {
   h.joint[i].position[0]=float(f)*0.025f;h.joint[i].position[1]=1;
   h.joint[i].position[2]=-0.02f*float(i-start[f]+1);
  }
  svr_pose grip{};grip.flags=SVR_POSE_VALID;grip.orientation[3]=1;
  vr::VRBoneTransform_t local[kHandBoneCount],model[kHandBoneCount];
  assert(tracked_hand_bones(h,grip,local));hand_to_model_space(local,model);
  // Actual measured joint positions must survive coordinate-basis and parent conversion.
  for(int i=1;i<26;i++)for(int k=0;k<3;k++)assert(near(model[i].position.v[k],h.joint[i].position[k]));
  vr::VRSkeletalSummaryData_t summary{};tracked_hand_summary(h,summary);
  for(float curl:summary.flFingerCurl)assert(near(curl,0));
  h.joint[9].position[1]+=0.03f;tracked_hand_summary(h,summary);
  assert(summary.flFingerCurl[1]>0.1f);assert(near(summary.flFingerCurl[2],0));
  h.joint[9].position[1]-=0.03f;
  // Moving the entire person must not change local finger bones.
  auto moved=h;svr_pose moved_grip=grip;Quat q{0,0.38268343f,0,0.92387953f};
  moved_grip.orientation[1]=q.y;moved_grip.orientation[3]=q.w;
  moved_grip.position[0]=3;moved_grip.position[1]=-2;
  for(auto &j:moved.joint){float p[3];qrot(q,j.position,p);for(int k=0;k<3;k++)j.position[k]=p[k]+moved_grip.position[k];j.orientation[1]=q.y;j.orientation[3]=q.w;}
  vr::VRBoneTransform_t transformed[kHandBoneCount];assert(tracked_hand_bones(moved,moved_grip,transformed));
  for(int i=0;i<kHandBoneCount;i++)for(int k=0;k<3;k++)assert(near(local[i].position.v[k],transformed[i].position.v[k]));
  moved.joint[5].position[0]=std::numeric_limits<float>::quiet_NaN();assert(!tracked_hand_bones(moved,moved_grip,transformed));
  h.active=0;assert(!tracked_hand_bones(h,grip,local));
 }
 // Prediction is based on the sample timestamp; re-predicting to the same timestamp must be a no-op.
 svr_pose p{};p.flags=SVR_POSE_VALID|SVR_POSE_VEL;p.orientation[3]=1;p.lin_vel[0]=1;p.ang_vel[1]=1;
 wmtr_predict_pose(&p,1000000000,1050000000);assert(near(p.position[0],.05f));assert(near(p.orientation[1],std::sin(.025f)));
 wmtr_predict_pose(&p,1050000000,1050000000);assert(near(p.position[0],.05f));
 wmtr_predict_pose(&p,1050000000,2050000000);assert(near(p.position[0],.15f));
 HandFilter filter;vr::VRBoneTransform_t bones[kHandBoneCount]{};
 for(auto &bone:bones)bone.orientation.w=1;
 int64_t t=1000000000;filter.apply(t,bones);
 float max_noise=0;
 for(int i=0;i<60;i++){t+=8333333;bones[5].position.v[0]=(i%2 ? -.002f : .002f);filter.apply(t,bones);if(i>10)max_noise=std::max(max_noise,std::abs(bones[5].position.v[0]));}
 assert(max_noise<.001f);
 float unchanged=bones[5].position.v[0];bones[5].position.v[0]=99;filter.apply(t,bones);assert(bones[5].position.v[0]==unchanged);
 for(int i=0;i<8;i++){t+=8333333;bones[5].position.v[0]=.03f;filter.apply(t,bones);}assert(bones[5].position.v[0]>.027f);
 t+=300000000;bones[5].position.v[0]=.1f;filter.apply(t,bones);assert(bones[5].position.v[0]==.1f);
 std::puts("PASS: tracked joints, both hands, rigid-motion invariance, invalid data, timestamp prediction");
}
