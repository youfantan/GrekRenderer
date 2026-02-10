#pragma once

#include <Windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <directx/d3dx12.h>
#include <DirectXMath.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "../win32/common.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class dx12_framework {
public:
    using dx12_inital_param_t = struct {
        uint32_t width;
        uint32_t height;
        HWND hwnd;
        bool enable_msaa_4x;
        float clear_color[4];
    };

    using pso_params_t = struct {
        D3D12_ROOT_PARAMETER* root_params;
        uint32_t root_params_sz;
        D3D12_INPUT_ELEMENT_DESC* ie_descs;
        uint32_t ie_descs_sz;
        ComPtr<ID3DBlob> vertex_shader;
        ComPtr<ID3DBlob> fragment_shader;
    };

private:
    dx12_inital_param_t param_;
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> cmd_queue_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    ComPtr<ID3D12CommandAllocator> cmd_alloc_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12Resource> buffers_[2];
    ComPtr<ID3D12Resource> msaa_rt_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> msaa_rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12Resource> vertex_buffer_;
    ComPtr<ID3D12Resource> vertex_upload_;
    ComPtr<ID3D12Resource> index_buffer_;
    ComPtr<ID3D12Resource> index_upload_;
    ComPtr<ID3D12Resource> constant_buffer_;
    ComPtr<ID3D12Resource> depth_buffer_;
    ComPtr<IDXGIAdapter> adapter_;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_;
    D3D12_INDEX_BUFFER_VIEW index_buffer_view_;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_lv_;
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12RootSignature> root_sig_;
    ComPtr<ID3D12Fence> fence_;
    uint32_t fb_index_;
    size_t rtv_size_;
    HANDLE fence_ev_;
    UINT64 fence_v_ = 0;
    bool cb_created_ = false;
public:
    dx12_framework(dx12_inital_param_t& p) : param_(p) {
        std::cout << "initializing directx12" << std::endl;
        CHECKHR(CreateDXGIFactory1(IID_PPV_ARGS(&factory_)));
        // Find GPU by performance
        CHECKHR(factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)));
        DXGI_ADAPTER_DESC desc;
        CHECKHR(adapter_->GetDesc(&desc));
        std::wcout << std::format(L"selected adapter {} with GPU memory {} MB", desc.Description, desc.DedicatedVideoMemory / 1024 / 1024) << std::endl;

#ifdef _DEBUG
        ComPtr<ID3D12Debug> debug_controller;
        CHECKHR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)));
        debug_controller->EnableDebugLayer();
#endif
        // Create DirectX12 device with feature level 11
        D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
#ifdef _DEBUG
        ComPtr<ID3D12InfoQueue> info_queue;
        CHECKHR(device_->QueryInterface(IID_PPV_ARGS(&info_queue)));
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

#endif
        // Query support for MSAA
        ms_lv_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ms_lv_.SampleCount = 4;
        ms_lv_.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        ms_lv_.NumQualityLevels = 0;
        CHECKHR(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms_lv_, sizeof(ms_lv_)));

        // Create a commmand queue with direct list
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&cmd_queue_));
        device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmd_alloc_));
        device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_alloc_.Get(), nullptr, IID_PPV_ARGS(&cmd_list_));

        // Create swapchain
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = 2; // double buffer
        sd.Width = p.width;
        sd.Height = p.height;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // format: RGBA32
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // use for render target
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // discard pixels when swap
        //if (p.enable_msaa_4x) sd.SampleDesc.Count = 4;
        sd.SampleDesc.Count = 1;
        sd.Scaling = DXGI_SCALING_NONE; // no scaling, just for debug

        // Create swapchain with lower level, then convert to higher level to support GetCurrentBackBufferIndex
        ComPtr<IDXGISwapChain1> sc;
        factory_->CreateSwapChainForHwnd(cmd_queue_.Get(), p.hwnd, &sd, nullptr, nullptr, &sc);
        sc.As(&swapchain_);
        fb_index_ = swapchain_->GetCurrentBackBufferIndex();

        // Create RTV Heap for swapchain buffers
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.NumDescriptors = 2;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap_));
        // Query RTV size
        rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create a RTV for each swapchain buffer
        auto handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < 2; ++i) {
            swapchain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i]));
            device_->CreateRenderTargetView(buffers_[i].Get(), nullptr, handle);
            handle.ptr += rtv_size_;
        }

        if (p.enable_msaa_4x) {
            auto msaa_rt_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto msaa_rt_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, p.width, p.height, 1, 1, 4);
            msaa_rt_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE msaa_clear_value = {};
            memcpy(&msaa_clear_value.Color, p.clear_color, sizeof(float) * 4);
            msaa_clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            device_->CreateCommittedResource(
                &msaa_rt_prop,
                D3D12_HEAP_FLAG_NONE,
                &msaa_rt_desc,
                D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                &msaa_clear_value,
                IID_PPV_ARGS(&msaa_rt_));
            D3D12_DESCRIPTOR_HEAP_DESC msaa_rtv_heap_desc{};
            msaa_rtv_heap_desc.NumDescriptors = 1;
            msaa_rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            device_->CreateDescriptorHeap(&msaa_rtv_heap_desc, IID_PPV_ARGS(msaa_rtv_heap_.GetAddressOf()));
            D3D12_RENDER_TARGET_VIEW_DESC msaa_rtv_desc;
            msaa_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            msaa_rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            device_->CreateRenderTargetView(msaa_rt_.Get(), &msaa_rtv_desc, msaa_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
        }

        // Create depth/stencil buffer
        D3D12_RESOURCE_DESC depth_heap_desc = {};
        depth_heap_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_heap_desc.Alignment = 0;
        depth_heap_desc.Width = p.width;
        depth_heap_desc.Height = p.height;
        depth_heap_desc.DepthOrArraySize = 1;
        depth_heap_desc.MipLevels = 1;
        depth_heap_desc.Format = DXGI_FORMAT_D32_FLOAT;
        depth_heap_desc.SampleDesc.Count = p.enable_msaa_4x ? 4 : 1;
        depth_heap_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_heap_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        auto depth_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_CLEAR_VALUE opt_clear = {};
        opt_clear.Format = DXGI_FORMAT_D32_FLOAT;
        opt_clear.DepthStencil.Depth = 1.0f;
        opt_clear.DepthStencil.Stencil = 0;

        device_->CreateCommittedResource(
            &depth_heap_prop,
            D3D12_HEAP_FLAG_NONE,
            &depth_heap_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &opt_clear,
            IID_PPV_ARGS(&depth_buffer_)
        );

        // Create DSV Heap for depth buffer
        D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {};
        dsv_desc.NumDescriptors = 1;
        dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device_->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap_));
        device_->CreateDepthStencilView(depth_buffer_.Get(), nullptr, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
        device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        fence_ev_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        cmd_list_->Close();
    }

    template<typename V>
    void apply_ia(V* vertices, size_t vertices_sz, void* indices, size_t indices_sz) {
        wait();
        vertex_buffer_.Reset();
        index_buffer_.Reset();
        auto vertex_heap_prop= CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto vertex_heap_desc = CD3DX12_RESOURCE_DESC::Buffer(vertices_sz);
        CHECKHR(device_->CreateCommittedResource(
            &vertex_heap_prop,
            D3D12_HEAP_FLAG_NONE,
            &vertex_heap_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&vertex_buffer_)));

        auto vertex_heap_upload_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto vertex_heap_upload_desc = CD3DX12_RESOURCE_DESC::Buffer(vertices_sz);

        device_->CreateCommittedResource(
            &vertex_heap_upload_prop,
            D3D12_HEAP_FLAG_NONE,
            &vertex_heap_upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertex_upload_));

        UINT8* vertex_mapping;
        CD3DX12_RANGE readRange(0, 0);
        vertex_upload_->Map(0, &readRange, reinterpret_cast<void**>(&vertex_mapping));
        memcpy(vertex_mapping, vertices, vertices_sz);
        vertex_upload_->Unmap(0, nullptr);

        vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
        vertex_buffer_view_.StrideInBytes = sizeof(V);
        vertex_buffer_view_.SizeInBytes = vertices_sz;

        auto index_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto index_heap_desc = CD3DX12_RESOURCE_DESC::Buffer(indices_sz);

        device_->CreateCommittedResource(
            &index_heap_prop,
            D3D12_HEAP_FLAG_NONE,
            &index_heap_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&index_buffer_));

        auto index_heap_upload_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto index_heap_upload_desc = CD3DX12_RESOURCE_DESC::Buffer(indices_sz);

        device_->CreateCommittedResource(
            &index_heap_upload_prop,
            D3D12_HEAP_FLAG_NONE,
            &index_heap_upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&index_upload_));

        UINT8* index_mapping;
        index_upload_->Map(0, &readRange, reinterpret_cast<void**>(&index_mapping));
        memcpy(index_mapping, indices, indices_sz);
        index_upload_->Unmap(0, nullptr);

        index_buffer_view_.BufferLocation = index_buffer_->GetGPUVirtualAddress();
        index_buffer_view_.SizeInBytes = indices_sz;
        index_buffer_view_.Format = DXGI_FORMAT_R16_UINT;
        cmd_alloc_->Reset();
        cmd_list_->Reset(cmd_alloc_.Get(), nullptr);
        D3D12_RESOURCE_BARRIER pre_copy[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                vertex_buffer_.Get(),
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COPY_DEST),

            CD3DX12_RESOURCE_BARRIER::Transition(
                index_buffer_.Get(),
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COPY_DEST)
        };
        cmd_list_->ResourceBarrier(2, pre_copy);
        cmd_list_->CopyResource(vertex_buffer_.Get(), vertex_upload_.Get());
        cmd_list_->CopyResource(index_buffer_.Get(), index_upload_.Get());

        D3D12_RESOURCE_BARRIER barriers[2];
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer_.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(index_buffer_.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);

        cmd_list_->ResourceBarrier(2, barriers);
        cmd_list_->Close();
        ID3D12CommandList* lists[] = { cmd_list_.Get() };
        cmd_queue_->ExecuteCommandLists(1, lists);
        wait();
        vertex_upload_.Reset();
        index_upload_.Reset();
    }

    template<typename C>
    void apply_cb(C* c) {
        if (!cb_created_) {
            if constexpr (sizeof(C) % 256 != 0) {
                static_assert("constant buffer not satisfied 256 bytes padding");
            }
            auto constant_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto constant_heap_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(C));

            device_->CreateCommittedResource(
                &constant_heap_prop,
                D3D12_HEAP_FLAG_NONE,
                &constant_heap_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&constant_buffer_));
            cb_created_ = true;
        }
        UINT8* cb_mapping;
        constant_buffer_->Map(0, nullptr, reinterpret_cast<void**>(&cb_mapping));
        memcpy(cb_mapping, c, sizeof(C));
        constant_buffer_->Unmap(0, nullptr);
    }

    void apply_pso(const pso_params_t& p) {
        CD3DX12_ROOT_SIGNATURE_DESC rs{};
        rs.Init(p.root_params_sz, p.root_params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> rsBlob, errBlob;
        D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &errBlob);
        device_->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&root_sig_));
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.InputLayout = { p.ie_descs, p.ie_descs_sz};
        pso.pRootSignature = root_sig_.Get();
        pso.VS = {p.vertex_shader->GetBufferPointer() , p.vertex_shader->GetBufferSize() };
        pso.PS = { p.fragment_shader->GetBufferPointer(), p.fragment_shader->GetBufferSize() };
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.SampleDesc.Count = param_.enable_msaa_4x ? 4 : 1;
        pso.SampleDesc.Quality = 0;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pso_));
    }

    void wait() {
        const UINT64 fence = fence_v_;
        cmd_queue_->Signal(fence_.Get(), fence);
        fence_v_++;

        if (fence_->GetCompletedValue() < fence) {
            fence_->SetEventOnCompletion(fence, fence_ev_);
            WaitForSingleObject(fence_ev_, INFINITE);
        }
    }

    void render() {
        wait(); // Wait for GPU processing and reset command list
        cmd_alloc_->Reset();
        cmd_list_->Reset(cmd_alloc_.Get(), pso_.Get());

        // Set viewport and scissor rect
        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)param_.width, (float)param_.height, 0.0f, 1.0f };
        D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(param_.width), static_cast<LONG>(param_.height) };
        cmd_list_->RSSetViewports(1, &viewport);
        cmd_list_->RSSetScissorRects(1, &scissor_rect);

        if (param_.enable_msaa_4x) {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(msaa_rt_.Get(),
    D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd_list_->ResourceBarrier(1, &barrier);
        } else {
            // Set backbuffer to Render Target
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                buffers_[fb_index_].Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd_list_->ResourceBarrier(1, &barrier);
        }

        // Offset and get descriptor
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += fb_index_ * rtv_size_;
        cmd_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        if (param_.enable_msaa_4x) {
            D3D12_CPU_DESCRIPTOR_HANDLE msaa_rtv = msaa_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
            cmd_list_->OMSetRenderTargets(1, &msaa_rtv, FALSE, &dsv);
            cmd_list_->ClearRenderTargetView(msaa_rtv, param_.clear_color, 0, nullptr);
        } else {
            // Clear Render Target
            cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
            cmd_list_->ClearRenderTargetView(rtv, param_.clear_color, 0, nullptr);
        }

        cmd_list_->SetGraphicsRootSignature(root_sig_.Get());
        cmd_list_->SetGraphicsRootConstantBufferView(0, constant_buffer_->GetGPUVirtualAddress());
        // Use triangle Topology
        cmd_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // Load vertices and indices
        cmd_list_->IASetVertexBuffers(0, 1, &vertex_buffer_view_);
        cmd_list_->IASetIndexBuffer(&index_buffer_view_);
        // Draw call
        cmd_list_->DrawIndexedInstanced(36, 1, 0, 0, 0);
        if (param_.enable_msaa_4x) {
            D3D12_RESOURCE_BARRIER barriers[2] = {
                CD3DX12_RESOURCE_BARRIER::Transition(
                    msaa_rt_.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(
                    buffers_[fb_index_].Get(),
                    D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_RESOLVE_DEST)
                };
            cmd_list_->ResourceBarrier(2, barriers);
            cmd_list_->ResolveSubresource(buffers_[fb_index_].Get(), 0, msaa_rt_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                buffers_[fb_index_].Get(),
                D3D12_RESOURCE_STATE_RESOLVE_DEST,
                D3D12_RESOURCE_STATE_PRESENT);
            cmd_list_->ResourceBarrier(1, &barrier);
        } else {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                buffers_[fb_index_].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT);
            cmd_list_->ResourceBarrier(1, &barrier);
        }
        cmd_list_->Close();
        ID3D12CommandList* lists[] = { cmd_list_.Get() };
        // Execute and present
        cmd_queue_->ExecuteCommandLists(1, lists);
        swapchain_->Present(1, 0);
        fb_index_ = swapchain_->GetCurrentBackBufferIndex();
    }

    ComPtr<ID3D12Device> device() {
        return device_;
    }


};

class shader_manager {
    enum class shader_type {
        VERTEX_SHADER,
        FRAGMENT_SHADER,
        UNKNOWN_SHADER,
    };
    struct shader_info {
        shader_type typ;
        std::string hlsl_typ_ver;
        ComPtr<ID3DBlob> blob;
    };

    std::map<std::string, shader_info> shaders_map_;

    std::optional<ComPtr<ID3DBlob>> compile_shader(std::filesystem::path shader_path, std::string& hlsl_typ_ver) {
        std::ifstream shader_file(shader_path, std::ios::binary | std::ios::in);
        shader_file.seekg(0, std::ios::end);
        size_t sz = shader_file.tellg();
        shader_file.seekg(0);
        char* shader_content = static_cast<char*>(malloc(sz + 1));
        shader_file.read(shader_content, sz);
        shader_content[sz] = 0;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> err;
        HRESULT r = D3DCompile(shader_content, sz, nullptr, nullptr, nullptr, "main", hlsl_typ_ver.c_str(), 0, 0, &blob, &err);
        free(shader_content);
        if (!SUCCEEDED(r)) {
            std::cout << static_cast<char*>(err->GetBufferPointer()) << std::endl;
            return std::nullopt;
        }
        std::ofstream cache_file("shaders/cache/" + shader_path.filename().string(), std::ios::binary | std::ios::out);
        cache_file.write(static_cast<const char*>(blob->GetBufferPointer()), blob->GetBufferSize());
        return blob;
    }

    std::optional<ComPtr<ID3DBlob>> load_shader_cache(std::filesystem::path cache) {
        std::ifstream shader_file(cache, std::ios::binary | std::ios::in);
        shader_file.seekg(0, std::ios::end);
        size_t sz = shader_file.tellg();
        shader_file.seekg(0);
        char* shader_content = static_cast<char*>(malloc(sz));
        shader_file.read(shader_content, sz);
        ComPtr<ID3DBlob> blob;
        HRESULT r = D3DCreateBlob(sz, &blob);
        if (!SUCCEEDED(r)) {
            free(shader_content);
            return std::nullopt;
        }
        memcpy(blob->GetBufferPointer(), shader_content, sz);
        free(shader_content);
        return blob;
    }
public:
    void load_shaders(bool force_recompile = true) {
        std::filesystem::directory_entry shaders("shaders");
        std::filesystem::directory_entry cache("shaders/cache/");
        if (!shaders.exists()) {
            std::cout << "shaders not exists, exit" << std::endl;
            exit(EXIT_FAILURE);
        }
        if (!cache.exists()) {
            std::filesystem::create_directory(cache);
        }
        for (auto& shader : std::filesystem::directory_iterator(shaders)) {
            if (shader.is_regular_file()) {
                std::cout << std::format("loading vertex shader {}. ", shader.path().filename().string());
                std::filesystem::directory_entry shader_cache("shaders/cache/" + shader.path().filename().string());
                shader_type typ;
                std::string hlsl_typ_ver;
                if (shader.path().string().ends_with(".vs")) {
                    typ = shader_type::VERTEX_SHADER;
                    hlsl_typ_ver = "vs_5_0";
                } else if (shader.path().string().ends_with(".ps")) {
                    typ = shader_type::FRAGMENT_SHADER;
                    hlsl_typ_ver = "ps_5_0";
                } else {
                    typ = shader_type::UNKNOWN_SHADER;
                    hlsl_typ_ver = "vs_5_0";
                }
                if (shader_cache.exists() && !force_recompile) {
                    std::cout << "shader compiled yet, using cache" << std::endl;
                    auto blob = load_shader_cache(shader_cache);
                    if (!blob.has_value()) {
                        std::cout << std::format("failed to load vertex shader cache {}, exit", shader_cache.path().filename().string()) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    shaders_map_[shader.path().filename().string()] = {
                        .typ = typ,
                        .hlsl_typ_ver = hlsl_typ_ver,
                        .blob = blob.value(),
                    };
                } else {
                    std::cout << "shader not compiled yet, compiling" << std::endl;
                    auto blob = compile_shader(shader.path(), hlsl_typ_ver);
                    if (!blob.has_value()) {
                        std::cout << std::format("failed to compile vertex shader {}, exit", shader.path().filename().string()) << std::endl;
                        exit(EXIT_FAILURE);
                    }
                    shaders_map_[shader.path().filename().string()] = {
                        .typ = typ,
                        .hlsl_typ_ver = hlsl_typ_ver,
                        .blob = blob.value(),
                    };
                }

            }
        }
    }

    std::optional<ComPtr<ID3DBlob>> get(const std::string& shader_name) {
        if (shaders_map_.contains(shader_name)) {
            return shaders_map_[shader_name].blob;
        }
        return std::nullopt;
    }
};