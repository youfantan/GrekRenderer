#pragma once

#include "dx12_framework.h"
#include "../common/font_loader.h"

class DX12UI {
private:
    DX12Application& dx_app_;
    TextureManager& tex_mgr_;
    ShaderManager& shd_mgr_;
    GPUResourceManager& res_mgr_;
    struct Vertex {
        float x, y;
        float u, v;
    };
    std::vector<Vertex> vertices_ = {
        {-0.5f, 0.5f, 0.0f, 0.0f},
        {0.5f, 0.5f, 1.0f, 0.0f},
        {-0.5f, -0.5f, 0.0f, 1.0f},
        {0.5f, -0.5f, 1.0f, 1.0f}
    };
    std::vector<uint32_t> indices_ = {
        0, 1, 2,
        2, 1, 3
    };

    struct TextInfo {
        uint32_t char_index;
        uint32_t x;
        uint32_t y;
        uint32_t size;
    };

    struct alignas(256) ScreenInfo {
        float width;
        float height;
        float atlas_font_size;
    };
    std::vector<FontLoader::SDFMeta> coords_;
    std::unordered_map<wchar_t, uint32_t> char_mapping_;
    ScreenInfo sc_info_;
    TextInfo text_info_[1024];
    uint32_t text_length_{0};
public:
    DX12UI(DX12Application& dx_app, std::string_view font_name, TextureManager& tex_mgr, ShaderManager& shd_mgr) : dx_app_(dx_app), tex_mgr_(tex_mgr), shd_mgr_(shd_mgr), res_mgr_(dx_app.GetRenderContext().GetGPUResourceManager()){
        LoadSDFCoords(font_name);
        auto sdf = tex_mgr_.get(std::format("{}_tex.png", font_name));
        auto vs = shd_mgr_.get("ui.vs");
        auto ps = shd_mgr_.get("ui.ps");
        sc_info_.width = dx_app_.GetRenderPresets().width;
        sc_info_.height = dx_app_.GetRenderPresets().height;
        if (!sdf.has_value() || !vs.has_value() || !ps.has_value()) {
            std::cout << "font assets not found" << std::endl;
            exit(EXIT_FAILURE);
        }
        D3D12_INPUT_ELEMENT_DESC ied[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        Pipeline& ui_pipeline = dx_app_.GetRenderContext().CreatePipeline("ui", { 4, 4, 4, dx_app_.GetRenderPresets().enable_msaa_4x });
        D3D12_INPUT_LAYOUT_DESC layout = { ied, _countof(ied) };
        auto texts_tex = res_mgr_.CreateTexture("texts_tex", sdf.value().width, sdf.value().height, sdf.value().data);
        auto text_vertices_res = res_mgr_.CreateVertexBuffer("text_vertices", vertices_.data(), vertices_.size());
        auto text_indices_res = res_mgr_.CreateIndexBuffer("text_indices", indices_.data(), indices_.size());
        auto screen_info = res_mgr_.CreateCBuffer("screen_info", sc_info_);
        auto text_info = res_mgr_.CreateCBuffer("text_info", text_info_, _countof(text_info_));
        auto sdf_coords = res_mgr_.CreateCBuffer("sdf_coords", coords_.data(), coords_.size());
        DescriptorHeap heap = ui_pipeline.GetDescriptorHeap();
        ui_pipeline.BindVertexBuffer(text_vertices_res);
        ui_pipeline.BindIndexBuffer(text_indices_res);
        ui_pipeline.BindStaticSampler(CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR));
        ui_pipeline.BindIALayout(layout);
        ui_pipeline.BindVertexShader(vs.value());
        ui_pipeline.BindFragmentShader(ps.value());
        heap.BindTextureAsSRV(texts_tex);
        heap.BindBufferAsSRV(sdf_coords);
        heap.BindBufferAsSRV(text_info);
        heap.BindBufferAsCBV(screen_info);
        ui_pipeline.SetDrawInstancesCount(text_length_);
        ui_pipeline.Build();
    }

    void UpdateUI() {
        Pipeline& ui_pipeline = dx_app_.GetRenderContext().SelectPipeline("ui");
        res_mgr_.ModifyCBuffer("text_info", text_info_, text_length_);
        ui_pipeline.SetDrawInstancesCount(text_length_);
        text_length_ = 0;
    }

    void LoadSDFCoords(std::string_view font_name) {
        FontLoader::ReadFontMeta(coords_, sc_info_.atlas_font_size, font_name);
        for (int i = 0; i < coords_.size(); ++i) {
            char_mapping_[coords_[i].character] = i;
        }
    }

    void DrawString(std::wstring_view str, uint32_t x, uint32_t y, uint32_t size) {
        uint32_t advance = 0;
        for (int i = 0; i < str.size(); ++i) {
            auto jumping = char_mapping_[str[i]];
            auto c = str[i];
            text_info_[text_length_].char_index = jumping;
            text_info_[text_length_].x = x + advance;
            text_info_[text_length_].y = y;
            text_info_[text_length_].size = size;
            advance += coords_[jumping].advance / (sc_info_.atlas_font_size / size);
            ++text_length_;
        }
    }
};