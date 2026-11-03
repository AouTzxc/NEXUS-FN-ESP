#pragma once

namespace rebootnova::runtime {

bool initialize(const char* path = nullptr);
void install_crash_handlers();
void set_stage(const char* stage);
void log(const char* format, ...);
void shutdown();

}  // namespace rebootnova::runtime
