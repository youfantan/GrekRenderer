#define STB_IMAGE_IMPLEMENTATION

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <cmath>

#include "src/common/font_loader.h"
#include "src/common/logger.h"
#include "src/dx12/dx12_framework.h"
#include "src/dx12/square_pyramid_sample.hpp"
#include "src/win32/common.h"
#include "src/win32/window.h"


int main() {
    // FontLoader loader("Lanting");
    // loader.GenerateFontTextureAndMeta();
    log_init("grek_render.log");
    atexit([]() {
        log_close();
    });
    RenderPreset presets = {
        .width = 1280,
        .height = 720,
        .enable_msaa_4x = false,
        .enable_full_screen = false,
        .enable_v_sync = true,
        .clear_color = {0.5f, 0.5f, 0.5f, 1.0f},
        .hwnd = nullptr,
    };
    SqaurePyramidApp app(presets);
    app.Run();
    return 0;
}
