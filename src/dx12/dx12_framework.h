#pragma once
#define NOMINMAX

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
#include <limits>
#include <stb_image.h>
#include <unordered_map>

#include "../win32/common.h"
#include "../win32/window.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX;

class GPUFence {
private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12Fence> fence_;
    uint64_t fence_value_ = 0;
    HANDLE fence_event_;
public:
    GPUFence() {}

    void Initialize(ComPtr<ID3D12Device> device) {
        device_ = device;
        device_->CreateFence(fence_value_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        fence_event_ = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    }

    void Insert(ComPtr<ID3D12CommandQueue> queue) {
        ++fence_value_;
        queue->Signal(fence_.Get(), fence_value_);
    }

    void Wait() {
        if (fence_->GetCompletedValue() < fence_value_)
        {
            fence_->SetEventOnCompletion(fence_value_, fence_event_);
            WaitForSingleObject(fence_event_, INFINITE);
        }
    }

    uint64_t GetValue() {
        return fence_value_;
    }

    ~GPUFence() {
        CloseHandle(fence_event_);
        fence_.Reset();
    }

};

class GPUResourceManager {
public:
    enum class gpu_resource_type {
        TEXTURE,
        VERTEX_BUFFER,
        INDEX_BUFFER,
        CONST_BUFFER,
    };

    using gpu_resource_t = struct {
        ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state;
        D3D12_HEAP_TYPE type;
        uint64_t fence_value;
        uint64_t size;
        void* ext_info;
    };

    using gpu_resource_handle_t = struct {
        gpu_resource_type typ;
        std::string id;
        gpu_resource_t* ptr; // only for quick access
    };


private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> copy_queue_;
    ComPtr<ID3D12GraphicsCommandList> copy_list_;
    ComPtr<ID3D12CommandAllocator> copy_alloc_;
    ComPtr<ID3D12CommandQueue> render_queue_;
    ComPtr<ID3D12GraphicsCommandList> render_list_;
    ComPtr<ID3D12CommandAllocator> render_alloc_;
    GPUFence io_fence_;
    std::vector<gpu_resource_t> temporary_resourcs_;
    std::unordered_map<std::string, gpu_resource_t> resources_map_;
public:
    GPUResourceManager() {}

    void Initialize(ComPtr<ID3D12Device> device, ComPtr<ID3D12CommandQueue>& render_queue) {
        device_ = device;
        render_queue_ = render_queue;
        io_fence_.Initialize(device_);
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        CHECKHR(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&copy_queue_)));
        copy_queue_->SetName(L"Copy Queue");
        CHECKHR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copy_alloc_)));
        CHECKHR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copy_alloc_.Get(), nullptr, IID_PPV_ARGS(&copy_list_)));
        CHECKHR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&render_alloc_)));
        CHECKHR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, render_alloc_.Get(), nullptr, IID_PPV_ARGS(&render_list_)));
        copy_list_->Close();
    }

    ComPtr<ID3D12Resource> CreateUploadHeap(size_t size) {
        ComPtr<ID3D12Resource> resource;
        auto vertex_heap_upload_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto vertex_heap_upload_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        CHECKHR(device_->CreateCommittedResource(
            &vertex_heap_upload_prop,
            D3D12_HEAP_FLAG_NONE,
            &vertex_heap_upload_desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource)));
        resource->SetName(L"Upload Buffer");
        return resource;
    }

    gpu_resource_handle_t CreateTexture(const std::string& res_id, uint32_t width, uint32_t height, uint8_t* tex_data) {
        ComPtr<ID3D12Resource> tex_buffer;
        D3D12_RESOURCE_DESC tex_desc {};
        tex_desc.MipLevels = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.Width = width;
        tex_desc.Height = height;
        tex_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        tex_desc.DepthOrArraySize = 1;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.SampleDesc.Quality = 0;
        tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        D3D12_HEAP_PROPERTIES tex_prop = CD3DX12_HEAP_PROPERTIES((D3D12_HEAP_TYPE_DEFAULT));
        CHECKHR(device_->CreateCommittedResource(
            &tex_prop,
            D3D12_HEAP_FLAG_NONE,
            &tex_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&tex_buffer)));
        auto wres_id = string_to_wstring(res_id);
        tex_buffer->SetName(wres_id.value().c_str());
        const UINT64 buffer_size = GetRequiredIntermediateSize(tex_buffer.Get(), 0, 1);
        ComPtr<ID3D12Resource> tex_upload = CreateUploadHeap(buffer_size);
        io_fence_.Wait();
        CHECKHR(copy_alloc_->Reset());
        CHECKHR(copy_list_->Reset(copy_alloc_.Get(), nullptr));
        D3D12_SUBRESOURCE_DATA tex_upload_desc = {};
        tex_upload_desc.pData = tex_data;
        tex_upload_desc.RowPitch = width * 4;
        tex_upload_desc.SlicePitch = tex_upload_desc.RowPitch * height;
        UpdateSubresources(copy_list_.Get(), tex_buffer.Get(), tex_upload.Get(), 0, 0, 1, &tex_upload_desc);
        CHECKHR(copy_list_->Close());
        ID3D12CommandList* lists[] = { copy_list_.Get() };
        copy_queue_->ExecuteCommandLists(1, lists);
        io_fence_.Insert(copy_queue_);
        DXGI_FORMAT* fmt = new DXGI_FORMAT(tex_desc.Format);
        gpu_resource_t tex_res = {
            .res = tex_buffer,
            .state = D3D12_RESOURCE_STATE_COMMON,
            .type = D3D12_HEAP_TYPE_DEFAULT,
            .fence_value = std::numeric_limits<uint64_t>::max(),
            .size = buffer_size,
            .ext_info = fmt,
        };
        gpu_resource_t tex_upload_res = {
            .res = tex_upload,
            .state = D3D12_RESOURCE_STATE_GENERIC_READ,
            .type = D3D12_HEAP_TYPE_UPLOAD,
            .fence_value = io_fence_.GetValue(),
            .size = buffer_size,
            .ext_info = nullptr,
        };
        resources_map_[res_id] = tex_res;
        temporary_resourcs_.push_back(tex_upload_res);
        gpu_resource_handle_t handle {
            .typ = gpu_resource_type::TEXTURE,
            .id = res_id,
            .ptr = &resources_map_[res_id],
        };
        return handle;
    }

    template<typename V>
    gpu_resource_handle_t CreateVertexBuffer(const std::string& res_id, const V* vertices, const uint32_t elem_size) {
        ComPtr<ID3D12Resource> vertex_buffer;
        uint32_t size_in_bytes = elem_size * sizeof(V);
        auto vertex_heap_prop= CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto vertex_heap_desc = CD3DX12_RESOURCE_DESC::Buffer(size_in_bytes);
        CHECKHR(device_->CreateCommittedResource(
            &vertex_heap_prop,
            D3D12_HEAP_FLAG_NONE,
            &vertex_heap_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&vertex_buffer)));
        auto wres_id = string_to_wstring(res_id);
        vertex_buffer->SetName(wres_id.value().c_str());
        ComPtr<ID3D12Resource> vertex_upload = CreateUploadHeap(size_in_bytes);
        uint8_t* vertex_mapping;
        CD3DX12_RANGE range(0, 0);
        CHECKHR(vertex_upload->Map(0, &range, reinterpret_cast<void**>(&vertex_mapping)));
        memcpy(vertex_mapping, vertices, size_in_bytes);
        vertex_upload->Unmap(0, nullptr);
        io_fence_.Wait();
        CHECKHR(copy_alloc_->Reset());
        CHECKHR(copy_list_->Reset(copy_alloc_.Get(), nullptr));
        copy_list_->CopyResource(vertex_buffer.Get(), vertex_upload.Get());
        CHECKHR(copy_list_->Close());
        ID3D12CommandList* lists[] = { copy_list_.Get() };
        copy_queue_->ExecuteCommandLists(1, lists);
        io_fence_.Insert(copy_queue_);
        uint32_t* stride = new uint32_t(sizeof(V));
        gpu_resource_t vertex_res = {
            .res = vertex_buffer,
            .state = D3D12_RESOURCE_STATE_COMMON,
            .type = D3D12_HEAP_TYPE_DEFAULT,
            .fence_value = std::numeric_limits<uint64_t>::max(),
            .size = size_in_bytes,
            .ext_info = stride,
        };
        gpu_resource_t vertex_upload_res = {
            .res = vertex_upload,
            .state = D3D12_RESOURCE_STATE_GENERIC_READ,
            .type = D3D12_HEAP_TYPE_UPLOAD,
            .fence_value = io_fence_.GetValue(),
            .size = size_in_bytes,
            .ext_info = nullptr,
        };
        resources_map_[res_id] = vertex_res;
        temporary_resourcs_.push_back(vertex_upload_res);
        gpu_resource_handle_t handle {
            .typ = gpu_resource_type::VERTEX_BUFFER,
            .id = res_id,
            .ptr = &resources_map_[res_id],
        };
        return handle;
    }

    gpu_resource_handle_t CreateIndexBuffer(const std::string& res_id, const uint32_t* indices, const uint32_t elem_size) {
        uint32_t size_in_bytes = elem_size * sizeof(uint32_t);
        ComPtr<ID3D12Resource> index_buffer;
        auto index_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto index_heap_desc = CD3DX12_RESOURCE_DESC::Buffer(size_in_bytes);
        CHECKHR(device_->CreateCommittedResource(
            &index_heap_prop,
            D3D12_HEAP_FLAG_NONE,
            &index_heap_desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&index_buffer)));
        auto wres_id = string_to_wstring(res_id);
        index_buffer->SetName(wres_id.value().c_str());
        ComPtr<ID3D12Resource> index_upload = CreateUploadHeap(size_in_bytes);
        uint8_t* index_mapping;
        CD3DX12_RANGE range(0, 0);
        CHECKHR(index_upload->Map(0, &range, reinterpret_cast<void**>(&index_mapping)));
        memcpy(index_mapping, indices, size_in_bytes);
        index_upload->Unmap(0, nullptr);

        io_fence_.Wait();
        CHECKHR(copy_alloc_->Reset());
        CHECKHR(copy_list_->Reset(copy_alloc_.Get(), nullptr));
        copy_list_->CopyResource(index_buffer.Get(), index_upload.Get());
        CHECKHR(copy_list_->Close());
        ID3D12CommandList* lists[] = { copy_list_.Get() };
        copy_queue_->ExecuteCommandLists(1, lists);
        io_fence_.Insert(copy_queue_);
        gpu_resource_t index_res = {
            .res = index_buffer,
            .state = D3D12_RESOURCE_STATE_COMMON,
            .type = D3D12_HEAP_TYPE_DEFAULT,
            .fence_value = std::numeric_limits<uint64_t>::max(),
            .size = size_in_bytes,
            .ext_info = nullptr,
        };
        gpu_resource_t index_upload_res = {
            .res = index_upload ,
            .state = D3D12_RESOURCE_STATE_GENERIC_READ,
            .type = D3D12_HEAP_TYPE_UPLOAD,
            .fence_value = io_fence_.GetValue(),
            .size = size_in_bytes,
            .ext_info = nullptr,
        };
        resources_map_[res_id] = index_res;
        temporary_resourcs_.push_back(index_upload_res);
        gpu_resource_handle_t handle {
            .typ = gpu_resource_type::INDEX_BUFFER,
            .id = res_id,
            .ptr = &resources_map_[res_id],
        };
        return handle;
    }

    template<typename C>
    gpu_resource_handle_t CreateCBuffer(const std::string& res_id, const C& data) {
        ComPtr<ID3D12Resource> cbuffer = CreateUploadHeap(sizeof(C));
        uint32_t* stride = new uint32_t(sizeof(C));
        auto wres_id = string_to_wstring(res_id);
        cbuffer->SetName(wres_id.value().c_str());
        gpu_resource_t cbuffer_res = {
            .res = cbuffer,
            .state = D3D12_RESOURCE_STATE_COMMON,
            .type = D3D12_HEAP_TYPE_UPLOAD,
            .fence_value = std::numeric_limits<uint64_t>::max(),
            .size = sizeof(C),
            .ext_info = stride,
        };
        resources_map_[res_id] = cbuffer_res;
        ModifyCBuffer(res_id, data);
        gpu_resource_handle_t handle {
            .typ = gpu_resource_type::CONST_BUFFER,
            .id = res_id,
            .ptr = &resources_map_[res_id],
        };
        return handle;
    }

    template<typename C>
    bool ModifyCBuffer(const std::string& res_id, const C& data) {
        if (!resources_map_.contains(res_id)) {
            return false;
        }
        ComPtr<ID3D12Resource> buffer = resources_map_[res_id].res;
        uint8_t* mapping_;
        CHECKHR(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapping_)));
        memcpy(mapping_, reinterpret_cast<const void*>(&data), sizeof(C));
        buffer->Unmap(0, nullptr);
        return true;
    }

    template<typename C>
    gpu_resource_handle_t CreateCBuffer(const std::string& res_id, const C* data_array, const uint32_t count) {
        auto buffer = CreateUploadHeap(sizeof(C) * count);
        uint32_t* stride = new uint32_t(sizeof(C));
        gpu_resource_t cbuffer_res = {
            .res = buffer,
            .state = D3D12_RESOURCE_STATE_COMMON,
            .type = D3D12_HEAP_TYPE_UPLOAD,
            .fence_value = std::numeric_limits<uint64_t>::max(),
            .size = sizeof(C) * count,
            .ext_info = stride,
        };
        resources_map_[res_id] = cbuffer_res;
        ModifyCBuffer(res_id, data_array, count);
        gpu_resource_handle_t handle {
            .typ = gpu_resource_type::CONST_BUFFER,
            .id = res_id,
            .ptr = &resources_map_[res_id],
        };
        return handle;
    }

    template<typename C>
    bool ModifyCBuffer(const std::string& res_id, const C* data_array, const uint32_t count) {
        if (!resources_map_.contains(res_id)) {
            return false;
        }
        ComPtr<ID3D12Resource> buffer = resources_map_[res_id].res;
        uint8_t* mapping_;
        CHECKHR(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapping_)));
        memcpy(mapping_, reinterpret_cast<const void*>(data_array), sizeof(C) * count);
        buffer->Unmap(0, nullptr);
        return true;
    }

    void DeferredRelease() {
        uint64_t completedValue = io_fence_.GetValue();
        for (auto it = temporary_resourcs_.begin(); it != temporary_resourcs_.end();) {
            if (it->fence_value <= completedValue) {
                std::cout << "deferred released an upload buffer" << std::endl;
                it->res.Reset();
                it = temporary_resourcs_.erase(it);
            } else {
                ++it;
            }
        }
    }

    ~GPUResourceManager() {
        io_fence_.Wait();
    }
};

class DescriptorHeap {
private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12DescriptorHeap> heap_;
    uint32_t srv_count_;
    uint32_t cbv_count_;
    uint32_t uav_count_;
    uint32_t descriptor_size_;
    uint32_t srv_off_;
    uint32_t cbv_off_;
    uint32_t uav_off_;
public:
    DescriptorHeap(ComPtr<ID3D12Device>& device, uint32_t srv_count, uint32_t cbv_count, uint32_t uav_count) : device_(device), srv_count_(srv_count), cbv_count_(cbv_count), uav_count_(uav_count) {
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
        srv_heap_desc.NumDescriptors = srv_count_ * 3;
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        CHECKHR(device_->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&heap_)));
        descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        srv_off_ = 0;
        cbv_off_ = 0 + srv_count;
        uav_off_ = 0 + srv_count + cbv_count;
    }
    /* Bind a descriptor for texture to the descriptor heap then returns the binding register number */
    uint32_t BindTextureAsSRV(const GPUResourceManager::gpu_resource_handle_t& hres) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = *static_cast<DXGI_FORMAT*>(hres.ptr->ext_info);
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += srv_off_ * descriptor_size_;
        device_->CreateShaderResourceView(hres.ptr->res.Get(), &srv_desc, handle);
        uint32_t bind_reg = srv_off_;
        ++srv_off_;
        return bind_reg;
    }

    uint32_t BindBufferAsSRV(const GPUResourceManager::gpu_resource_handle_t& hres) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.StructureByteStride = *reinterpret_cast<uint32_t*>(hres.ptr->ext_info);
        srv_desc.Buffer.NumElements = hres.ptr->size / srv_desc.Buffer.StructureByteStride;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += srv_off_ * descriptor_size_;
        device_->CreateShaderResourceView(hres.ptr->res.Get(), &srv_desc, handle);
        uint32_t bind_reg = srv_off_;
        ++srv_off_;
        return bind_reg;
    }

    uint32_t BindBufferAsCBV(const GPUResourceManager::gpu_resource_handle_t& hres) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
        cbv_desc.BufferLocation = hres.ptr->res->GetGPUVirtualAddress();
        cbv_desc.SizeInBytes = hres.ptr->size;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += cbv_off_ * descriptor_size_;
        device_->CreateConstantBufferView(&cbv_desc, handle);
        uint32_t bind_reg = cbv_off_ - srv_count_;
        ++cbv_off_;
        return bind_reg;
    }

    ComPtr<ID3D12DescriptorHeap> GetHeap() {
        return heap_;
    }

    uint32_t GetDescriptorSize() {
        return descriptor_size_;
    }

    uint32_t GetSRVCount() const {
        return srv_count_;
    }

    uint32_t GetCBVCount() const {
        return cbv_count_;
    }

    uint32_t GetUAVCount() const {
        return uav_count_;
    }

    uint32_t GetAllDescriptorsCount() const {
        return srv_count_ + cbv_count_ + uav_count_;
    }
};

class RootSignature {
private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12RootSignature> signature_;
    std::vector<CD3DX12_ROOT_PARAMETER> root_params_;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers_;
    CD3DX12_DESCRIPTOR_RANGE heap_range_[3]{};
public:
    explicit RootSignature(ComPtr<ID3D12Device>& device) : device_(device) {}

    void BindHeap(const DescriptorHeap& heap) {
        heap_range_[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, heap.GetSRVCount(), 0);
        heap_range_[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, heap.GetCBVCount(), 0);
        heap_range_[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, heap.GetUAVCount(), 0);
        CD3DX12_ROOT_PARAMETER p[3]{};
        p[0].InitAsDescriptorTable(1, &heap_range_[0], D3D12_SHADER_VISIBILITY_ALL);
        p[1].InitAsDescriptorTable(1, &heap_range_[1], D3D12_SHADER_VISIBILITY_ALL);
        p[2].InitAsDescriptorTable(1, &heap_range_[2], D3D12_SHADER_VISIBILITY_ALL);
        root_params_.push_back(p[0]);
        root_params_.push_back(p[1]);
        root_params_.push_back(p[2]);
    }

    void BindStaticSampler(const D3D12_STATIC_SAMPLER_DESC& desc) {
        samplers_.push_back(desc);
    }

    void Build() {
        CD3DX12_ROOT_SIGNATURE_DESC desc{};
        desc.Init(root_params_.size(), root_params_.data(), samplers_.size(), samplers_.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> rs_blob, err_blob;
        CHECKHR(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &err_blob));
        CHECKHR(device_->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&signature_)));
    }

    ComPtr<ID3D12RootSignature> GetRootSignature() {
        return signature_;
    }
};

class Pipeline {
public:
    enum class render_binding_type {
        CBV_HEAP,
        SRV_HEAP,
        UAV_HEAP
    };

    using pipeline_init_t = struct {
        uint32_t srv_heap_size;
        uint32_t cbv_heap_size;
        uint32_t uav_heap_size;
        bool enable_msaa_4x;
    };

private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12PipelineState> pso_;
    RootSignature signature_;
    D3D12_INPUT_LAYOUT_DESC layout_;
    ComPtr<ID3DBlob> vs_;
    ComPtr<ID3DBlob> ps_;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_;
    D3D12_INDEX_BUFFER_VIEW index_buffer_view_;
    DescriptorHeap heap_;
    const pipeline_init_t init_;
    uint32_t draw_instances_{0};
public:
    Pipeline(ComPtr<ID3D12Device>& device, const pipeline_init_t& init) : device_(device), init_(init), signature_(device), heap_(device, init.srv_heap_size, init.cbv_heap_size, init.uav_heap_size){}
    Pipeline(Pipeline&) = delete;
    Pipeline(Pipeline&& p) = default;
    void BindStaticSampler(const D3D12_STATIC_SAMPLER_DESC& desc) {
        signature_.BindStaticSampler(desc);
    }

    void BindIALayout(D3D12_INPUT_LAYOUT_DESC desc) {
        layout_ = desc;
    }

    void BindVertexBuffer(GPUResourceManager::gpu_resource_handle_t& hres) {
        vertex_buffer_view_.BufferLocation = hres.ptr->res->GetGPUVirtualAddress();
        vertex_buffer_view_.StrideInBytes = *reinterpret_cast<uint32_t*>(hres.ptr->ext_info);
        vertex_buffer_view_.SizeInBytes = hres.ptr->size;
    }

    void BindIndexBuffer(GPUResourceManager::gpu_resource_handle_t& hres) {
        index_buffer_view_.BufferLocation = hres.ptr->res->GetGPUVirtualAddress();
        index_buffer_view_.SizeInBytes = hres.ptr->size;
        index_buffer_view_.Format = DXGI_FORMAT_R32_UINT;
    }

    void BindVertexShader(ComPtr<ID3DBlob> vs) {
        vs_ = vs;
    }
    void BindFragmentShader(ComPtr<ID3DBlob> ps) {
        ps_ = ps;
    }

    void Build() {
        signature_.BindHeap(heap_);
        signature_.Build();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.InputLayout = layout_;
        pso.pRootSignature = signature_.GetRootSignature().Get();
        pso.VS = {vs_->GetBufferPointer() , vs_->GetBufferSize() };
        pso.PS = { ps_->GetBufferPointer(), ps_->GetBufferSize() };
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.SampleDesc.Count = init_.enable_msaa_4x ? 4 : 1;
        pso.SampleDesc.Quality = 0;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        CHECKHR(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pso_)));
    }

    void SetDrawInstancesCount(uint32_t count) {
        draw_instances_ = count;
    }

    ComPtr<ID3D12PipelineState> GetPSO() {
        return pso_;
    }

    ComPtr<ID3D12RootSignature> GetRootSignature() {
        return signature_.GetRootSignature();
    }

    D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() {
        return vertex_buffer_view_;
    }

    D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() {
        return index_buffer_view_;
    }

    uint32_t GetInstanceIndicesCount() {
        return index_buffer_view_.SizeInBytes / sizeof(uint32_t);
    }

    uint32_t GetDrawInstancesCount() {
        return draw_instances_;
    }

    DescriptorHeap& GetDescriptorHeap() {
        return heap_;
    }
};

class RenderContext {
public:
    using rendering_presets = struct {
        uint32_t width;
        uint32_t height;
        bool enable_msaa_4x;
        float clear_color[4];
        HWND hwnd;
    };
private:
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter> adapter_;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_lv_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> render_queue_;
    ComPtr<ID3D12CommandAllocator> render_alloc_;
    ComPtr<ID3D12GraphicsCommandList> render_list_;
    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12Resource> buffers_[2];
    ComPtr<ID3D12Resource> msaa_rt_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> msaa_rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    ComPtr<ID3D12Resource> depth_buffer_;

    uint32_t fb_index_;
    size_t rtv_size_;
    size_t dsv_size_;

    GPUFence graphics_fence_;
    std::unordered_map<std::string, Pipeline> pipelines_;
    const rendering_presets& presets_;
    GPUResourceManager gpu_resource_mgr_;
public:
    RenderContext(const rendering_presets& presets): presets_(presets) {}

    void Initialize() {
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

        // Create swapchain
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = 2; // double buffer
        sd.Width = presets_.width;
        sd.Height = presets_.height;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // format: RGBA32
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // use for render target
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // discard pixels when swap
        //if (p.enable_msaa_4x) sd.SampleDesc.Count = 4;
        sd.SampleDesc.Count = 1;
        sd.Scaling = DXGI_SCALING_NONE; // no scaling, just for debug

        // Create a commmand queue with direct list
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CHECKHR(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&render_queue_)));
        render_queue_->SetName(L"Render Queue");
        CHECKHR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&render_alloc_)));
        CHECKHR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, render_alloc_.Get(), nullptr, IID_PPV_ARGS(&render_list_)));

        gpu_resource_mgr_.Initialize(device_, render_queue_);
        graphics_fence_.Initialize(device_);

        // Create swapchain with lower level, then convert to higher level to support GetCurrentBackBufferIndex
        ComPtr<IDXGISwapChain1> sc;
        CHECKHR(factory_->CreateSwapChainForHwnd(render_queue_.Get(), presets_.hwnd, &sd, nullptr, nullptr, &sc));
        CHECKHR(sc.As(&swapchain_));
        fb_index_ = swapchain_->GetCurrentBackBufferIndex();

        // Create RTV Heap for swapchain buffers
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
        rtv_heap_desc.NumDescriptors = 2;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        CHECKHR(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_)));
        // Query RTV size
        rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create a RTV for each swapchain buffer
        auto handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < 2; ++i) {
            CHECKHR(swapchain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])));
            buffers_[i]->SetName(L"Back Buffer");
            device_->CreateRenderTargetView(buffers_[i].Get(), nullptr, handle);
            handle.ptr += rtv_size_;
        }

        if (presets_.enable_msaa_4x) {
            auto msaa_rt_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto msaa_rt_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, presets_.width, presets_.height, 1, 1, 4);
            msaa_rt_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE msaa_clear_value = {};
            memcpy(&msaa_clear_value.Color, presets_.clear_color, sizeof(float) * 4);
            msaa_clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            CHECKHR(device_->CreateCommittedResource(
                &msaa_rt_prop,
                D3D12_HEAP_FLAG_NONE,
                &msaa_rt_desc,
                D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                &msaa_clear_value,
                IID_PPV_ARGS(&msaa_rt_)));
            msaa_rt_->SetName(L"MSAA RenderTarget");
            D3D12_DESCRIPTOR_HEAP_DESC msaa_rtv_heap_desc{};
            msaa_rtv_heap_desc.NumDescriptors = 1;
            msaa_rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            CHECKHR(device_->CreateDescriptorHeap(&msaa_rtv_heap_desc, IID_PPV_ARGS(msaa_rtv_heap_.GetAddressOf())));
            D3D12_RENDER_TARGET_VIEW_DESC msaa_rtv_desc;
            msaa_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            msaa_rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            device_->CreateRenderTargetView(msaa_rt_.Get(), &msaa_rtv_desc, msaa_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
        }

        // Create depth/stencil buffer
        D3D12_RESOURCE_DESC depth_heap_desc = {};
        depth_heap_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_heap_desc.Alignment = 0;
        depth_heap_desc.Width = presets_.width;
        depth_heap_desc.Height = presets_.height;
        depth_heap_desc.DepthOrArraySize = 1;
        depth_heap_desc.MipLevels = 1;
        depth_heap_desc.Format = DXGI_FORMAT_D32_FLOAT;
        depth_heap_desc.SampleDesc.Count = presets_.enable_msaa_4x ? 4 : 1;
        depth_heap_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_heap_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        auto depth_heap_prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_CLEAR_VALUE opt_clear = {};
        opt_clear.Format = DXGI_FORMAT_D32_FLOAT;
        opt_clear.DepthStencil.Depth = 1.0f;
        opt_clear.DepthStencil.Stencil = 0;

        CHECKHR(device_->CreateCommittedResource(
            &depth_heap_prop,
            D3D12_HEAP_FLAG_NONE,
            &depth_heap_desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &opt_clear,
            IID_PPV_ARGS(&depth_buffer_)
        ));
        depth_buffer_->SetName(L"Depth Buffer");

        // Create DSV Heap for depth buffer
        D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {};
        dsv_desc.NumDescriptors = 1;
        dsv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        CHECKHR(device_->CreateDescriptorHeap(&dsv_desc, IID_PPV_ARGS(&dsv_heap_)));
        device_->CreateDepthStencilView(depth_buffer_.Get(), nullptr, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
        // Query DSV size
        dsv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        render_list_->Close();
    }

    Pipeline& CreatePipeline(const std::string& pipeline_id, const Pipeline::pipeline_init_t& init) {
        auto [it, inserted] = pipelines_.try_emplace(pipeline_id, device_, init);
        return it->second;
    }

    Pipeline& SelectPipeline(const std::string& pipeline_id) {
        return pipelines_.at(pipeline_id);
    }

    void Render() {
        graphics_fence_.Wait();
        CHECKHR(render_alloc_->Reset());
        CHECKHR(render_list_->Reset(render_alloc_.Get(), nullptr));
        // Set viewport and scissor rect
        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)presets_.width, (float)presets_.height, 0.0f, 1.0f };
        D3D12_RECT scissor_rect = { 0, 0, static_cast<LONG>(presets_.width), static_cast<LONG>(presets_.height) };
        render_list_->RSSetViewports(1, &viewport);
        render_list_->RSSetScissorRects(1, &scissor_rect);

        if (presets_.enable_msaa_4x) {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(msaa_rt_.Get(),
    D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            render_list_->ResourceBarrier(1, &barrier);
        } else {
            // Set backbuffer to Render Target
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                buffers_[fb_index_].Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            render_list_->ResourceBarrier(1, &barrier);
        }

        // Offset and get descriptor
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += fb_index_ * rtv_size_;
        render_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        if (presets_.enable_msaa_4x) {
            D3D12_CPU_DESCRIPTOR_HANDLE msaa_rtv = msaa_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
            render_list_->OMSetRenderTargets(1, &msaa_rtv, FALSE, &dsv);
            render_list_->ClearRenderTargetView(msaa_rtv, presets_.clear_color, 0, nullptr);
        } else {
            // Clear Render Target
            render_list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
            render_list_->ClearRenderTargetView(rtv, presets_.clear_color, 0, nullptr);
        }
        for (auto& pair : pipelines_) {
            auto& pipeline = pair.second;
            render_list_->SetPipelineState(pipeline.GetPSO().Get());
            render_list_->SetGraphicsRootSignature(pipeline.GetRootSignature().Get());
            ID3D12DescriptorHeap* heaps[] = {pipeline.GetDescriptorHeap().GetHeap().Get()};
            render_list_->SetDescriptorHeaps(1, heaps);
            auto handle = pipeline.GetDescriptorHeap().GetHeap()->GetGPUDescriptorHandleForHeapStart();
            render_list_->SetGraphicsRootDescriptorTable(0, handle);
            handle.ptr += pipeline.GetDescriptorHeap().GetSRVCount() * pipeline.GetDescriptorHeap().GetDescriptorSize();
            render_list_->SetGraphicsRootDescriptorTable(1, handle);
            handle.ptr += pipeline.GetDescriptorHeap().GetCBVCount() * pipeline.GetDescriptorHeap().GetDescriptorSize();
            render_list_->SetGraphicsRootDescriptorTable(2, handle);
            // Use triangle Topology
            render_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            // Load vertices and indices
            render_list_->IASetVertexBuffers(0, 1, &pipeline.GetVertexBufferView());
            render_list_->IASetIndexBuffer(&pipeline.GetIndexBufferView());
            // Draw call
            render_list_->DrawIndexedInstanced(pipeline.GetInstanceIndicesCount(), pipeline.GetDrawInstancesCount(), 0, 0, 0);
        }
        if (presets_.enable_msaa_4x) {
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
            render_list_->ResourceBarrier(2, barriers);
            render_list_->ResolveSubresource(buffers_[fb_index_].Get(), 0, msaa_rt_.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                buffers_[fb_index_].Get(),
                D3D12_RESOURCE_STATE_RESOLVE_DEST,
                D3D12_RESOURCE_STATE_PRESENT);
            render_list_->ResourceBarrier(1, &barrier);
        } else {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                buffers_[fb_index_].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT);
            render_list_->ResourceBarrier(1, &barrier);
        }
        CHECKHR(render_list_->Close());
        ID3D12CommandList* lists[] = { render_list_.Get() };
        // Execute and present
        render_queue_->ExecuteCommandLists(1, lists);
        CHECKHR(swapchain_->Present(1, 0));
        graphics_fence_.Insert(render_queue_);
        fb_index_ = swapchain_->GetCurrentBackBufferIndex();
        gpu_resource_mgr_.DeferredRelease();
    }

    ComPtr<ID3D12Device> GetDevice() {
        return device_;
    }

    GPUResourceManager& GetGPUResourceManager() {
        return gpu_resource_mgr_;
    }

    ~RenderContext() {
        graphics_fence_.Wait();
    }
};

class DX12Application {
protected:
    RenderContext render_ctx_;
    RenderContext::rendering_presets& presets_;
    FPSCounter fpsc_;
public:
    explicit DX12Application(RenderContext::rendering_presets& presets) : presets_(presets), render_ctx_(presets) {}
    RenderContext& GetRenderContext() {
        return render_ctx_;
    }

    virtual void Update(float delta_ms) = 0;
    virtual void OnWindowActivate(WPARAM) = 0;
    virtual void OnKeyDown(WPARAM) = 0;
    virtual void OnKeyUp(WPARAM) = 0;
    virtual void Run() {
        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                auto delta = fpsc_.delta_ms();
                Update(delta);
                render_ctx_.Render();
            }
        }
    }

    const RenderContext::rendering_presets& GetRenderPresets() const {
        return presets_;
    }

    static LRESULT CALLBACK GameWindowProcess(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        DX12Application* app = nullptr;
        if (uMsg != WM_NCCREATE) {
            app = reinterpret_cast<DX12Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        switch (uMsg) {
            case WM_NCCREATE: {
                CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
                app = reinterpret_cast<DX12Application*>(cs->lpCreateParams);
                SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
                break;
            }
            case WM_DESTROY: PostQuitMessage(0); return 0;
            case WM_ACTIVATE: app->OnWindowActivate(LOWORD(wParam)); break;
            case WM_KEYDOWN: app->OnKeyDown(wParam); break;
            case WM_KEYUP: app->OnKeyUp(wParam); break;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};

class ShaderManager {
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
                std::cout << std::format("loading shader {}. ", shader.path().filename().string());
                std::filesystem::directory_entry shader_cache("shaders/cache/" + shader.path().filename().string());
                shader_type typ;
                std::string hlsl_typ_ver;
                if (shader.path().string().ends_with(".vs")) {
                    typ = shader_type::VERTEX_SHADER;
                    hlsl_typ_ver = "vs_5_1";
                } else if (shader.path().string().ends_with(".ps")) {
                    typ = shader_type::FRAGMENT_SHADER;
                    hlsl_typ_ver = "ps_5_1";
                } else {
                    typ = shader_type::UNKNOWN_SHADER;
                    hlsl_typ_ver = "vs_5_1";
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

class TextureManager {
public:
    using raw_tex_t = struct {
        uint32_t width;
        uint32_t height;
        uint8_t* data;
    };
private:
    std::map<std::string, raw_tex_t> textures_map;

public:
    void load_textures() {
        std::filesystem::directory_entry textures("textures");
        if (!textures.exists()) {
            std::cout << "textures not exists, exit" << std::endl;
            exit(EXIT_FAILURE);
        }
        for (auto& texture : std::filesystem::directory_iterator(textures)) {
            if (texture.is_regular_file() && (texture.path().string().ends_with(".jpg")) || texture.path().string().ends_with(".png")) {
                std::cout << std::format("loading texture {}", texture.path().filename().string()) << std::endl;
                int width, height, channels;
                unsigned char* data = stbi_load(texture.path().string().c_str(), &width, &height, &channels, 4);
                if (data == nullptr) {
                    std::cout << std::format("failed to load texture {}, exit", texture.path().filename().string()) << std::endl;
                    exit(EXIT_FAILURE);
                }
                textures_map[texture.path().filename().string()] = {
                    .width = static_cast<uint32_t>(width),
                    .height = static_cast<uint32_t>(height),
                    .data = data
                };
            }
        }
    }

    std::optional<raw_tex_t> get(const std::string& tex_name) {
        if (textures_map.contains(tex_name)) {
            return textures_map[tex_name];
        }
        return std::nullopt;
    }

};