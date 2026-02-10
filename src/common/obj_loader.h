#pragma once
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>
#include <fstream>
#include <iostream>

class obj_loader {
public:
    struct vertex {
        float x;
        float y;
        float z;
    };
private:
    std::vector<vertex> vertices_;
    std::vector<uint32_t> indices_;
public:
    bool load_model(std::string_view path) {
        std::ifstream in(std::filesystem::path(path), std::ios::in | std::ios::binary);
        if (!in) {
            std::cout << std::format("failed to open model {}", path) << std::endl;
            return false;
        }
        while (!in.eof()) {
            char buf[1024];
            in.getline(buf, 1024);
            std::stringstream ss(buf);
            std::string typ;
            ss >> typ;
            if (typ == "v") {
                vertex v {};
                ss >> v.x;
                ss >> v.y;
                ss >> v.z;
                vertices_.push_back(v);
            } else if (typ == "f") {
                for (int i = 0; i < 3; ++i) {
                    std::string tok;
                    ss >> tok;
                    uint32_t vi = 0;
                    sscanf_s(tok.c_str(), "%u", &vi);
                    indices_.push_back(vi - 1);
                }
            }
        }
        return true;
    }
    std::vector<vertex>& vertices() {
        return vertices_;
    }
    std::vector<uint32_t>& indices() {
        return indices_;
    }
};
