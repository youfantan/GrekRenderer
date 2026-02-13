#pragma once

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <iostream>
#include <tiny_gltf.h>


class gltf_loader {
public:
    struct mesh {
        std::vector<float> vertices;
        std::vector<float> normals;
        std::vector<float> uvs;
        std::vector<uint32_t> indices;

        int textureWidth = 0;
        int textureHeight = 0;
        int textureChannels = 0;
        std::vector<unsigned char> texture_data;
    };

    struct vertex {
        float x, y, z;
        float u, v;
        float nx, ny, nz;
    };
private:
    mesh mesh_;
    std::vector<vertex> vertices_;
    std::vector<uint32_t>& indices = mesh_.indices;
    const std::string& file_name_;

    const unsigned char* get_buffer_ptr(const tinygltf::Model& model, int accessor_index) {
        const tinygltf::Accessor& accessor = model.accessors[accessor_index];
        const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
        return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    }
public:
    gltf_loader(const std::string& file_name) : file_name_(file_name) {}
    bool LoadGLB() {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, file_name_);
        if (!warn.empty()) std::cout << "Warn: " << warn << std::endl;
        if (!err.empty()) std::cerr << "Err: " << err << std::endl;
        if (!ret) return false;

        if (model.meshes.empty()) return false;
        const tinygltf::Mesh& mesh = model.meshes[0];
        const tinygltf::Primitive& primitive = mesh.primitives[0];

        if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
            int accessorIdx = primitive.attributes.at("POSITION");
            const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
            const float* positions = reinterpret_cast<const float*>(get_buffer_ptr(model, accessorIdx));
            for (size_t i = 0; i < accessor.count; ++i) {
                mesh_.vertices.push_back(positions[i * 3 + 0]);
                mesh_.vertices.push_back(positions[i * 3 + 1]);
                mesh_.vertices.push_back(positions[i * 3 + 2]);
            }
            std::cout << "Extracted " << accessor.count << " vertices." << std::endl;
        }

        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
            int accessorIdx = primitive.attributes.at("NORMAL");
            const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
            const float* normals = reinterpret_cast<const float*>(get_buffer_ptr(model, accessorIdx));

            for (size_t i = 0; i < accessor.count; ++i) {
                mesh_.normals.push_back(normals[i * 3 + 0]);
                mesh_.normals.push_back(normals[i * 3 + 1]);
                mesh_.normals.push_back(normals[i * 3 + 2]);
            }
            std::cout << "Extracted " << accessor.count << " normals." << std::endl;
        }

        if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
            int accessorIdx = primitive.attributes.at("TEXCOORD_0");
            const tinygltf::Accessor& accessor = model.accessors[accessorIdx];
            const float* uvs = reinterpret_cast<const float*>(get_buffer_ptr(model, accessorIdx));

            for (size_t i = 0; i < accessor.count; ++i) {
                mesh_.uvs.push_back(uvs[i * 2 + 0]);
                mesh_.uvs.push_back(uvs[i * 2 + 1]);
            }
            std::cout << "Extracted " << accessor.count << " UVs." << std::endl;
        }

        if (primitive.indices >= 0) {
            const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
            const unsigned char* bufferPtr = get_buffer_ptr(model, primitive.indices);

            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const uint16_t* buf = reinterpret_cast<const uint16_t*>(bufferPtr);
                for (size_t i = 0; i < accessor.count; ++i) {
                    mesh_.indices.push_back(buf[i]);
                }
            } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const uint32_t* buf = reinterpret_cast<const uint32_t*>(bufferPtr);
                for (size_t i = 0; i < accessor.count; ++i) {
                    mesh_.indices.push_back(buf[i]);
                }
            }
            std::cout << "Extracted " << accessor.count << " indices." << std::endl;
        }
        
        if (primitive.material >= 0) {
            const tinygltf::Material& mat = model.materials[primitive.material];
            int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
            if (texIndex >= 0) {
                const tinygltf::Texture& tex = model.textures[texIndex];
                if (tex.source >= 0) {
                    const tinygltf::Image& img = model.images[tex.source];
                    mesh_.textureWidth = img.width;
                    mesh_.textureHeight = img.height;
                    mesh_.textureChannels = img.component;
                    mesh_.texture_data = img.image;
                    std::cout << "Extracted Texture: " << img.width << "x" << img.height
                              << " (" << img.component << " channels)" << std::endl;
                }
            }
        }
        return true;
    }

    void merge() {
        size_t vertexCount = mesh_.vertices.size() / 3;
        vertices_.reserve(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            vertex v;
            v.x = mesh_.vertices[i * 3 + 0];
            v.y = mesh_.vertices[i * 3 + 1];
            v.z = mesh_.vertices[i * 3 + 2];

            if (!mesh_.uvs.empty()) {
                v.u = mesh_.uvs[i * 2 + 0];
                v.v = mesh_.uvs[i * 2 + 1];
            } else {
                v.u = 0.0f; v.v = 0.0f;
            }

            if (!mesh_.normals.empty()) {
                v.nx = mesh_.normals[i * 3 + 0];
                v.ny = mesh_.normals[i * 3 + 1];
                v.nz = mesh_.normals[i * 3 + 2];
            } else {
                v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
            }

            vertices_.push_back(v);
        }
    }
};