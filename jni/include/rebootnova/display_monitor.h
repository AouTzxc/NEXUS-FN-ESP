#pragma once
#include "native_surface/ANativeWindowCreator.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace rebootnova {
// Compositor IPC / dumpsys never run on the steady-state render thread.
// NativeWindowCreator serializes surface operations internally. No Settings reference crosses threads.
class DisplayMonitor {
public:
    using Info = decltype(android::ANativeWindowCreator::GetDisplayInfo());
    explicit DisplayMonitor(Info initial): info_(initial),worker_([this]{run();}) {}
    ~DisplayMonitor(){stop();}
    void stop(){stopping_.store(true);wake_.notify_all();if(worker_.joinable())worker_.join();}
    void mirror(bool enabled){mirror_.store(enabled);}
    Info snapshot(){std::lock_guard<std::mutex> lock(mutex_);return info_;}
    double query_ms() const{return query_ms_.load();}
    double mirror_ms() const{return mirror_ms_.load();}
private:
    void run(){
        while(!stopping_.load()) {
            const auto begin=std::chrono::steady_clock::now();
            const auto next=android::ANativeWindowCreator::GetDisplayInfo();
            const auto queried=std::chrono::steady_clock::now();
            if(next.width>0&&next.height>0){std::lock_guard<std::mutex> lock(mutex_);info_=next;}
            if(mirror_.load()&&!stopping_.load())android::ANativeWindowCreator::ProcessMirrorDisplay();
            const auto end=std::chrono::steady_clock::now();
            query_ms_.store(std::chrono::duration<double,std::milli>(queried-begin).count());
            mirror_ms_.store(std::chrono::duration<double,std::milli>(end-queried).count());
            std::unique_lock<std::mutex> lock(wait_mutex_);
            wake_.wait_for(lock,std::chrono::milliseconds(250),[&]{return stopping_.load();});
        }
    }
    Info info_;
    std::mutex mutex_,wait_mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stopping_{false},mirror_{false};
    std::atomic<double> query_ms_{0},mirror_ms_{0};
    std::thread worker_;
};
}
