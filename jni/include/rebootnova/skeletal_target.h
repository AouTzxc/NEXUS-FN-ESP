#pragma once
#include "rebootnova/types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace rebootnova::skeletal_target {
// [V] Reuse existing CL57566230 profile fields and the existing ESP bone IDs.
// No new guessed offsets. Sampling behavior is device-pending, not offset evidence.
struct Transform {
    double qx{},qy{},qz{},qw{};
    double tx{},ty{},tz{},translation_pad{};
    double sx{1},sy{1},sz{1},scale_pad{};
};
static_assert(sizeof(Transform)==0x60);
struct BoneArray {std::uintptr_t data{};std::int32_t count{},capacity{};};
struct Layout {std::uint64_t mesh{},root{},array{},component{},stride{};};
inline bool pointer(std::uintptr_t p){return p>=0x100000000ULL&&p<0x8000000000ULL&&p%8==0;}
inline bool array_valid(const BoneArray& a){return pointer(a.data)&&a.count>0&&a.count<=512&&a.capacity>=a.count&&a.capacity<=512;}
inline bool transform_valid(const Transform& t){
    const double n=t.qx*t.qx+t.qy*t.qy+t.qz*t.qz+t.qw*t.qw;
    return std::isfinite(n)&&n>=.8&&n<=1.2&&
        std::isfinite(t.tx)&&std::isfinite(t.ty)&&std::isfinite(t.tz)&&
        std::abs(t.tx)<1e8&&std::abs(t.ty)<1e8&&std::abs(t.tz)<1e8&&
        std::isfinite(t.sx)&&std::isfinite(t.sy)&&std::isfinite(t.sz)&&
        std::abs(t.sx)>=.01&&std::abs(t.sx)<=10&&
        std::abs(t.sy)>=.01&&std::abs(t.sy)<=10&&
        std::abs(t.sz)>=.01&&std::abs(t.sz)<=10;
}
inline Vec3 world_point(const Transform& bone,const Transform& component){
    const double x=bone.tx*component.sx,y=bone.ty*component.sy,z=bone.tz*component.sz;
    const double qx=component.qx,qy=component.qy,qz=component.qz,qw=component.qw;
    const double ix=qw*x+qy*z-qz*y,iy=qw*y+qz*x-qx*z,iz=qw*z+qx*y-qy*x,iw=-qx*x-qy*y-qz*z;
    return {float(component.tx+ix*qw-iw*qx-iy*qz+iz*qy),
            float(component.ty+iy*qw-iw*qy-iz*qx+ix*qz),
            float(component.tz+iz*qw-iw*qz-ix*qy+iy*qx)};
}
struct Sample {
    bool valid{};std::uintptr_t mesh{},array{};int slot{-1},bone_index{-1},bone_count{};
    Vec3 point{},anchor{};
    std::size_t reads{},bytes{};
    const char* status{"live bone unavailable"};
};
// Exactly one selected Actor is sampled. No ESP location/bone/screen-box inputs.
// At most four candidate bone records + a consistency reread, <=736 bytes / 13 reads.
template<class Read>
Sample sample(Read&& reader,std::uintptr_t actor,std::uintptr_t expected_root,Vec3 root_position,
              const Layout& layout,int requested){
    Sample out;
    if(!pointer(actor)||!pointer(expected_root)||!root_position.finite()||layout.stride!=sizeof(Transform)||
       !layout.mesh||!layout.root||!layout.array||!layout.component||layout.mesh>0x4000||layout.root>0x4000||
       layout.array>0x4000||layout.component>0x4000){out.status="live bone layout invalid";return out;}
    const auto read=[&](std::uintptr_t address,auto& value){
        ++out.reads;out.bytes+=sizeof(value);return reader(address,&value,sizeof(value));
    };
    std::uintptr_t mesh{},root{};BoneArray array{};Transform component{};
    if(!read(actor+layout.mesh,mesh)||!pointer(mesh)||!read(actor+layout.root,root)||root!=expected_root){
        out.status="live mesh/root unavailable";return out;
    }
    if(!read(mesh+layout.array,array)||!array_valid(array)){out.status="live bone array invalid";return out;}
    if(!read(mesh+layout.component,component)||!transform_valid(component)){out.status="live mesh transform invalid";return out;}
    // Head=110 may be absent in a 109-entry mesh. Bounds are checked per bone,
    // not by rejecting the entire skeleton for having fewer than 111 entries.
    constexpr int ids[]={110,67,66,2};requested=std::clamp(requested,0,3);
    const int order[]={requested,1,2,3};
    Transform bone{};Vec3 point{};
    for(int i=0;i<4;++i){
        const int slot=order[i];bool duplicate=false;
        for(int j=0;j<i;++j)duplicate|=order[j]==slot;
        if(duplicate||ids[slot]>=array.count)continue;
        Transform candidate{};
        if(!read(array.data+std::uintptr_t(ids[slot])*layout.stride,candidate)||!transform_valid(candidate))continue;
        const Vec3 world=world_point(candidate,component);
        if(!world.finite()||(world-root_position).length()>500)continue;
        bone=candidate;point=world;out.slot=slot;out.bone_index=ids[slot];break;
    }
    if(out.slot<0){out.status="no readable live bone";return out;}
    std::uintptr_t mesh_after{},root_after{};BoneArray array_after{};Transform component_after{},bone_after{};
    if(!read(actor+layout.mesh,mesh_after)||mesh_after!=mesh||!read(actor+layout.root,root_after)||root_after!=root||
       !read(mesh+layout.array,array_after)||array_after.data!=array.data||array_after.count!=array.count||array_after.capacity!=array.capacity){
        out.status="live skeleton identity changed";return out;
    }
    if(!read(mesh+layout.component,component_after)||!transform_valid(component_after)||
       !read(array.data+std::uintptr_t(out.bone_index)*layout.stride,bone_after)||!transform_valid(bone_after)){
        out.status="live pose reread failed";return out;
    }
    const Vec3 final_point=world_point(bone_after,component_after);
    // A loose coherence check, not an atomic engine snapshot. Large changes
    // suspend this publication instead of combining unrelated pose generations.
    if(!final_point.finite()||(final_point-point).length()>8||(final_point-root_position).length()>500){
        out.status="live pose changed during read";return out;
    }
    out.valid=true;out.mesh=mesh;out.array=array.data;out.bone_count=array.count;out.point=final_point;
    out.anchor={float(component_after.tx),float(component_after.ty),float(component_after.tz)};
    out.status="live bone";return out;
}
}
