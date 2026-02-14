#pragma once

#include <ft2build.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <format>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>

#include "stb_image_write.h"
#include "../win32/common.h"

#include FT_FREETYPE_H

class FontLoader {
public:
    struct SDFMeta {
        uint32_t character;
        float tex_u0;
        float tex_v0;
        float tex_u1;
        float tex_v1;
        float width;
        float height;
        float bearingX;
        float bearingY;
        float advance;
    };
private:
    FT_Face face_;
    std::string font_name_;
    std::wstring render_chars_;
    size_t atlas_size_{32};
    size_t cell_size_{64};
public:
    FontLoader(const std::string& font_name) : font_name_(font_name) {
        std::string font_path = std::format("./assets/{}.ttf", font_name);
        FT_Library library;
        FT_Init_FreeType(&library);
        FT_Error err = FT_New_Face(library, font_path.c_str(), 0, &face_);
        if (err != 0) {
            std::cout << "failed to load fond " << font_path << std::endl;
            exit(EXIT_FAILURE);
        }
        FT_Set_Pixel_Sizes(face_, 0, atlas_size_);

        std::string chars;
        read_file_to_string("./assets/characters.txt", chars);
        render_chars_ = string_to_wstring(chars).value();
    }
    void GenerateFontTextureAndMeta() {
        std::vector<SDFMeta> metas;
        size_t width = 70;
        size_t height = 70;
        size_t cell_size = 128;
        std::vector<uint8_t> atlas_bits(cell_size * width * cell_size * height, 128);
        float total_width = (float)(width * cell_size);
        float total_height = (float)(height * cell_size);
        std::cout << std::format("\rGenerating Atlas: {} / {}\r", 0, render_chars_.size());
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                size_t idx = i * width + j;
                if (idx >= render_chars_.size()) break;
                wchar_t c = render_chars_[idx];
                std::cout << std::format("\rGenerating Atlas: {} / {}", idx, render_chars_.size() - 1);
                FT_Load_Char(face_, c, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
                FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_SDF);
                FT_Bitmap& bitmap = face_->glyph->bitmap;
                size_t start_x = j * cell_size + (cell_size - bitmap.width) / 2;
                size_t start_y = i * cell_size + (cell_size - bitmap.rows) / 2;
                float x0 = (float)start_x;
                float y0 = (float)start_y;
                float x1 = (float)(start_x + bitmap.width);
                float y1 = (float)(start_y + bitmap.rows);
                float uv_x0 = x0 / total_width;
                float uv_y0 = y0 / total_height;
                float uv_x1 = x1 / total_width;
                float uv_y1 = y1 / total_height;
                float font_width = face_->glyph->bitmap.width;
                float font_height = face_->glyph->bitmap.rows;
                float bearingX = (float)face_->glyph->bitmap_left;
                float bearingY = (float)face_->glyph->bitmap_top;
                float advance  = (float)(face_->glyph->advance.x >> 6);
                metas.push_back({c, uv_x0, uv_y0, uv_x1, uv_y1, font_width, font_height, bearingX, bearingY, advance});
                for (int row = 0; row < (int)bitmap.rows; ++row) {
                    if (row >= cell_size) break;
                    uint8_t* dest = &atlas_bits[(start_y + row) * (width * cell_size) + start_x];
                    uint8_t* src = bitmap.buffer + row * bitmap.pitch;
                    size_t copy_width = std::min((size_t)bitmap.width, cell_size);
                    memcpy(dest, src, copy_width);
                }
            }
        }
        std::cout << std::endl;
        stbi_write_png(std::format("./textures/{}_tex.png", font_name_).c_str(), width * cell_size, height * cell_size, 1, atlas_bits.data(), width * cell_size);
        WriteFontMeta(metas, atlas_size_, font_name_);
    }

    static void WriteFontMeta(const std::vector<SDFMeta>& metas, float atlas_size, std::string_view font_name) {
        std::string file_name = std::format("./textures/{}_tex.sdfmeta", font_name);
        std::ofstream out(file_name.data(), std::ios::out | std::ios::binary);
        size_t size = metas.size();
        out.write(reinterpret_cast<const char *>(&size), sizeof(size));
        out.write(reinterpret_cast<const char *>(&atlas_size), sizeof(atlas_size));
        for (auto& m: metas) {
            out.write(reinterpret_cast<const char *>(&m), sizeof(SDFMeta));
        }
    }

   static void ReadFontMeta(std::vector<SDFMeta>& metas, float& atlas_size, std::string_view font_name) {
        std::string file_name = std::format("./textures/{}_tex.sdfmeta", font_name);
        std::ifstream in(file_name.data(), std::ios::out | std::ios::binary);
        if (!in.good()) {
            std::cout << "failed to load font meta of " << file_name << std::endl;
            exit(EXIT_FAILURE);
        }
        size_t size;
        in.read(reinterpret_cast<char *>(&size), sizeof(size));
        in.read(reinterpret_cast<char *>(&atlas_size), sizeof(atlas_size));
        metas.resize(size);
        for (int i = 0; i < size; ++i) {
            in.read(reinterpret_cast<char *>(&metas[i]), sizeof(SDFMeta));
        }
    }
};