cmake_minimum_required(VERSION 3.31)
project(Gerk)

set(CMAKE_CXX_STANDARD 20)
include_directories(include)
add_compile_definitions(-DUNICODE)
link_directories(lib)

set(COPY_DIRS shaders assets textures)
add_custom_target(copy_dirs ALL)
foreach(dir ${COPY_DIRS})
    add_custom_command(POST_BUILD TARGET copy_dirs
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/${dir}
            $<TARGET_FILE_DIR:Gerk>/${dir}
    )
endforeach ()

add_executable(Gerk main.cpp
        src/win32/window.h
        src/win32/common.h
        src/dx12/dx12_framework.h
        src/logic.h
        src/common/obj_loader.h
        src/common/model_loader.h
        "src/dx12/square_pyramid_sample.hpp"
        src/dx12/dx12_ui.h
        src/common/font_loader.h
        src/dx12/dx12_transformation.h
        src/common/logger.h
)
target_link_libraries(Gerk d3d12 dxgi d3dcompiler freetyped)
add_dependencies(Gerk copy_dirs)
