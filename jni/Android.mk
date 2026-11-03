LOCAL_PATH := $(call my-dir)
PROJECT_ROOT := $(LOCAL_PATH)/..
SURFACE_ROOT := $(PROJECT_ROOT)/third_party/surface_imgui/jni

include $(CLEAR_VARS)
LOCAL_MODULE := REBOOT-ESP

LOCAL_CPPFLAGS := -std=c++20 -O2 -fvisibility=hidden -fexceptions -frtti
LOCAL_CPPFLAGS += -DIMGUI_DISABLE_DEBUG_TOOLS -DSURFACE_LOG_TAG=\"REBOOT-ESP\"
LOCAL_CPPFLAGS += -DSURFACE_LOG_LEVEL_ERROR=1 -DSURFACE_LOG_LEVEL_WARN=2 -DSURFACE_LOG_LEVEL_INFO=3 -DSURFACE_LOG_LEVEL_DEBUG=4 -DSURFACE_LOG_LEVEL=2
LOCAL_CFLAGS := -O2 -fvisibility=hidden

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include/Android_Graphics
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include/Android_my_imgui
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include/Android_touch
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include/ImGui
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include/ImGui/backends
LOCAL_C_INCLUDES += $(SURFACE_ROOT)/include/My_Utils

LOCAL_SRC_FILES := src/rebootnova/main.cpp
LOCAL_SRC_FILES += src/rebootnova/embedded_font.cpp
LOCAL_SRC_FILES += src/rebootnova/offset_profile.cpp
LOCAL_SRC_FILES += src/rebootnova/process_memory.cpp
LOCAL_SRC_FILES += src/rebootnova/game_reader.cpp
LOCAL_SRC_FILES += src/rebootnova/feature_engine.cpp
LOCAL_SRC_FILES += src/rebootnova/config_store.cpp
LOCAL_SRC_FILES += src/rebootnova/ui.cpp
LOCAL_SRC_FILES += src/rebootnova/runtime_log.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/Android_touch/TouchHelperA.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/Android_Graphics/OpenGLGraphics.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/Android_my_imgui/AndroidImgui.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/Android_my_imgui/my_imgui.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/Android_my_imgui/my_imgui_impl_android.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/ImGui/imgui.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/ImGui/imgui_draw.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/ImGui/imgui_tables.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/ImGui/imgui_widgets.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/ImGui/backends/imgui_impl_opengl3.cpp
LOCAL_SRC_FILES += ../third_party/surface_imgui/jni/src/My_Utils/stb_image.cpp

LOCAL_LDLIBS := -llog -landroid -ldl -lEGL -lGLESv3 -lz
include $(BUILD_EXECUTABLE)
