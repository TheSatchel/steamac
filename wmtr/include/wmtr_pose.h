/* SPDX-License-Identifier: Apache-2.0 OR GPL-3.0-or-later */
#ifndef WMTR_POSE_H
#define WMTR_POSE_H
#include <math.h>
#include "steamac_vr_guest.h"
/* Predict a fresh cached pose exactly once to the requested display time. */
static inline void wmtr_predict_pose(struct svr_pose *p, int64_t from, int64_t to) {
 if (!(p->flags & SVR_POSE_VEL)) return;
 double dt=(double)(to-from)*1e-9;
 if(dt>0.1) dt=0.1; if(dt<-.1) dt=-.1;
 for(int i=0;i<3;i++) p->position[i]+=(float)(p->lin_vel[i]*dt);
 float x=p->ang_vel[0],y=p->ang_vel[1],z=p->ang_vel[2];
 float rate=sqrtf(x*x+y*y+z*z);if(rate<1e-8f) return;
 float h=(float)(rate*dt*.5),s=sinf(h)/rate,w=cosf(h);x*=s;y*=s;z*=s;
 float a=p->orientation[0],b=p->orientation[1],c=p->orientation[2],d=p->orientation[3];
 p->orientation[0]=w*a+x*d+y*c-z*b; p->orientation[1]=w*b-x*c+y*d+z*a;
 p->orientation[2]=w*c+x*b-y*a+z*d; p->orientation[3]=w*d-x*a-y*b-z*c;
}
#endif
