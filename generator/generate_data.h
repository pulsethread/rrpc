#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
struct GenerateRequest {std::filesystem::path input,output;bool physical=false,replace=false;};
using GenerateProgress=std::function<void(int,const std::wstring&)>;
void generate_data(const GenerateRequest&,const GenerateProgress&,const std::atomic<bool>& cancel);
