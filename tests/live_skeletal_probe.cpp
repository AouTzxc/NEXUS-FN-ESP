#include "rebootnova/skeletal_target.h"
namespace {
struct PoseFixture {
    using T=rebootnova::skeletal_target::Transform;
    using A=rebootnova::skeletal_target::BoneArray;
    static constexpr std::uintptr_t actor=0x6000000000ULL,root=actor+0x1000,mesh=actor+0x2000,array=actor+0x4000;
    T component{},bone{};int count{},fail{},mesh_reads{},root_reads{},array_reads{},component_reads{},out_of_bounds{};
    bool operator()(std::uintptr_t address,void* output,std::size_t size){
        const void* source=nullptr;std::uintptr_t ptr{};A arr{array,count,count};T value{};
        if(address==actor+0x2E0&&size==8){if(fail==1)return false;ptr=mesh;if(++mesh_reads>1&&fail==8)ptr+=8;source=&ptr;}
        else if(address==actor+0x1B0&&size==8){ptr=root;if(++root_reads>1&&fail==6)ptr+=8;source=&ptr;}
        else if(address==mesh+0x658&&size==sizeof(A)){if(++array_reads>1&&fail==4)arr.data+=96;source=&arr;}
        else if(address==mesh+0x1E0&&size==sizeof(T)){
            value=component;if(++component_reads>1&&fail==5)value.tx+=20;
            if(fail==3)value.qw=value.qz=0;source=&value;
        }
        else if(address>=array&&size==sizeof(T)){
            auto index=(address-array)/96;
            if(index>=std::size_t(std::max(0,count))){++out_of_bounds;return false;}
            if(fail==2)return false;
            value=bone;if(fail==7||(fail==10&&index==110))value.qw=0;source=&value;
        }
        else return false;
        // Volatile copy keeps the freestanding test fixture independent of libc.
        auto* dst=static_cast<volatile unsigned char*>(output);
        auto* src=static_cast<const unsigned char*>(source);
        for(std::size_t i=0;i<size;++i)dst[i]=src[i];return true;
    }
};
}
extern "C" void rn_live_skeletal_sample(const double* a,double* out){
    PoseFixture f;f.count=int(a[9]);f.fail=int(a[11]);
    const double yaw=a[13]*3.14159265358979323846/180;
    f.component.qw=std::cos(yaw*.5);f.component.qz=std::sin(yaw*.5);
    f.component.tx=a[3];f.component.ty=a[4];f.component.tz=a[5];f.component.sx=f.component.sy=f.component.sz=a[12];
    f.bone.qw=1;f.bone.tx=a[6];f.bone.ty=a[7];f.bone.tz=a[8];
    const rebootnova::skeletal_target::Layout layout{0x2E0,0x1B0,0x658,0x1E0,f.fail==9?48UL:96UL};
    const auto p=rebootnova::skeletal_target::sample(f,PoseFixture::actor,PoseFixture::root,
        {float(a[0]),float(a[1]),float(a[2])},layout,int(a[10]));
    out[0]=p.valid;out[1]=p.point.x;out[2]=p.point.y;out[3]=p.point.z;
    out[4]=p.anchor.x;out[5]=p.anchor.y;out[6]=p.anchor.z;
    out[7]=p.slot;out[8]=p.bone_index;out[9]=p.bone_count;out[10]=p.reads;out[11]=p.bytes;out[12]=f.out_of_bounds;
}
