#pragma once
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>
#include <fstream>
#include <iostream>

#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <format>
#include <unordered_map>

class obj_loader {
public:

    struct vertex {
        float x, y, z;
        float u, v;
        float nx, ny, nz;
        bool operator==(const vertex& other) const {
            return memcmp(this, &other, sizeof(vertex)) == 0;
        }
    };

    struct vertex_hasher {
        size_t operator()(const vertex& v) const {
            size_t h1 = std::hash<float>{}(v.x);
            size_t h2 = std::hash<float>{}(v.y);
            size_t h3 = std::hash<float>{}(v.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

private:
    std::vector<float> temp_positions_;
    std::vector<float> temp_uvs_;
    std::vector<float> temp_normals_;

    std::vector<vertex> vertices_;
    std::vector<uint32_t> indices_;

public:
    bool load_model(std::string_view path) {
        std::ifstream in(path.data(), std::ios::in);
        if (!in.is_open()) {
            std::cout << std::format("Failed to open model: {}", path) << std::endl;
            return false;
        }

        std::unordered_map<vertex, uint32_t, vertex_hasher> unique_vertices;
        std::string line;

        while (std::getline(in, line)) {
            std::stringstream ss(line);
            std::string prefix;
            ss >> prefix;

            if (prefix == "v") {
                float x, y, z;
                ss >> x >> y >> z;
                temp_positions_.insert(temp_positions_.end(), {x, y, z});
            }
            else if (prefix == "vt") {
                float u, v;
                ss >> u >> v;
                temp_uvs_.insert(temp_uvs_.end(), {u, 1.0f - v});
            }
            else if (prefix == "vn") {
                float x, y, z;
                ss >> x >> y >> z;
                temp_normals_.insert(temp_normals_.end(), {x, y, z});
            }
            else if (prefix == "f") {
                for (int i = 0; i < 3; ++i) {
                    std::string segment;
                    ss >> segment;

                    uint32_t v_idx = 0, vt_idx = 0, vn_idx = 0;
                    parse_face_segment(segment, v_idx, vt_idx, vn_idx);

                    vertex vert{};
                    vert.x = temp_positions_[(v_idx - 1) * 3 + 0];
                    vert.y = temp_positions_[(v_idx - 1) * 3 + 1];
                    vert.z = temp_positions_[(v_idx - 1) * 3 + 2];

                    if (vt_idx > 0) {
                        vert.u = temp_uvs_[(vt_idx - 1) * 2 + 0];
                        vert.v = temp_uvs_[(vt_idx - 1) * 2 + 1];
                    }
                    if (vn_idx > 0) {
                        vert.nx = temp_normals_[(vn_idx - 1) * 3 + 0];
                        vert.ny = temp_normals_[(vn_idx - 1) * 3 + 1];
                        vert.nz = temp_normals_[(vn_idx - 1) * 3 + 2];
                    }

                    if (unique_vertices.find(vert) == unique_vertices.end()) {
                        unique_vertices[vert] = static_cast<uint32_t>(vertices_.size());
                        vertices_.push_back(vert);
                    }
                    indices_.push_back(unique_vertices[vert]);
                }
            }
        }
        return true;
    }

    const std::vector<vertex>& vertices() const { return vertices_; }
    const std::vector<uint32_t>& indices() const { return indices_; }

private:
    void parse_face_segment(const std::string& segment, uint32_t& v, uint32_t& vt, uint32_t& vn) {
        v = vt = vn = 0;
        size_t first_slash = segment.find('/');
        size_t last_slash = segment.find_last_of('/');

        v = std::stoi(segment.substr(0, first_slash));
        if (first_slash != std::string::npos && last_slash != std::string::npos) {
            if (last_slash > first_slash + 1) {
                vt = std::stoi(segment.substr(first_slash + 1, last_slash - first_slash - 1));
            }
            vn = std::stoi(segment.substr(last_slash + 1));
        }
    }
};