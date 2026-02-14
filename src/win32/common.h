#pragma once

#include <optional>
#include <string>
#include <string_view>
#define NOMINMAX
#include "Windows.h"

inline std::optional<std::wstring> string_to_wstring(std::string_view src) {
    if (src.empty()) return std::nullopt;
    int len = MultiByteToWideChar(CP_ACP, 0, src.data(), -1, nullptr, 0);
    if (len == 0) return std::nullopt;
    wchar_t* dst = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!dst) return std::nullopt;
    MultiByteToWideChar(CP_ACP, 0, src.data(), -1, dst, len);
    return dst;
}

inline std::optional<std::string> wstring_to_string(std::wstring_view src)
{
    if (src.empty()) return std::nullopt;
    int len = WideCharToMultiByte(CP_ACP, 0, src.data(), -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) return std::nullopt;
    char* dst = (char*)malloc(len);
    if (!dst) return std::nullopt;
    WideCharToMultiByte( CP_ACP, 0, src.data(), -1, dst, len, nullptr, nullptr);
    return dst;
}

inline bool read_file_to_string(std::string_view file_name, std::string& str) {
    std::ifstream fs(file_name.data(), std::ios::in | std::ios::binary);
    if (!fs.good()) return false;
    fs.seekg(0, std::ios::end);
    size_t sz = fs.tellg();
    fs.seekg(0, std::ios::beg);
    str.resize(sz + 1);
    fs.read(str.data(), sz);
    return true;
}

class FPSCounter {
private:
    LARGE_INTEGER freq_;
    LARGE_INTEGER prev_;
    float fps_;
public:
    FPSCounter() {
        QueryPerformanceFrequency(&freq_);
        QueryPerformanceCounter(&prev_);
    }

    float delta_ms() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float delta_ms = static_cast<float>(now.QuadPart - prev_.QuadPart) / static_cast<float>(freq_.QuadPart) * 1000;
        fps_ = 1.0f / (delta_ms / 1000);
        prev_ = now;
        return delta_ms;
    }

    int fps() {
        return fps_;
    }
};

inline void hr_failed(const char* file, int line, HRESULT hr) {
    auto text = string_to_wstring(std::format("Operation Failed in file {}:{}. HRESULT: {:X}", file, line, static_cast<uint32_t>(hr)));
    MessageBox(nullptr, text.value().c_str(), L"ERROR", MB_OK);
}

#define CHECKHR(hr) if (!SUCCEEDED(hr)) hr_failed(__FILE__, __LINE__, hr)