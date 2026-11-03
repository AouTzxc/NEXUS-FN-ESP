#pragma once

#include <linux/input.h>
#include <vector>
#include <functional>
#include "VectorStruct.h"

namespace Touch {
    struct touchObj {
        My_Vector2 pos{};
        int id = 0;
        bool isDown = false;
    };

    struct Device {
        int fd;
        float S2TX;
        float S2TY;
        input_absinfo absX, absY;
        touchObj Finger[10];
        bool type_b{};
        bool grabbed{};
        char path[64]{};

        Device() { memset((void *) this, 0, sizeof(*this)); }
    };

    bool Init(const My_Vector2 &s, bool p_readOnly);

    void UpdateImGui();

    void Close();

    void Down(float x, float y);

    void Move(float x, float y);

    void Up();

    void Move(touchObj *touch, float x, float y);

    void Upload();

    void SetCallBack(const std::function<void(std::vector<Device> *)> &cb);

    My_Vector2 Touch2Screen(const My_Vector2 &coord);

    My_Vector2 Screen2Touch(const My_Vector2 &coord);

    bool CanInject();

    // Rewrite the physical look finger already on the grabbed digitizer.
    // Do not inject Finger[9]; Fortnite look follows the original contact.
    struct LookAimCommand {
        bool enabled = false;
        float target_x = 0.f;
        float target_y = 0.f;
        float look_min_x = 0.45f, look_min_y = 0.f, look_max_x = 1.f, look_max_y = 1.f;
        float fire_min_x = 0.68f, fire_min_y = 0.55f, fire_max_x = 1.f, fire_max_y = 1.f;
        float ads_min_x = 0.18f, ads_min_y = 0.30f, ads_max_x = 0.48f, ads_max_y = 0.68f;
        float fov_px = 350.f;
        float smooth = 0.18f;
        float max_step_px = 28.f;
        float deadzone_px = 4.f;
    };
    void SetLookAim(const LookAimCommand& cmd);

    // Returns true when a real device finger (excluding the synthetic AIM
    // finger) is inside the normalized screen rectangle.
    bool HasPhysicalTouchInNormalizedRegion(float min_x, float min_y,
                                             float max_x, float max_y);
    bool HasPhysicalStationaryTouchInNormalizedRegion(float min_x, float min_y,
                                                       float max_x, float max_y,
                                                       float max_motion_px = 18.0F);

    My_Vector2 GetScale();

    void setOrientation(int orientation);

    void setOtherTouch(bool p_otherTouch);
}
