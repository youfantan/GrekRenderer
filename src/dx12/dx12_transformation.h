#pragma once

#include <Windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <directx/d3dx12.h>
#include <DirectXMath.h>

using namespace DirectX;

class DX12FreeCamera {
public:
    using free_camera_init_param_t = struct {
        float speed;
        float sensitivity;
        float aspect_ratio;
        HWND hwnd;
    };
private:
    float speed_;
    float sensitivity_;
    float camera_yaw_ = 90.0f;
    float camera_pitch_ = 0.0f;
    float aspect_ratio_;

    XMFLOAT3 camera_position_ = {0.0f, 0.0f, 0.0f};
    XMFLOAT3 camera_forward_ = {0.0f, 0.0f, 0.0f};

    HWND hwnd_;

    bool press_w_ = false;
    bool press_a_ = false;
    bool press_d_ = false;
    bool press_s_ = false;
    bool press_space_ = false;
    bool press_shift_ = false;
    bool is_active_ = true;

    XMMATRIX vp_{};

    XMMATRIX matrix_apply_View(XMFLOAT3 camera, XMFLOAT3 focus) {
        return XMMatrixLookAtLH(XMLoadFloat3(&camera), XMLoadFloat3(&focus), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    }

    XMMATRIX matrix_apply_Projection() {
        return XMMatrixPerspectiveFovLH(XM_PIDIV2, aspect_ratio_, 0.1f, 100.0f);
    }

public:
    DX12FreeCamera(const free_camera_init_param_t& param) : sensitivity_(param.sensitivity), speed_(param.speed), aspect_ratio_(param.aspect_ratio), hwnd_(param.hwnd) {}

    XMMATRIX& GetViewMatrix() {
        return vp_;
    }

    XMFLOAT3& GetCameraPosition() {
        return camera_position_;
    }

    void SetCameraSensitivity(float s) {
        sensitivity_ = s;
    }

    void SetCameraSpeed(float s) {
        speed_ = s;
    }

    void OnWindowActive(WPARAM wParam) {
        if (LOWORD(wParam) == WA_INACTIVE) {
            is_active_ = false;
            ShowCursor(TRUE);
        }
        else {
            is_active_ = true;
            ShowCursor(FALSE);
        }
    }

    void UpdatePerspective(float delta_ms) {
        if (delta_ms == 0.0f) return;
        // Get cursor and calculate pitch and yaw
        POINT currentPos;
        GetCursorPos(&currentPos);
        RECT rect;
        GetWindowRect(hwnd_, &rect);
        int centerX = rect.left + (rect.right - rect.left) / 2;
        int centerY = rect.top + (rect.bottom - rect.top) / 2;
        float dx = static_cast<float>(currentPos.x - centerX);
        float dy = static_cast<float>(currentPos.y - centerY);
        press_w_ = (GetAsyncKeyState('W') & 0x8000) >> 15;
        press_a_ = (GetAsyncKeyState('A') & 0x8000) >> 15;
        press_s_ = (GetAsyncKeyState('S') & 0x8000) >> 15;
        press_d_ = (GetAsyncKeyState('D') & 0x8000) >> 15;
        press_shift_ = (GetAsyncKeyState(VK_SHIFT) & 0x8000) >> 15;
        press_space_ = (GetAsyncKeyState(VK_SPACE) & 0x8000) >> 15;
        if (dx != 0 || dy != 0) {
            // Calculate camera yaw and pitch, limit pitch to (-89, 89)
            camera_yaw_ -= dx * sensitivity_;
            camera_pitch_ += dy * sensitivity_;
            if (camera_pitch_ > 89.0f)  camera_pitch_ = 89.0f;
            if (camera_pitch_ < -89.0f) camera_pitch_ = -89.0f;
            // Reset cursor position
            if (is_active_) SetCursorPos(centerX, centerY);
        }

        float r_pitch = XMConvertToRadians(camera_pitch_);
        float r_yaw = XMConvertToRadians(camera_yaw_);
        // Transformation from Spherical coordinate system to Cartesian coordinate system
        XMVECTOR v_fwd = XMVectorSet(cosf(r_pitch) * cosf(r_yaw), -sinf(r_pitch), cosf(r_pitch) * sinf(r_yaw), 0.0f);
        // Normalize
        v_fwd = XMVector3Normalize(v_fwd);
        // Calculate precise displacement by delta time between frames
        auto displacement = speed_ * delta_ms;
        XMVECTOR v_pos = XMLoadFloat3(&camera_position_);
        // Limit y axis when pressed W or S
        XMVECTOR v_move_fwd = XMVectorSet(XMVectorGetX(v_fwd), 0.0f, XMVectorGetZ(v_fwd), 0.0f);
        v_move_fwd = XMVector3Normalize(v_move_fwd);
        XMVECTOR v_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        // Use Cross to get the right direction vector
        XMVECTOR v_right = XMVector3Normalize(XMVector3Cross(v_up, v_move_fwd));
        if (press_w_) v_pos = XMVectorAdd(v_pos, XMVectorScale(v_move_fwd, displacement));
        if (press_s_) v_pos = XMVectorSubtract(v_pos, XMVectorScale(v_move_fwd, displacement));
        if (press_a_) v_pos = XMVectorSubtract(v_pos, XMVectorScale(v_right, displacement));
        if (press_d_) v_pos = XMVectorAdd(v_pos, XMVectorScale(v_right, displacement));
        if (press_space_) v_pos = XMVectorAdd(v_pos, XMVectorScale(v_up, displacement));
        if (press_shift_) v_pos = XMVectorSubtract(v_pos, XMVectorScale(v_up, displacement));
        // Save position and forward vector
        XMStoreFloat3(&camera_position_, v_pos);
        XMVECTOR v_focus = XMVectorAdd(v_pos, v_fwd);
        XMStoreFloat3(&camera_forward_, v_focus);
        // Enable block to rotate
        XMMATRIX view = matrix_apply_View(camera_position_, camera_forward_);
        XMMATRIX projection = matrix_apply_Projection();
        vp_ = view * projection;
    }
};

class DX12World {
public:
    DX12World() = default;
    XMMATRIX GetObjectMatrix(XMFLOAT3 rotation, XMFLOAT3 translation, XMFLOAT3 scaling) {
        XMMATRIX r = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
        XMMATRIX t = XMMatrixTranslation(translation.x, translation.y, translation.z);
        XMMATRIX s = XMMatrixScaling(scaling.x, scaling.y, scaling.z);
        return s * r * t;
    }
};