#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rebootnova {

struct Vec2 {
    float x{};
    float y{};
};

struct Vec3 {
    float x{};
    float y{};
    float z{};

    Vec3 operator+(const Vec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    Vec3 operator-(const Vec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

// UE5 Large World Coordinates store FVector as three doubles (24 bytes).
struct Vec3d {
    double x{};
    double y{};
    double z{};
    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    Vec3 narrow() const { return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}; }
};

struct Rotator {
    float pitch{};
    float yaw{};
    float roll{};
};

// The donor copies 0x34 bytes from CameraManager + 0x29D0. Keep the raw bytes
// as evidence-compatible storage and decode only the fields independently used.
struct CameraCacheRaw {
    std::array<std::uint8_t, 0x34> bytes{};
};

struct CameraView {
    Vec3 location{};
    Rotator rotation{};
    float fov{80.0F};
    bool valid{};
};

inline float degrees(float radians) { return radians * 57.29577951308232F; }
inline float radians(float degrees_value) { return degrees_value * 0.017453292519943295F; }
inline constexpr float kReferenceAspect = 16.0F / 9.0F;

inline bool world_to_screen(const Vec3& world, const CameraView& camera, float width, float height, Vec2& out,
                            bool vertical_fov = false) {
    const float pitch = radians(camera.rotation.pitch);
    const float yaw = radians(camera.rotation.yaw);
    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    const Vec3 axis_x{cp * cy, cp * sy, sp};
    const Vec3 axis_y{-sy, cy, 0.0F};
    const Vec3 axis_z{-sp * cy, -sp * sy, cp};
    const Vec3 delta = world - camera.location;
    const Vec3 transformed{
        delta.x * axis_y.x + delta.y * axis_y.y + delta.z * axis_y.z,
        delta.x * axis_z.x + delta.y * axis_z.y + delta.z * axis_z.z,
        delta.x * axis_x.x + delta.y * axis_x.y + delta.z * axis_x.z,
    };
    if (transformed.z < 1.0F || !std::isfinite(camera.fov) || camera.fov < 10.0F || camera.fov > 170.0F) return false;
    const float center_x = width * 0.5F;
    const float center_y = height * 0.5F;
    const float horizontal_focal_basis = std::min(width, height * kReferenceAspect);
    const float focal_basis = vertical_fov ? height : horizontal_focal_basis;
    const float focal = focal_basis * 0.5F / std::tan(radians(camera.fov) * 0.5F);
    out = {center_x + transformed.x * focal / transformed.z, center_y - transformed.y * focal / transformed.z};
    return std::isfinite(out.x) && std::isfinite(out.y) && out.x > -width && out.x < width * 2.0F && out.y > -height && out.y < height * 2.0F;
}

// Prepared once per render pass instead of recomputing trig per bone/segment.
struct Projection {
    Vec3 origin{},axis_x{},axis_y{},axis_z{};
    float width{},height{},focal{};
    bool valid{};
    Projection(const CameraView& c,float w,float h,bool vertical=false):origin(c.location),width(w),height(h){
        const float pitch=radians(c.rotation.pitch),yaw=radians(c.rotation.yaw);
        const float sp=std::sin(pitch),cp=std::cos(pitch),sy=std::sin(yaw),cy=std::cos(yaw);
        axis_x={cp*cy,cp*sy,sp};axis_y={-sy,cy,0};axis_z={-sp*cy,-sp*sy,cp};
        valid=std::isfinite(c.fov)&&c.fov>=10&&c.fov<=170;
        if(valid)focal=(vertical?h:std::min(w,h*kReferenceAspect))*.5F/std::tan(radians(c.fov)*.5F);
    }
    bool project(const Vec3& world,Vec2& out) const {
        const Vec3 d=world-origin;
        const float z=d.x*axis_x.x+d.y*axis_x.y+d.z*axis_x.z;
        if(!valid||z<1)return false;
        const float x=d.x*axis_y.x+d.y*axis_y.y+d.z*axis_y.z;
        const float y=d.x*axis_z.x+d.y*axis_z.y+d.z*axis_z.z;
        out={width*.5F+x*focal/z,height*.5F-y*focal/z};
        return std::isfinite(out.x)&&std::isfinite(out.y)&&out.x> -width&&out.x<width*2&&out.y> -height&&out.y<height*2;
    }
};

}  // namespace rebootnova
