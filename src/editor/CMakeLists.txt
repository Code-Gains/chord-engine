add_executable(editor
    Editor.cpp
    RegistryViewer.cpp
    ViewerComponentUi.cpp
    EntityViewer.cpp
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
        engine
        imgui
)


# -----------------------------
# Run target
# -----------------------------
add_custom_target(run-editor
    COMMAND $<TARGET_FILE:editor>
    DEPENDS editor
    WORKING_DIRECTORY $<TARGET_FILE_DIR:editor>
)