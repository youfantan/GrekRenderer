cmake_minimum_required(VERSION 3.31)
project(Gerk)

set(CMAKE_CXX_STANDARD 20)
include_directories(include)
add_executable(Gerk main.cpp
        src/win32/window.h
        src/win32/common.h
        src/dx12/dx12_framework.h
        src/logic.h)
target_link_libraries(Gerk d3d12 dxgi d3dcompiler)