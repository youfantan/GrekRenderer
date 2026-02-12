#include "dx12_framework.h"
#include "../win32/window.h"

constexpr double PI = 3.1415926f;
bool press_w = false;
bool press_a = false;
bool press_d = false;
bool press_s = false;
bool press_space = false;
bool press_shift = false;
bool press_alt = false;
float theta = 0.0f;
float speed = 0.005f;
float omega = 0.0005f;
float camera_yaw = 90.0f;
float camera_pitch = 0.0f;
float sensitivity = 0.2f;
bool is_active = false;

struct alignas(256) Scene {
    float color[4];
    float light_pos[4];
    float light_color[4];
    float ambient[4];
    float camera_pos[4];
    float view_matrix[4][4];
};

struct alignas(256) World {
    float world_matrix[2][4][4];
};

Scene scene {
    .color = {0.7f, 0.7f, 0.7f, 1.0f},
    .light_pos = { 0.5f, 1.0f, 0.0f, 0.0f},
    .light_color = {1.0f, 0.9f, 0.9f, 1.0f},
    .ambient = {0.2f, 0.2f, 0.2f, 1.0f},
    .camera_pos = {0.0f, 0.0f, 0.0f},
};

World world{};

XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
XMFLOAT3 forward = {0.0f, 0.0f, 0.0f};

XMMATRIX matrix_apply_RTS(float pitch, float yaw, float roll, float x, float y, float z, float sx, float sy, float sz) {
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
    XMMATRIX tranlation = XMMatrixTranslation(x, y, z);
    XMMATRIX scaling = XMMatrixScaling(sx, sy, sz);
    return scaling * rotation * tranlation;
}

XMMATRIX matrix_apply_View(XMFLOAT3 camera, XMFLOAT3 focus) {
    return XMMatrixLookAtLH(XMLoadFloat3(&camera), XMLoadFloat3(&focus), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

XMMATRIX matrix_apply_Projection() {
    return XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.77f, 0.1f, 100.0f);
}

struct Vertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
};

void GenerateNormal(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    for (uint32_t i = 0; i < indices.size(); i+= 3) {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        XMVECTOR d = XMVectorSet(a.x - b.x, a.y - b.y, a.z - b.z, 0.0f);
        XMVECTOR e = XMVectorSet(c.x - b.x, c.y - b.y, c.z - b.z, 0.0f);
        XMVECTOR v_normal = XMVector3Cross(d, e);
        XMVector3Normalize(v_normal);
        XMFLOAT3 normal {};
        XMStoreFloat3(&normal, v_normal);

        memcpy(&a.nx, &normal, sizeof(XMFLOAT3));
        memcpy(&b.nx, &normal, sizeof(XMFLOAT3));
        memcpy(&c.nx, &normal, sizeof(XMFLOAT3));
    }
}

class SqaurePyramidApp : public DX12Application {
private:
    Win32Window window_;
public:
    SqaurePyramidApp(RenderContext::rendering_presets& presets) : DX12Application(presets), window_(L"Grek Renderer", WS_OVERLAPPEDWINDOW, presets_.width, presets_.height, this, GameWindowProcess) {
        presets_.hwnd = window_.handle();
        this->render_ctx_.Initialize();
        ShowCursor(FALSE);
        shader_manager shader_mgr;
        shader_mgr.load_shaders();
        auto triangle_vs = shader_mgr.get("triangle.vs");
        auto triangle_ps = shader_mgr.get("triangle.ps");
        texture_manager tex_mgr;
        tex_mgr.load_textures();
        auto basic = tex_mgr.get("13551.jpg");
        if (!triangle_vs.has_value() || !triangle_ps.has_value()) {
            std::cout << "triangle shader not exists" << std::endl;
            exit(EXIT_FAILURE);
        }

        if (!basic.has_value()) {
            std::cout << "basic texture not exists" << std::endl;
            exit(EXIT_FAILURE);
        }

        std::vector<Vertex> vertices = {
            { 0.0f,  0.5f,  0.0f, 0.5f, 0.0f, 0, 0, 0},
            { 0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0, 0, 0},
            {-0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0, 0, 0},

            { 0.0f,  0.5f,  0.0f, 0.5f, 0.0f, 0, 0, 0},
            { 0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 0, 0, 0},
            { 0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0, 0, 0},

            { 0.0f,  0.5f,  0.0f, 0.5f, 0.0f, 0, 0, 0},
            {-0.5f, -0.5f,  0.5f, 1.0f, 1.0f, 0, 0, 0},
            { 0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0, 0, 0},

            { 0.0f,  0.5f,  0.0f, 0.5f, 0.0f, 0, 0, 0},
            {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0, 0, 0},
            {-0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0, 0, 0},

            {-0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0, 0, 0},
            { 0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0, 0, 0},
            { 0.5f, -0.5f,  0.5f, 0.0f, 1.0f, 0, 0, 0},
            {-0.5f, -0.5f,  0.5f, 0.0f, 0.0f, 0, 0, 0}
        };

        std::vector<uint32_t> indices = {
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            9, 10, 11,
            12, 13, 14,
            12, 14, 15
        };

        GenerateNormal(vertices, indices);

        D3D12_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_INPUT_LAYOUT_DESC layout = {il, _countof(il)};
        Pipeline& default_pipeline = this->render_ctx_.CreatePipeline("default");
        GPUResourceManager& gr_mgr = this->render_ctx_.GetGPUResourceManager();
        SRVManager& srv_mgr = this->render_ctx_.GetSRVManager();
        auto vertices_res = gr_mgr.CreateVertexBuffer("pyramid_vertices", vertices.data(), vertices.size());
        auto indices_res = gr_mgr.CreateIndexBuffer("pyramid_indices", indices.data(), indices.size());
        auto scene_res = gr_mgr.CreateCBuffer("scene", scene);
        auto world_res = gr_mgr.CreateCBuffer("world", world);
        auto tex_res = gr_mgr.CreateTexture("pyramid_tex", basic.value().width, basic.value().height, basic.value().data);
        srv_mgr.BindTextureSRV(tex_res);
        default_pipeline.BindVertexBuffer(vertices_res);
        default_pipeline.BindIndexBuffer(indices_res);
        default_pipeline.BindCBuffer(scene_res);
        default_pipeline.BindCBuffer(world_res);
        default_pipeline.BindStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR));
        default_pipeline.BindIALayout(layout);
        default_pipeline.BindVertexShader(triangle_vs.value());
        default_pipeline.BindFragmentShader(triangle_ps.value());
        default_pipeline.BindSRVHeap(srv_mgr.GetSRVHeap());
        default_pipeline.Build();
        render_ctx_.SelectPipeline("default");
    }

    virtual void Update(float delta_ms) override {
        window_.set_title(std::format(L"Grek Renderer | {} FPS", fpsc_.fps()));
        auto& mgr = this->render_ctx_.GetGPUResourceManager();
        mgr.ModifyCBuffer("scene", scene);
        mgr.ModifyCBuffer("world", world);
        if (delta_ms == 0.0f) return;
        // Get cursor and calculate pitch and yaw
        POINT currentPos;
        GetCursorPos(&currentPos);
        RECT rect;
        GetWindowRect(presets_.hwnd, &rect);
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
        auto displacement = speed * delta_ms;
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
        memcpy(scene.camera_pos, &position.x, sizeof(XMFLOAT3));

        // Enable block to rotate
        theta += omega * delta_ms;
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(&world.world_matrix[0]), matrix_apply_RTS(0.0f, theta, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f));
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(&world.world_matrix[1]), matrix_apply_RTS(0.0f, theta, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f));
        XMMATRIX view = matrix_apply_View(position, forward);
        XMMATRIX projection = matrix_apply_Projection();
        XMMATRIX vp = view * projection;
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(scene.view_matrix), vp);
    }
    virtual void OnWindowActivate(WPARAM wParam) override {
        if (LOWORD(wParam) == WA_INACTIVE) {
            is_active = false;
            ShowCursor(TRUE);
        }
        else {
            is_active = true;
            ShowCursor(FALSE);
        }
    }
    virtual void OnKeyDown(WPARAM wParam) override {
        switch (wParam) {
            case 'W': press_w = true; break;
            case 'A': press_a = true; break;
            case 'S': press_s = true; break;
            case 'D': press_d = true; break;
            case VK_SPACE: press_space = true; break;
            case VK_SHIFT: press_shift = true; break;
            case VK_MENU: press_alt = true; break;
        }
    }
    virtual void OnKeyUp(WPARAM wParam) override {
        switch (wParam) {
            case VK_ESCAPE: {
                PostQuitMessage(0);
            }
            case 'W': press_w = false; break;
            case 'A': press_a = false; break;
            case 'S': press_s = false; break;
            case 'D': press_d = false; break;
            case VK_SPACE: press_space = false; break;
            case VK_SHIFT: press_shift = false; break;
            case VK_MENU: press_alt = false; break;
        }

    }
};