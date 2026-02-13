#define STB_IMAGE_IMPLEMENTATION

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <cmath>

#include "src/dx12/dx12_framework.h"
#include "src/dx12/square_pyramid_sample.hpp"
#include "src/win32/common.h"
#include "src/win32/window.h"


int main() {
    RenderContext::rendering_presets presets = {
        .width = 1280,
        .height = 720,
        .enable_msaa_4x = true,
        .clear_color = {0.5f, 0.5f, 0.5f, 1.0f},
        .hwnd = nullptr,
    };
    SqaurePyramidApp app(presets);
    app.Run();
    return 0;
}
