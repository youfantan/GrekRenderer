#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "Windows.h"

inline std::optional<std::wstring> mb_to_wc_win32(std::string_view src) {
    if (src.empty()) return std::nullopt;
    int len = MultiByteToWideChar(CP_ACP, 0, src.data(), -1, nullptr, 0);
    if (len == 0) return std::nullopt;
    wchar_t* dst = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!dst) return std::nullopt;
    MultiByteToWideChar(CP_ACP, 0, src.data(), -1, dst, len);
    return dst;
}

class fps_counter {
private:
    LARGE_INTEGER freq_;
    LARGE_INTEGER prev_;
    float fps_;
public:
    fps_counter() {
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