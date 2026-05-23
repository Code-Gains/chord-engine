# commands to run:
# Windows:
# cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVulkanSdkDir="C:/VulkanSDK/1.4.335.0"
#
# Linux:
# cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVulkanSdkDir="$HOME/vulkan-sdk/x86_64"
#
# Build:
# cmake --build build --target editor

cmake_minimum_required(VERSION 3.26)
cmake_policy(VERSION 3.26)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(MyVulkanEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wno-deprecated-literal-operator)

include(FetchContent)

# ---------------------
# Vulkan
# ---------------------
if(NOT DEFINED VulkanSdkDir)
    message(FATAL_ERROR "You must set -DVulkanSdkDir=<path to VulkanSDK>")
endif()

set(ENV{VULKAN_SDK} "${VulkanSdkDir}")

if(WIN32)
    set(Vulkan_INCLUDE_DIR "${VulkanSdkDir}/Include")
    set(Vulkan_LIBRARY "${VulkanSdkDir}/Lib/vulkan-1.lib")
elseif(UNIX)
    set(Vulkan_INCLUDE_DIR "${VulkanSdkDir}/include")
    set(Vulkan_LIBRARY "${VulkanSdkDir}/lib/libvulkan.so")
endif()

find_package(Vulkan REQUIRED)

message(STATUS "Vulkan include dir: ${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan library: ${Vulkan_LIBRARY}")

# ---------------------
# GLFW
# ---------------------
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# ---------------------
# GLM
# ---------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
)
FetchContent_MakeAvailable(glm)

# ---------------------
# VkBootstrap
# ---------------------
FetchContent_Declare(
    vkbootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG v1.4.344
)
FetchContent_MakeAvailable(vkbootstrap)

# ---------------------
# ImGui
# ---------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.6-docking
    GIT_SHALLOW FALSE
)

FetchContent_GetProperties(imgui)

if(NOT imgui_POPULATED)
    message(STATUS "Cloning ImGui v1.92.6-docking...")
    FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

# ---------------------
# fastgltf
# ---------------------
FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG v0.9.0
)
FetchContent_MakeAvailable(fastgltf)

# ---------------------
# EnTT
# ---------------------
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.0
)
FetchContent_MakeAvailable(entt)

# ---------------------
# Shaders
# ---------------------
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders" CACHE PATH "Shader directory")

# ---------------------
# Add src folders
# ---------------------
add_subdirectory(src/engine)
add_subdirectory(src/editor)
add_subdirectory(src/simulation)