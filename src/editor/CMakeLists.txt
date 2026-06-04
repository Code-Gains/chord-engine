add_library(editor_tools STATIC
    RegistryViewer.cpp
    ViewerComponentUi.cpp
    EntityViewer.cpp
    AssetViewer.cpp
)

target_include_directories(editor_tools
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..       # points to src/
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine # engine headers
        $ENV{VULKAN_SDK}/Include            # Vulkan headers
)

target_link_libraries(editor_tools
    PUBLIC
        engine
        imgui
)

add_executable(editor
    Editor.cpp
)

target_compile_definitions(editor PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

target_include_directories(editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..       # points to src/
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine # engine headers
        $ENV{VULKAN_SDK}/Include            # Vulkan headers
)

target_link_libraries(editor
    PRIVATE 
        editor_tools
)


# -----------------------------
# Run target
# -----------------------------
add_custom_target(run-editor
    COMMAND $<TARGET_FILE:editor>
    DEPENDS editor
    WORKING_DIRECTORY $<TARGET_FILE_DIR:editor>
)
