#pragma once

#include <stdexcept>
#include <string>
#include "Windows.h"

class Win32Window {
public:
    using callback_t = LRESULT(*)(HWND, UINT, WPARAM, LPARAM);
private:
    std::wstring title_;
    DWORD style_;
    uint32_t width_;
    uint32_t height_;
    callback_t cb_;
    HWND handle_;
public:
    Win32Window(std::wstring_view title, DWORD style, int32_t width, int32_t height, void* app, callback_t cb) : title_(title), style_(style), width_(width), height_(height), cb_(std::move(cb)) {
        WNDCLASS wc = { };
        wc.lpfnWndProc = cb;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = title.data();
        RegisterClass(&wc);
        HWND hwnd = CreateWindowEx(0,title.data(), title.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width_, height_, nullptr, nullptr, GetModuleHandle(nullptr), app);
        handle_ = hwnd;
        if (hwnd == nullptr)
        {
            throw std::runtime_error("Failed to create a window");
        }
        ShowWindow(hwnd, SW_SHOW);
    }

    void run_loop() {
        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void set_title(const std::wstring& text) {
        SetWindowText(handle_, text.c_str());
    }

    HWND handle() {
        return handle_;
    }
};
