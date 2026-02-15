#include "dx12_framework.h"
#include "dx12_transformation.h"
#include "dx12_ui.h"
#include "../win32/window.h"

constexpr double PI = 3.1415926f;

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

struct PyramidVertex {
    float x, y, z;
    float u, v;
    float nx, ny, nz;
};

void GenerateNormal(std::vector<PyramidVertex>& vertices, std::vector<uint32_t>& indices) {
    for (uint32_t i = 0; i < indices.size(); i+= 3) {
        PyramidVertex& a = vertices[indices[i]];
        PyramidVertex& b = vertices[indices[i + 1]];
        PyramidVertex& c = vertices[indices[i + 2]];
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
    Win32Window* window_;
    DX12UI* ui_{};
    DX12FreeCamera* free_cam_{};
    DX12World* world_{};
public:
    SqaurePyramidApp(RenderPreset& presets) : DX12Application(presets) {
        window_ = new Win32Window(L"Grek Renderer", WS_OVERLAPPEDWINDOW, presets_.width, presets_.height, this, GameWindowProcess);
        presets_.hwnd = window_->handle();
        this->render_ctx_.Initialize();
        ShowCursor(FALSE);
        ShaderManager shader_mgr;
        shader_mgr.load_shaders();
        auto triangle_vs = shader_mgr.get("triangle.vs");
        auto triangle_ps = shader_mgr.get("triangle.ps");
        TextureManager tex_mgr;
        tex_mgr.load_textures();
        auto basic = tex_mgr.get("basic.jpg");
        auto brick = tex_mgr.get("brick.jpg");
        if (!triangle_vs.has_value() || !triangle_ps.has_value()) {
            std::cout << "shaders not exists" << std::endl;
            exit(EXIT_FAILURE);
        }

        if (!basic.has_value() || !brick.has_value()) {
            std::cout << "textures not exists" << std::endl;
            exit(EXIT_FAILURE);
        }

        std::vector<PyramidVertex> pyramid_vertices = {
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

        std::vector<uint32_t> pyramid_indices = {
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            9, 10, 11,
            12, 13, 14,
            12, 14, 15
        };

        std::vector<PyramidVertex> ground_vertices = {
            {-0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0, 0, 0},
            {0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0, 0, 0},
            {-0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0, 0, 0},
            {0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 0, 0, 0}
        };

        std::vector<uint32_t> ground_indices = {
            0, 1, 2,
            2, 1, 3
        };

        GenerateNormal(pyramid_vertices, pyramid_indices);
        GenerateNormal(ground_vertices, ground_indices);

        D3D12_INPUT_ELEMENT_DESC pyramid_layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_INPUT_LAYOUT_DESC layout = {pyramid_layout, _countof(pyramid_layout)};
        using PyramidDrawCallLayout = DrawCallLayout<
            DrawCallTexturesBinding<0, 32>,
            DrawCallCBVBinding<0>,
            DrawCallCBVBinding<1>,
            DrawCallStaticSamplerBinding<0, D3D12_FILTER_MIN_MAG_MIP_LINEAR>
        >;
        Pipeline<PyramidDrawCallLayout>::pipeline_init_t init = {32, 32, 0, presets.enable_msaa_4x};
        std::shared_ptr<Pipeline<PyramidDrawCallLayout>> default_pipeline = this->render_ctx_.CreatePipeline<PyramidDrawCallLayout>("default", init);
        GPUResourceManager& gr_mgr = this->render_ctx_.GetGPUResourceManager();
        auto py_vertices_res = gr_mgr.CreateVertexBuffer("pyramid_vertices", pyramid_vertices.data(), pyramid_vertices.size());
        auto py_indices_res = gr_mgr.CreateIndexBuffer("pyramid_indices", pyramid_indices.data(), pyramid_indices.size());
        auto gr_vertices_res = gr_mgr.CreateVertexBuffer("ground_vertices", ground_vertices.data(), ground_vertices.size());
        auto gr_indices_res = gr_mgr.CreateIndexBuffer("ground_indices", ground_indices.data(), ground_indices.size());
        auto scene_res = gr_mgr.CreateCBuffer("scene", scene);
        auto world_res = gr_mgr.CreateCBuffer("world", world);
        auto basic_tex = gr_mgr.CreateTexture("pyramid_tex", basic.value().width, basic.value().height, basic.value().data);
        auto brick_tex = gr_mgr.CreateTexture("brick_tex", brick.value().width, brick.value().height, brick.value().data);
        auto heap = this->render_ctx_.CreateTextureHeap<32>();
        heap->BindTexture(basic_tex);
        heap->BindTexture(brick_tex);
        PyramidDrawCallLayout::Bindings pyramid_bindings(*heap, scene_res, world_res, DrawCallStaticSamplerBinding<0, D3D12_FILTER_MIN_MAG_MIP_LINEAR>());
        PyramidDrawCallLayout::Bindings ground_bindings(*heap, scene_res, world_res, DrawCallStaticSamplerBinding<0, D3D12_FILTER_MIN_MAG_MIP_LINEAR>());
        DrawCall<PyramidDrawCallLayout> py_drawcall(std::move(pyramid_bindings));
        py_drawcall.BindIABuffer(py_vertices_res, py_indices_res, 1);
        DrawCall<PyramidDrawCallLayout> gr_drawcall(std::move(ground_bindings));
        gr_drawcall.BindIABuffer(gr_vertices_res, gr_indices_res, 1);
        default_pipeline->BindIALayout(layout);
        default_pipeline->BindDrawCall(py_drawcall);
        default_pipeline->BindDrawCall(gr_drawcall);
        default_pipeline->BindVertexShader(triangle_vs.value());
        default_pipeline->BindFragmentShader(triangle_ps.value());
        default_pipeline->Build();
        ui_ = new DX12UI(*this, "Lanting", tex_mgr, shader_mgr);
        free_cam_ = new DX12FreeCamera({0.001f, 0.1f, static_cast<float>(presets_.width) / static_cast<float>(presets_.height), presets_.hwnd});
        world_ = new DX12World();
    }
    float theta = 0.0f;
    float omega = 0.0005f;

    virtual void Update(float delta_ms) override {
        window_->set_title(std::format(L"Grek Renderer | {} FPS", fpsc_.fps()));
        ui_->DrawString(std::format(L"Grek渲染器 | {} FPS", fpsc_.fps()), 10, 640, 16);
        auto& mgr = this->render_ctx_.GetGPUResourceManager();
        mgr.ModifyCBuffer("scene", scene);
        mgr.ModifyCBuffer("world", world);
        theta += omega * delta_ms;
        free_cam_->UpdatePerspective(delta_ms);
        memcpy(&scene.camera_pos[0], &free_cam_->GetCameraPosition().x, sizeof(float) * 3);
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(&world.world_matrix[0]), world_->GetObjectMatrix({0.0f, theta, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(&world.world_matrix[1]), world_->GetObjectMatrix({0.0f, 0.0f, 0.0f}, {0.0f, -4.0f, 0.0f}, {1.0f, 1.0f, 1.0f}));
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4 *>(&scene.view_matrix), free_cam_->GetViewMatrix());
        ui_->UpdateUI();
    }
    virtual void OnWindowActivate(WPARAM wParam) override {
        if (free_cam_ != nullptr) free_cam_->OnWindowActive(wParam);
    }
    virtual void OnKeyDown(WPARAM wParam) override {

    }
    virtual void OnKeyUp(WPARAM wParam) override {
        switch (wParam) {
            case VK_ESCAPE: {
                PostQuitMessage(0);
            }
        }

    }
};