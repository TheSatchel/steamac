// SPDX-License-Identifier: Apache-2.0
#include <dlfcn.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>
#include "openvr.h"
class Core {
public:
 virtual vr::EVRInitError Init(vr::EVRApplicationType,const char*)=0;
 virtual void Cleanup()=0;
 virtual vr::EVRInitError IsInterfaceVersionValid(const char*)=0;
 virtual void *GetGenericInterface(const char*,vr::EVRInitError*)=0;
 virtual bool BIsHmdPresent()=0;
 virtual const char *GetEnglishStringForHmdError(vr::EVRInitError)=0;
 virtual const char *GetIDForVRInitError(vr::EVRInitError)=0;
};
int main(int argc,char**argv){
 assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
 auto factory=(void*(*)(const char*,int*))dlsym(lib,"VRClientCoreFactory");assert(factory);
 int error=0;auto *core=(Core*)factory("IVRClientCore_003",&error);assert(core);
 assert(core->Init(vr::VRApplication_Scene,nullptr)==vr::VRInitError_None);
 vr::EVRInitError e;auto *system=(vr::IVRSystem*)core->GetGenericInterface(vr::IVRSystem_Version,&e);assert(system);
 vr::TrackedDevicePose_t poses[64]{};system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,64);
 vr::TrackedDeviceIndex_t ids[8]{};assert(system->GetSortedTrackedDeviceIndicesOfClass(vr::TrackedDeviceClass_GenericTracker,ids,8,vr::k_unTrackedDeviceIndexInvalid)==4);
 assert(system->GetTrackedDeviceClass(3)==vr::TrackedDeviceClass_GenericTracker);
 assert(poses[3].bPoseIsValid && poses[3].eTrackingResult==vr::TrackingResult_Running_OK);
 assert(poses[3].mDeviceToAbsoluteTracking.m[0][3]==1.f);
 assert(!poses[5].bDeviceIsConnected); // upper-body data must not invent feet
 char serial[80]{};vr::ETrackedPropertyError prop;
 system->GetStringTrackedDeviceProperty(3,vr::Prop_SerialNumber_String,serial,sizeof serial,&prop);
 assert(std::string(serial)=="satchel-body-1");core->Cleanup();dlclose(lib);
 puts("PASS: real runtime exposes valid native body trackers without invented feet");
}
