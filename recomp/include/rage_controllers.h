#pragma once
#include <cstdint>
void rage_controllers_configure(const char* directory);
void rage_controllers_show(void* parent);
extern "C" unsigned rage_host_pad_buttons(int slot);
extern "C" int rage_host_device_count();
extern "C" const char* rage_host_device_name(int device);
extern "C" const char* rage_host_device_guid(int device);
extern "C" void rage_host_assign(int slot,const char* device);
extern "C" const char* rage_host_assignment(int slot);
extern "C" void rage_host_binding(int slot,const char* key,const char* source);
extern "C" const char* rage_host_mapping(int slot,const char* key);
