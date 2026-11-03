#include <dirent.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>
#include <unordered_map>
#include <linux/input.h>
#include <linux/uinput.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include "spinlock.h"

#include "imgui.h"
#include "TouchHelperA.h"
#include "Utils.h"

#define maxE 5
#define maxF 10
#define UNGRAB 0
#define GRAB 1

//TODO 触摸穿透

namespace Touch {
    static struct {
        input_event downEvent[2]{{{}, EV_KEY, BTN_TOUCH,       1}, {{}, EV_KEY, BTN_TOOL_FINGER, 1}};
        input_event event[512]{0};
    } input;

    static My_Vector2 touch_scale;

    static My_Vector2 screenSize;

    static std::vector<Device> devices;

    static int nowfd;

    static int orientation = 0;

    static std::atomic_bool initialized{false};

    static bool readOnly = false;

    static bool otherTouch = false;

    static std::function<void(std::vector<Device> *)> callback;

    static std::vector<pthread_t> readerThreads;

    static spinlock lock;

    struct PendingImGuiEvent {
        My_Vector2 pos{};
        bool hasPosition = false;
        bool hasButton = false;
        bool down = false;
    };

    static std::vector<PendingImGuiEvent> pendingImGuiEvents;
    static My_Vector2 lastPointerPos{};
    static bool lastPointerDown = false;
    static std::unordered_map<int, My_Vector2> physicalTouchStarts;
    static std::unordered_map<int, bool> physicalTouchMoved;
    static std::unordered_map<int, std::chrono::steady_clock::time_point> physicalTouchStartedAt;
    static LookAimCommand lookAim{};
    static char selfUinputName[16]{};
    static char selfEventPath[64]{};
    static std::chrono::steady_clock::time_point nextTouchHotplug{};


    static bool in_norm_rect(float nx, float ny, float x0, float y0, float x1, float y1) {
        const float minx = std::fmin(x0, x1), maxx = std::fmax(x0, x1);
        const float miny = std::fmin(y0, y1), maxy = std::fmax(y0, y1);
        return nx >= minx && nx <= maxx && ny >= miny && ny <= maxy;
    }

    static void applyLookAim() {
        if (!lookAim.enabled || devices.empty()) return;
        const float width = screenSize.x;
        const float height = screenSize.y;
        if (width <= 1.f || height <= 1.f) return;
        const float ex = lookAim.target_x - width * 0.5f;
        const float ey = lookAim.target_y - height * 0.5f;
        const float dist = std::hypot(ex, ey);
        if (!(dist > lookAim.deadzone_px) || dist > lookAim.fov_px) return;
        const float step = std::fmin(lookAim.max_step_px, dist * lookAim.smooth);
        const float dx = ex / dist * step;
        const float dy = ey / dist * step;
        for (auto &device: devices) {
            if (!device.grabbed) continue;
            for (int index = 0; index < 9; ++index) {
                auto &finger = device.Finger[index];
                if (!finger.isDown) continue;
                const My_Vector2 screen = Touch2Screen(finger.pos);
                const float nx = screen.x / width;
                const float ny = screen.y / height;
                if (in_norm_rect(nx, ny, lookAim.fire_min_x, lookAim.fire_min_y,
                                 lookAim.fire_max_x, lookAim.fire_max_y))
                    continue;
                if (in_norm_rect(nx, ny, lookAim.ads_min_x, lookAim.ads_min_y,
                                 lookAim.ads_max_x, lookAim.ads_max_y))
                    continue;
                if (!in_norm_rect(nx, ny, lookAim.look_min_x, lookAim.look_min_y,
                                  lookAim.look_max_x, lookAim.look_max_y))
                    continue;
                finger.pos = Screen2Touch({screen.x + dx, screen.y + dy});
                return;
            }
        }
    }

    static void resetPhysicalTouchTracking() {
        physicalTouchStarts.clear();
        physicalTouchMoved.clear();
        physicalTouchStartedAt.clear();
    }

    void Upload() {
        static bool isFirstDown = true;
        int tmpCnt = 0, tmpCnt2 = 0;
        for (auto &device: devices) {
            for (auto &finger: device.Finger) {
                if (finger.isDown) {
                    if (tmpCnt2++ > 20) {
                        goto finish;
                    }
                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_X;
                    input.event[tmpCnt].value = (int) finger.pos.x;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_Y;
                    input.event[tmpCnt].value = (int) finger.pos.y;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_MT_POSITION_X;
                    input.event[tmpCnt].value = (int) finger.pos.x;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_MT_POSITION_Y;
                    input.event[tmpCnt].value = (int) finger.pos.y;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_MT_TRACKING_ID;
                    input.event[tmpCnt].value = finger.id;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_SYN;
                    input.event[tmpCnt].code = SYN_MT_REPORT;
                    input.event[tmpCnt].value = 0;
                    tmpCnt++;
                }
            }
        }
        finish:
        bool is = false;
        if (tmpCnt == 0) {
            input.event[tmpCnt].type = EV_SYN;
            input.event[tmpCnt].code = SYN_MT_REPORT;
            input.event[tmpCnt].value = 0;
            tmpCnt++;
            if (!isFirstDown) {
                isFirstDown = true;
                input.event[tmpCnt].type = EV_KEY;
                input.event[tmpCnt].code = BTN_TOUCH;
                input.event[tmpCnt].value = 0;
                tmpCnt++;
                input.event[tmpCnt].type = EV_KEY;
                input.event[tmpCnt].code = BTN_TOOL_FINGER;
                input.event[tmpCnt].value = 0;
                tmpCnt++;
            }
        } else {
            is = true;
        }
        input.event[tmpCnt].type = EV_SYN;
        input.event[tmpCnt].code = SYN_REPORT;
        input.event[tmpCnt].value = 0;
        tmpCnt++;

        if (is && isFirstDown) {
            isFirstDown = false;
            write(nowfd, &input, sizeof(struct input_event) * (tmpCnt + 2));
        } else {
            write(nowfd, input.event, sizeof(struct input_event) * tmpCnt);
        }
    }


    /*void *TypeB(void *arg) {
        int i = (int) (long) arg;
        Device &device = devices[i];
        int latest = 0;
        input_event inputEvent[64]{0};

        while (Touch_initialized) {
            auto readSize = (int32_t) read(origfd[i], inputEvent, sizeof(inputEvent));
            if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
                continue;
            }
            size_t count = size_t(readSize) / sizeof(input_event);
            for (size_t j = 0; j < count; j++) {
                input_event &ie = inputEvent[j];
                if (latest < 0)
                    latest = 0;
                if (latest >= 10)
                    continue;
                if (ie.code == ABS_MT_TRACKING_ID) {
                    if (ie.value < 0) {
                        Finger[i][latest].isDown = false;
                    } else {
                        Finger[i][latest].isDown = true;
                    }
                    Finger[i][latest].id = (i * 2 + 1) * maxF + ie.value;
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_X) {
                    Finger[i][latest].isDown = true;
                    Finger[i][latest].x = (int) (ie.value * S2TX);
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_Y) {
                    Finger[i][latest].isDown = true;
                    Finger[i][latest].y = (int) (ie.value * S2TY);
                    continue;
                }
                if (ie.code == SYN_MT_REPORT) {
                    latest += 1;
                    continue;
                }
                if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
                    Upload();
                    memset(&Finger[i][0], 0, sizeof(Finger) * 10);
                    latest = -1;
                    continue;
                }
            }
        }
        return nullptr;
    }*/

    static void *TypeA(void *arg) {
        int i = (int) (long) arg;
        Device &device = devices[i];

        int latest = 0;
        input_event inputEvent[64]{0};

        while (initialized.load(std::memory_order_acquire)) {
            auto readSize = (int32_t) read(device.fd, inputEvent, sizeof(inputEvent));
            if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
                usleep(1000);
                continue;
            }
            size_t count = size_t(readSize) / sizeof(input_event);

            lock.lock();
            for (size_t j = 0; j < count; j++) {
                input_event &ie = inputEvent[j];
                if (ie.type == EV_ABS) {
                    if (ie.code == ABS_MT_SLOT) {
                        latest = (ie.value >= 0 && ie.value < 10) ? ie.value : -1;
                        continue;
                    }
                    if (latest < 0 || latest >= 10) continue;
                    if (ie.code == ABS_MT_TRACKING_ID) {
                        if (ie.value == -1) {
                            device.Finger[latest].isDown = false;
                        } else {
                            device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
                            device.Finger[latest].isDown = true;
                        }
                        continue;
                    }
                    if (ie.code == ABS_MT_POSITION_X) {
                        device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
                        device.Finger[latest].pos.x = (float) ie.value * device.S2TX;
                        if (!device.type_b) device.Finger[latest].isDown = true;
                        continue;
                    }
                    if (ie.code == ABS_MT_POSITION_Y) {
                        device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
                        device.Finger[latest].pos.y = (float) ie.value * device.S2TY;
                        if (!device.type_b) device.Finger[latest].isDown = true;
                        continue;
                    }
                }
                if (!device.type_b && ie.type == EV_SYN && ie.code == SYN_MT_REPORT) {
                    if (device.Finger[latest].isDown && latest < 9) latest += 1;
                    continue;
                }
                if (ie.type == EV_KEY && (ie.code == BTN_TOUCH || ie.code == BTN_TOOL_FINGER) &&
                    ie.value == 0 && !device.type_b) {
                    for (auto &finger: device.Finger) finger.isDown = false;
                    latest = 0;
                    continue;
                }
                if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
                    if (!device.type_b) {
                        const int clear_from = (latest < 9 && device.Finger[latest].isDown)
                            ? latest + 1 : latest;
                        for (int index = clear_from; index < 9; ++index)
                            device.Finger[index].isDown = false;
                        latest = 0;
                    }
                    bool pointerDown = false;
                    My_Vector2 pointerPos = lastPointerPos;
                    for (const auto &candidateDevice: devices) {
                        for (const auto &finger: candidateDevice.Finger) {
                            if (finger.isDown) {
                                pointerDown = true;
                                pointerPos = Touch2Screen(finger.pos);
                                break;
                            }
                        }
                        if (pointerDown) break;
                    }

                    PendingImGuiEvent imguiEvent{};
                    if (pointerDown) {
                        const bool moved = !lastPointerDown
                                           || std::fabs(pointerPos.x - lastPointerPos.x) >= 0.5F
                                           || std::fabs(pointerPos.y - lastPointerPos.y) >= 0.5F;
                        imguiEvent.pos = pointerPos;
                        imguiEvent.hasPosition = moved;
                    }
                    if (pointerDown != lastPointerDown) {
                        imguiEvent.hasButton = true;
                        imguiEvent.down = pointerDown;
                    }
                    if (imguiEvent.hasPosition || imguiEvent.hasButton) {
                        pendingImGuiEvents.push_back(imguiEvent);
                    }
                    lastPointerPos = pointerPos;
                    lastPointerDown = pointerDown;

                    if (!readOnly) {
                        applyLookAim();
                        if (callback) {
                            callback(&devices);
                        } else {
                            Upload();
                        }
                    }
                    continue;
                }
            }
            lock.unlock();

        }
        return nullptr;
    }

    static bool checkDeviceIsTouch(int fd, bool *type_b_out);
    static bool isSelfUinputName(const char *name) {
        return selfUinputName[0] && name && std::strcmp(name, selfUinputName) == 0;
    }
    static void rememberSelfEventPath() {
        selfEventPath[0] = 0;
        if (!selfUinputName[0]) return;
        DIR *dir = opendir("/dev/input/");
        if (!dir) return;
        while (dirent *ptr = readdir(dir)) {
            if (strncmp(ptr->d_name, "event", 5) != 0) continue;
            char path[64]{};
            std::snprintf(path, sizeof(path), "/dev/input/%s", ptr->d_name);
            int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            char name[128]{};
            const int ok = ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
            close(fd);
            if (ok >= 0 && isSelfUinputName(name)) {
                std::snprintf(selfEventPath, sizeof(selfEventPath), "%s", path);
                break;
            }
        }
        closedir(dir);
    }
    static void hotplugTouchDevices() {
        if (!initialized.load(std::memory_order_acquire) || devices.empty()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextTouchHotplug) return;
        nextTouchHotplug = now + std::chrono::seconds(2);
        DIR *dir = opendir("/dev/input/");
        if (!dir) return;
        std::vector<std::string> eventPaths;
        while (dirent *ptr = readdir(dir)) {
            if (strncmp(ptr->d_name, "event", 5) == 0)
                eventPaths.emplace_back(std::string("/dev/input/") + ptr->d_name);
        }
        closedir(dir);
        std::sort(eventPaths.begin(), eventPaths.end());
        const int screenX = devices[0].absX.maximum;
        const int screenY = devices[0].absY.maximum;
        if (screenX <= 0 || screenY <= 0) return;
        for (const auto &path : eventPaths) {
            if (devices.size() >= 16) break;
            if (selfEventPath[0] && path == selfEventPath) continue;
            bool known = false;
            for (const auto &device : devices) {
                if (std::strncmp(device.path, path.c_str(), sizeof(device.path)) == 0) {
                    known = true;
                    break;
                }
            }
            if (known) continue;
            // Listen only. Never grab hotplugged nodes: V24 grabbed our own
            // uinput (cloned I2C bustype) and the system lost all touch.
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            char name[128]{};
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 && isSelfUinputName(name)) {
                std::snprintf(selfEventPath, sizeof(selfEventPath), "%s", path.c_str());
                close(fd);
                continue;
            }
            bool type_b = false;
            if (!checkDeviceIsTouch(fd, &type_b)) {
                close(fd);
                continue;
            }
            Device device{};
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &device.absX) != 0 ||
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &device.absY) != 0 ||
                device.absX.maximum <= 0 || device.absY.maximum <= 0) {
                close(fd);
                continue;
            }
            device.fd = fd;
            device.type_b = type_b;
            device.grabbed = false;
            std::snprintf(device.path, sizeof(device.path), "%s", path.c_str());
            device.S2TX = (float) screenX / (float) device.absX.maximum;
            device.S2TY = (float) screenY / (float) device.absY.maximum;
            lock.lock();
            const int index = static_cast<int>(devices.size());
            devices.push_back(device);
            lock.unlock();
            pthread_t thread{};
            if (pthread_create(&thread, nullptr, TypeA, (void *) (long) index) == 0)
                readerThreads.push_back(thread);
        }
    }

    void UpdateImGui() {
        hotplugTouchDevices();
        std::vector<PendingImGuiEvent> events;
        lock.lock();
        events.swap(pendingImGuiEvents);
        lock.unlock();
        if (events.empty() || ImGui::GetCurrentContext() == nullptr) return;

        ImGuiIO &io = ImGui::GetIO();
        for (const auto &event: events) {
            if (event.hasPosition) io.AddMousePosEvent(event.pos.x, event.pos.y);
            if (event.hasButton) io.AddMouseButtonEvent(0, event.down);
        }
    }

    static bool checkDeviceIsTouch(int fd, bool *type_b_out) {
        uint8_t *bits = NULL;
        ssize_t bits_size = 0;
        int res, j, k;
        bool has_slot = false, has_x = false, has_y = false;
        struct input_absinfo abs{};
        while (true) {
            res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
            if (res < bits_size)
                break;
            bits_size = res + 16;
            bits = (uint8_t *) realloc(bits, bits_size * 2);
        }
        for (j = 0; j < res; j++) {
            for (k = 0; k < 8; k++)
                if (bits[j] & 1 << k && ioctl(fd, EVIOCGABS(j * 8 + k), &abs) == 0) {
                    if (j * 8 + k == ABS_MT_SLOT) {
                        has_slot = true;
                        continue;
                    }
                    if (j * 8 + k == ABS_MT_POSITION_X) {
                        has_x = true;
                        continue;
                    }
                    if (j * 8 + k == ABS_MT_POSITION_Y) {
                        has_y = true;
                        continue;
                    }
                }
        }
        free(bits);
        if (type_b_out) *type_b_out = has_slot;
        // Type-B screens expose ABS_MT_SLOT. Type-A converter mapped contacts
        // only expose ABS_MT_POSITION_X/Y (or SYN_MT_REPORT packets).
        return has_x && has_y;
    }

    bool Init(const My_Vector2 &s, bool p_readOnly) {
        Close();
        devices.clear();
        devices.reserve(16);
        My_Vector2 size = s;
        readOnly = p_readOnly;
        const auto cleanupPartialInit = [&]() {
            for (auto &device: devices) {
                if (!readOnly) ioctl(device.fd, EVIOCGRAB, UNGRAB);
                close(device.fd);
                device.fd = 0;
            }
            devices.clear();
            if (nowfd > 0) {
                close(nowfd);
                nowfd = 0;
            }
        };
        if (size.x > size.y) {
            screenSize = size;
        } else {
            screenSize = {size.y, size.x};
        }
        DIR *dir = opendir("/dev/input/");
        if (!dir) {
            return false;
        }

        dirent *ptr = NULL;
        std::vector<std::string> eventPaths;
        while ((ptr = readdir(dir)) != NULL) {
            if (strncmp(ptr->d_name, "event", 5) == 0)
                eventPaths.push_back(std::string("/dev/input/") + ptr->d_name);
        }
        closedir(dir);
        std::sort(eventPaths.begin(), eventPaths.end());
        for (const auto& path : eventPaths) {
            int fd = open(path.c_str(), (readOnly ? O_RDONLY : O_RDWR) | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                continue;
            }
            bool type_b = false;
            if (checkDeviceIsTouch(fd, &type_b)) {
                Device device{};
                if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &device.absX) == 0
                    && ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &device.absY) == 0) {
                    input_id id{};
                    ioctl(fd, EVIOCGID, &id);
                    device.fd = fd;
                    device.type_b = type_b;
                    std::snprintf(device.path, sizeof(device.path), "%s", path.c_str());
                    // BUS_USB/BUS_BLUETOOTH converters already own the HID node
                    // after they activate. Grabbing them swallows mapped fire/ADS.
                    // Only grab the internal digitizer (I2C/SPI/I3C).
                    const bool converter_bus = id.bustype == BUS_USB || id.bustype == BUS_BLUETOOTH;
                    device.grabbed = !readOnly && !converter_bus;
                    if (device.grabbed) {
                        ioctl(fd, EVIOCGRAB, GRAB);
                    }
                    devices.push_back(device);
                } else { close(fd); }
            } else {
                close(fd);
            }
        }

        if (devices.empty()) {
            puts("获取屏幕驱动失败");
            return false;
        }
        //LOGD("device count: %zu", devices.size());

        int screenX = devices[0].absX.maximum;
        int screenY = devices[0].absY.maximum;

        if (!readOnly) {
            struct uinput_user_dev ui_dev;
            nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
            if (nowfd <= 0) {
                cleanupPartialInit();
                return false;
            }

            int string_len = rand() % 10 + 5;
            // string_len is 5..14. Use a fixed buffer to keep standard C++20
            // compatibility instead of relying on Clang's VLA extension.
            char string[16]{};
            memset(&ui_dev, 0, sizeof(ui_dev));

            genRandomString(string, string_len);
            strncpy(ui_dev.name, string, UINPUT_MAX_NAME_SIZE);
            std::memset(selfUinputName, 0, sizeof(selfUinputName));
            std::strncpy(selfUinputName, string, sizeof(selfUinputName) - 1);

            ui_dev.id.bustype = 0;
            ui_dev.id.vendor = rand() % 10 + 5;
            ui_dev.id.product = rand() % 10 + 5;
            ui_dev.id.version = rand() % 10 + 5;

            ioctl(nowfd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

            ioctl(nowfd, UI_SET_EVBIT, EV_ABS);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
            ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
            ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
            ioctl(nowfd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
            ioctl(nowfd, UI_SET_KEYBIT, BTN_TOUCH);

            genRandomString(string, string_len);
            ioctl(nowfd, UI_SET_PHYS, string);

            int fd = devices[0].fd;
            {
                struct input_id id{};
                if (ioctl(fd, EVIOCGID, &id) == 0) {
                    ui_dev.id.bustype = id.bustype;
                    ui_dev.id.vendor = id.vendor;
                    ui_dev.id.product = id.product;
                    ui_dev.id.version = id.version;
                }
                uint8_t *bits = NULL;
                ssize_t bits_size = 0;
                int res, j, k;
                while (1) {
                    res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
                    if (res < bits_size)
                        break;
                    bits_size = res + 16;
                    bits = (uint8_t *) realloc(bits, bits_size * 2);
                }
                for (j = 0; j < res; j++) {
                    for (k = 0; k < 8; k++)
                        if (bits[j] & 1 << k) {
                            if (j * 8 + k == BTN_TOUCH || j * 8 + k == BTN_TOOL_FINGER)
                                continue;
                            ioctl(nowfd, UI_SET_KEYBIT, j * 8 + k);
                        }
                }
                free(bits);
            }
            ui_dev.absmin[ABS_MT_POSITION_X] = 0;
            ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
            ui_dev.absmin[ABS_MT_POSITION_Y] = 0;
            ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;
            ui_dev.absmin[ABS_X] = 0;
            ui_dev.absmax[ABS_X] = screenX;
            ui_dev.absmin[ABS_Y] = 0;
            ui_dev.absmax[ABS_Y] = screenY;
            ui_dev.absmin[ABS_MT_TRACKING_ID] = 0;
            ui_dev.absmax[ABS_MT_TRACKING_ID] = 65535;
            write(nowfd, &ui_dev, sizeof(ui_dev));

            if (ioctl(nowfd, UI_DEV_CREATE)) {
                cleanupPartialInit();
                return false;
            }
            rememberSelfEventPath();
        }
        initialized.store(true, std::memory_order_release);
        if (size.x > size.y) {
            std::swap(size.x, size.y);
        }
        if (otherTouch) {
            std::swap(size.x, size.y);
        }
        touch_scale.x = (float) screenX / size.x;
        touch_scale.y = (float) screenY / size.y;

        lock.lock();
        pendingImGuiEvents.clear();
        lastPointerPos = {};
        lastPointerDown = false;
        resetPhysicalTouchTracking();
        lock.unlock();

        readerThreads.clear();
        for (int i = 0; i < devices.size(); i++) {
            devices[i].S2TX = (float) screenX / (float) devices[i].absX.maximum;
            devices[i].S2TY = (float) screenY / (float) devices[i].absY.maximum;
            pthread_t thread{};
            if (pthread_create(&thread, nullptr, TypeA, (void *) (long) i) == 0)
                readerThreads.push_back(thread);
        }

        //system("chmod 000 -R /proc/bus/input/*");
        return true;
    }

    void Close() {
        if (initialized.load(std::memory_order_acquire)) {
            initialized.store(false, std::memory_order_release);
            for (pthread_t thread: readerThreads) pthread_join(thread, nullptr);
            readerThreads.clear();
            for (auto &device: devices) {
                if (!readOnly)
                    ioctl(device.fd, EVIOCGRAB, UNGRAB);
                close(device.fd);
                device.fd = 0;
            }
            if (nowfd > 0) {
                ioctl(nowfd, UI_DEV_DESTROY);
                close(nowfd);
                nowfd = 0;
            }
            memset(input.event, 0, sizeof(input.event));
            selfUinputName[0] = 0;
            selfEventPath[0] = 0;
            devices.clear();
            lock.lock();
            pendingImGuiEvents.clear();
            lastPointerDown = false;
            resetPhysicalTouchTracking();
            lock.unlock();
        }
    }

    void Down(float x, float y) {
        if (!CanInject() || devices.empty()) return;
        lock.lock();
        touchObj &touch = devices[0].Finger[9];
        touch.id = 19;
        touch.pos = Screen2Touch({x, y});
        touch.isDown = true;
        Upload();
        lock.unlock();
    }

    void Move(touchObj *touch, float x, float y) {
        if (!CanInject() || !touch) return;
        lock.lock();
        touch->pos = Screen2Touch({x, y});
        Upload();
        lock.unlock();
    }

    void Move(float x, float y) {
        Down(x, y);
    }

    void Up() {
        if (!CanInject() || devices.empty()) return;
        lock.lock();
        touchObj &touch = devices[0].Finger[9];
        touch.isDown = false;
        Upload();
        lock.unlock();
    }

    void SetCallBack(const std::function<void(std::vector<Device> *)> &cb) {
        callback = cb;
    }

    My_Vector2 Touch2Screen(const My_Vector2 &coord) {
        float x = coord.x, y = coord.y;
        float xt = x / touch_scale.x;
        float yt = y / touch_scale.y;

        if (otherTouch) {
            switch (orientation) {
                case 1:
                    x = xt;
                    y = yt;
                    break;
                case 2:
                    y = yt;
                    x = screenSize.y - xt;
                    break;
                case 3:
                    x = screenSize.y - xt;
                    y = screenSize.x - yt;
                    break;
                default:
                    y = xt;
                    x = screenSize.y - yt;
                    break;
            }
        } else {
            switch (orientation) {
                case 1:
                    x = yt;
                    y = screenSize.y - xt;
                    break;
                case 2:
                    x = screenSize.y - xt;
                    y = screenSize.x - yt;
                    break;
                case 3:
                    y = xt;
                    x = screenSize.x - yt;
                    break;
                default:
                    x = xt;
                    y = yt;
                    break;
            }
        }
        return {x, y};
    }

    My_Vector2 Screen2Touch(const My_Vector2 &coord) {
        float xt = coord.x, yt = coord.y;
        if (otherTouch) {
            switch (orientation) {
                case 1:
                    xt = coord.x;
                    yt = coord.y;
                    break;
                case 2:
                    xt = screenSize.y - coord.x;
                    yt = coord.y;
                    break;
                case 3:
                    xt = screenSize.y - coord.x;
                    yt = screenSize.x - coord.y;
                    break;
                default:
                    xt = coord.y;
                    yt = screenSize.y - coord.x;
                    break;
            }
        } else {
            switch (orientation) {
                case 1:
                    xt = screenSize.y - coord.y;
                    yt = coord.x;
                    break;
                case 2:
                    xt = screenSize.y - coord.x;
                    yt = screenSize.x - coord.y;
                    break;
                case 3:
                    xt = coord.y;
                    yt = screenSize.x - coord.x;
                    break;
                default:
                    xt = coord.x;
                    yt = coord.y;
                    break;
            }
        }
        return {xt * touch_scale.x, yt * touch_scale.y};
    }

    bool CanInject() {
        return initialized.load(std::memory_order_acquire) && !readOnly && nowfd > 0;
    }

    void SetLookAim(const LookAimCommand& cmd) {
        lock.lock();
        lookAim = cmd;
        lock.unlock();
    }

    bool HasPhysicalTouchInNormalizedRegion(float min_x, float min_y,
                                             float max_x, float max_y) {
        if (!initialized.load(std::memory_order_acquire)) return false;
        const float normalized_min_x = std::fmin(min_x, max_x);
        const float normalized_max_x = std::fmax(min_x, max_x);
        const float normalized_min_y = std::fmin(min_y, max_y);
        const float normalized_max_y = std::fmax(min_y, max_y);
        min_x = normalized_min_x;
        max_x = normalized_max_x;
        min_y = normalized_min_y;
        max_y = normalized_max_y;
        const float width = screenSize.x;
        const float height = screenSize.y;
        if (width <= 0.0F || height <= 0.0F) return false;
        lock.lock();
        bool found = false;
        for (const auto &device : devices) {
            // Finger[9] is reserved for the synthetic AIM look-stick contact.
            for (int index = 0; index < 9; ++index) {
                const auto &finger = device.Finger[index];
                if (!finger.isDown) continue;
                const My_Vector2 position = Touch2Screen(finger.pos);
                const float normalized_x = position.x / width;
                const float normalized_y = position.y / height;
                if (normalized_x >= min_x && normalized_x <= max_x &&
                    normalized_y >= min_y && normalized_y <= max_y) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        lock.unlock();
        return found;
    }

    bool HasPhysicalStationaryTouchInNormalizedRegion(float min_x, float min_y,
                                                       float max_x, float max_y,
                                                       float max_motion_px) {
        if (!initialized.load(std::memory_order_acquire)) return false;
        const float normalized_min_x = std::fmin(min_x, max_x);
        const float normalized_max_x = std::fmax(min_x, max_x);
        const float normalized_min_y = std::fmin(min_y, max_y);
        const float normalized_max_y = std::fmax(min_y, max_y);
        min_x = normalized_min_x;
        max_x = normalized_max_x;
        min_y = normalized_min_y;
        max_y = normalized_max_y;
        max_motion_px = std::max(0.0F, max_motion_px);
        const float width = screenSize.x;
        const float height = screenSize.y;
        if (width <= 0.0F || height <= 0.0F) return false;
        constexpr auto kMinimumHold = std::chrono::milliseconds(35);
        const auto now = std::chrono::steady_clock::now();
        lock.lock();
        bool found = false;
        std::unordered_map<int, bool> live;
        for (const auto &device : devices) {
            for (int index = 0; index < 9; ++index) {
                const auto &finger = device.Finger[index];
                if (!finger.isDown) continue;
                const My_Vector2 position = Touch2Screen(finger.pos);
                live[finger.id] = true;
                const auto start = physicalTouchStarts.find(finger.id);
                if (start == physicalTouchStarts.end()) {
                    physicalTouchStarts[finger.id] = position;
                    physicalTouchMoved[finger.id] = false;
                    physicalTouchStartedAt[finger.id] = now;
                } else if (std::hypot(position.x - start->second.x,
                                      position.y - start->second.y) > max_motion_px) {
                    physicalTouchMoved[finger.id] = true;
                }
                const float normalized_x = position.x / width;
                const float normalized_y = position.y / height;
                const auto held_for = now - physicalTouchStartedAt[finger.id];
                if (!physicalTouchMoved[finger.id] && held_for >= kMinimumHold &&
                    normalized_x >= min_x && normalized_x <= max_x &&
                    normalized_y >= min_y && normalized_y <= max_y) found = true;
            }
        }
        for (auto it = physicalTouchStarts.begin(); it != physicalTouchStarts.end();) {
            if (!live.count(it->first)) {
                physicalTouchMoved.erase(it->first);
                physicalTouchStartedAt.erase(it->first);
                it = physicalTouchStarts.erase(it);
            } else ++it;
        }
        lock.unlock();
        return found;
    }

    My_Vector2 GetScale() {
        return touch_scale;
    }

    void setOrientation(int o) {
        orientation = o;
    }

    void setOtherTouch(bool p_otherTouch) {
        otherTouch = p_otherTouch;
    }
}
