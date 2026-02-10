#ifndef UNICODE
#define UNICODE
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <cmath>

#include "src/common/obj_loader.h"
#include "src/dx12/dx12_framework.h"
#include "src/win32/common.h"
#include "src/win32/window.h"

constexpr double PI = 3.1415926f;
bool press_w = false;
bool press_a = false;
bool press_d = false;
bool press_s = false;
bool press_space = false;
bool press_shift = false;
bool press_alt = false;
float theta = 0.0f;
float speed = 0.01f;
float omega = 0.01f;
float camera_yaw = 90.0f;
float camera_pitch = 0.0f;
float sensitivity = 0.2f;
HWND hwnd;
bool is_active = true;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                is_active = false;
                ShowCursor(TRUE);
            } else {
                is_active = true;
                ShowCursor(FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            switch (wParam) {
                case 'W': press_w = true;
                    break;
                case 'A': press_a = true;
                    break;
                case 'S': press_s = true;
                    break;
                case 'D': press_d = true;
                    break;
                case VK_SPACE: press_space = true;
                    break;
                case VK_SHIFT: press_shift = true;
                    break;
                case VK_MENU: press_alt = true; break;
            }
            break;
        }
        case WM_KEYUP: {
            switch (wParam) {
                case VK_ESCAPE: {
                    PostQuitMessage(0);
                    return 0;
                }
                case 'W': press_w = false;
                    break;
                case 'A': press_a = false;
                    break;
                case 'S': press_s = false;
                    break;
                case 'D': press_d = false;
                    break;
                case VK_SPACE: press_space = false;
                    break;
                case VK_SHIFT: press_shift = false;
                    break;
                case VK_MENU: press_alt = false; break;
            }
            break;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

struct SceneData {
    float color[6][4];
    float world_matrix[4][4];
    float padding[24];
};

SceneData scene_data{
    .color = {
        {0.3f, 0.3f, 0.3f, 1.0f},
        {0.2f, 1.0f, 0.3f, 1.0f},
        {0.6f, 0.4f, 0.8f, 1.0f},
        {1.0f, 0.6f, 0.2f, 1.0f},
        {1.0f, 0.2f, 1.0f, 1.0f},
        {0.1f, 0.5f, 0.2f, 1.0f}
    }
};

XMFLOAT3 position = {0.0f, 0.0f, 2.0f};
XMFLOAT3 forward = {0.0f, 0.0f, 0.0f};

XMMATRIX world;
XMMATRIX view;
XMMATRIX projection;

void matrix_apply_RTS(float pitch, float yaw, float roll, float x, float y, float z, float sx, float sy, float sz) {
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
    XMMATRIX tranlation = XMMatrixTranslation(x, y, z);
    XMMATRIX scaling = XMMatrixScaling(sx, sy, sz);
    world = scaling * rotation * tranlation;
}

void matrix_apply_View(XMFLOAT3 camera, XMFLOAT3 focus) {
    view = XMMatrixLookAtLH(XMLoadFloat3(&camera), XMLoadFloat3(&focus), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

void matrix_apply_Projection() {
    projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.77f, 0.1f, 100.0f);
}

void update(float delta) {
    if (delta == 0.0f) return;
    // Get cursor and calculate pitch and yaw
    POINT currentPos;
    GetCursorPos(&currentPos);
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int centerX = rect.left + (rect.right - rect.left) / 2;
    int centerY = rect.top + (rect.bottom - rect.top) / 2;
    float dx = static_cast<float>(currentPos.x - centerX);
    float dy = static_cast<float>(currentPos.y - centerY);

    if (dx != 0 || dy != 0) {
        // Calculate camera yaw and pitch, limit pitch to (-89, 89)
        camera_yaw -= dx * sensitivity;
        camera_pitch += dy * sensitivity;
        if (camera_pitch > 89.0f)  camera_pitch = 89.0f;
        if (camera_pitch < -89.0f) camera_pitch = -89.0f;
        // Reset cursor position
        if (is_active) SetCursorPos(centerX, centerY);
    }

    float r_pitch = XMConvertToRadians(camera_pitch);
    float r_yaw = XMConvertToRadians(camera_yaw);
    // Transformation from Spherical coordinate system to Cartesian coordinate system
    XMVECTOR v_fwd = XMVectorSet(cosf(r_pitch) * cosf(r_yaw), -sinf(r_pitch), cosf(r_pitch) * sinf(r_yaw), 0.0f);
    // Normalize
    v_fwd = XMVector3Normalize(v_fwd);
    // Calculate precise displacement by delta time between frames
    auto displacement = speed * delta;
    XMVECTOR v_pos = XMLoadFloat3(&position);
    // Limit y axis when pressed W or S
    XMVECTOR v_move_fwd = XMVectorSet(XMVectorGetX(v_fwd), 0.0f, XMVectorGetZ(v_fwd), 0.0f);
    v_move_fwd = XMVector3Normalize(v_move_fwd);
    XMVECTOR v_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    // Use Cross to get the right direction vector
    XMVECTOR v_right = XMVector3Normalize(XMVector3Cross(v_up, v_move_fwd));
    if (press_w) v_pos = XMVectorAdd(v_pos, XMVectorScale(v_move_fwd, displacement));
    if (press_s) v_pos = XMVectorSubtract(v_pos, XMVectorScale(v_move_fwd, displacement));
    if (press_a) v_pos = XMVectorSubtract(v_pos, XMVectorScale(v_right, displacement));
    if (press_d) v_pos = XMVectorAdd(v_pos, XMVectorScale(v_right, displacement));
    if (press_space) v_pos = XMVectorAdd(v_pos, XMVectorScale(v_up, displacement));
    if (press_shift) v_pos = XMVectorSubtract(v_pos, XMVectorScale(v_up, displacement));
    // Save position and forward vector
    XMStoreFloat3(&position, v_pos);
    XMVECTOR v_focus = XMVectorAdd(v_pos, v_fwd);
    XMStoreFloat3(&forward, v_focus);

    // Enable block to rotate
    theta += omega * delta;

    matrix_apply_RTS(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    matrix_apply_View(position, forward);
    matrix_apply_Projection();
    XMMATRIX wvp = world * view * projection;
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(scene_data.world_matrix), wvp);
}

int main() {
    obj_loader loader;
    loader.load_model("columbia.obj");
    win32_window window(L"Grek Renderer", WS_OVERLAPPEDWINDOW, 1280, 720, WindowProc);
    fps_counter fpsc;
    hwnd = window.handle();
    ShowCursor(FALSE);
    shader_manager mgr;
    mgr.load_shaders();
    auto triangle_vs = mgr.get("triangle.vs");
    auto triangle_ps = mgr.get("triangle.ps");
    if (!triangle_vs.has_value() || !triangle_ps.has_value()) {
        std::cout << "triangle shader not exists" << std::endl;
        exit(EXIT_FAILURE);
    }

    obj_loader::vertex* vertices = loader.vertices().data();
    uint32_t* indices = loader.indices().data();

    dx12_framework::dx12_inital_param_t param{
        .width = 1280,
        .height = 720,
        .hwnd = hwnd,
        .enable_msaa_4x = true,
        .clear_color = {0.5f, 0.2f, 0.3f, 1.0f},
    };
    dx12_framework framework(param);
    D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    CD3DX12_ROOT_PARAMETER root_params[1];
    root_params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    dx12_framework::pso_params_t pso_p {
        .root_params = root_params,
        .root_params_sz = _countof(root_params),
        .ie_descs = il,
        .ie_descs_sz = _countof(il),
        .vertex_shader = triangle_vs.value(),
        .fragment_shader = triangle_ps.value()
    };
    framework.apply_ia(vertices, loader.vertices().size() * sizeof(obj_loader::vertex), indices, loader.indices().size() * sizeof(uint32_t));
    framework.apply_cb(&scene_data);
    framework.apply_pso(pso_p);
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            framework.render(loader.indices().size());
            auto delta = fpsc.delta_ms();
            update(delta);
            framework.apply_cb(&scene_data);
            window.set_title(std::format(L"Grek Renderer | {} FPS", fpsc.fps()));
        }
    }
    framework.wait();
    return 0;
}
