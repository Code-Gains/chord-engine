#include "EditorImGuiTheme.h"

#include <imgui.h>

namespace Engine {
namespace {

constexpr float kEditorFontSize = 13.0f;

ImVec4 Color(float r, float g, float b, float a = 1.0f)
{
    return ImVec4{ r, g, b, a };
}

void ApplyEditorFont()
{
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig fontConfig;
    fontConfig.SizePixels = kEditorFontSize;
    fontConfig.PixelSnapH = true;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    io.Fonts->AddFontDefaultBitmap(&fontConfig);
}

void ApplyEditorColors()
{
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = Color(0.90f, 0.93f, 0.91f);
    colors[ImGuiCol_TextDisabled] = Color(0.48f, 0.54f, 0.50f);
    colors[ImGuiCol_WindowBg] = Color(0.09f, 0.10f, 0.10f);
    colors[ImGuiCol_ChildBg] = Color(0.10f, 0.11f, 0.11f);
    colors[ImGuiCol_PopupBg] = Color(0.08f, 0.09f, 0.09f);
    colors[ImGuiCol_Border] = Color(0.22f, 0.27f, 0.24f);
    colors[ImGuiCol_FrameBg] = Color(0.13f, 0.17f, 0.15f);
    colors[ImGuiCol_FrameBgHovered] = Color(0.16f, 0.28f, 0.21f);
    colors[ImGuiCol_FrameBgActive] = Color(0.18f, 0.38f, 0.27f);
    colors[ImGuiCol_TitleBg] = Color(0.08f, 0.10f, 0.09f);
    colors[ImGuiCol_TitleBgActive] = Color(0.10f, 0.20f, 0.15f);
    colors[ImGuiCol_TitleBgCollapsed] = Color(0.06f, 0.07f, 0.07f);
    colors[ImGuiCol_MenuBarBg] = Color(0.10f, 0.12f, 0.11f);
    colors[ImGuiCol_ScrollbarBg] = Color(0.06f, 0.07f, 0.07f);
    colors[ImGuiCol_ScrollbarGrab] = Color(0.20f, 0.30f, 0.24f);
    colors[ImGuiCol_ScrollbarGrabHovered] = Color(0.24f, 0.42f, 0.31f);
    colors[ImGuiCol_ScrollbarGrabActive] = Color(0.28f, 0.55f, 0.38f);
    colors[ImGuiCol_CheckMark] = Color(0.35f, 0.82f, 0.45f);
    colors[ImGuiCol_SliderGrab] = Color(0.28f, 0.68f, 0.38f);
    colors[ImGuiCol_SliderGrabActive] = Color(0.36f, 0.90f, 0.48f);
    colors[ImGuiCol_Button] = Color(0.16f, 0.30f, 0.22f);
    colors[ImGuiCol_ButtonHovered] = Color(0.22f, 0.48f, 0.32f);
    colors[ImGuiCol_ButtonActive] = Color(0.28f, 0.62f, 0.40f);
    colors[ImGuiCol_Header] = Color(0.16f, 0.34f, 0.24f);
    colors[ImGuiCol_HeaderHovered] = Color(0.22f, 0.50f, 0.34f);
    colors[ImGuiCol_HeaderActive] = Color(0.27f, 0.62f, 0.41f);
    colors[ImGuiCol_Separator] = Color(0.20f, 0.28f, 0.24f);
    colors[ImGuiCol_SeparatorHovered] = Color(0.28f, 0.62f, 0.40f);
    colors[ImGuiCol_SeparatorActive] = Color(0.35f, 0.80f, 0.50f);
    colors[ImGuiCol_ResizeGrip] = Color(0.22f, 0.50f, 0.34f, 0.45f);
    colors[ImGuiCol_ResizeGripHovered] = Color(0.28f, 0.68f, 0.42f, 0.70f);
    colors[ImGuiCol_ResizeGripActive] = Color(0.35f, 0.86f, 0.50f, 0.95f);
    colors[ImGuiCol_Tab] = Color(0.12f, 0.22f, 0.17f);
    colors[ImGuiCol_TabHovered] = Color(0.24f, 0.55f, 0.36f);
    colors[ImGuiCol_TabActive] = Color(0.18f, 0.38f, 0.26f);
    colors[ImGuiCol_TabUnfocused] = Color(0.08f, 0.12f, 0.10f);
    colors[ImGuiCol_TabUnfocusedActive] = Color(0.12f, 0.24f, 0.17f);
    colors[ImGuiCol_DockingPreview] = Color(0.25f, 0.78f, 0.42f, 0.70f);
    colors[ImGuiCol_TextSelectedBg] = Color(0.20f, 0.55f, 0.32f, 0.45f);
    colors[ImGuiCol_NavHighlight] = Color(0.35f, 0.82f, 0.45f);

    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.FramePadding = ImVec2{ 7.0f, 4.0f };
    style.ItemSpacing = ImVec2{ 8.0f, 5.0f };
    style.WindowPadding = ImVec2{ 8.0f, 8.0f };

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

}

void ApplyEditorImGuiTheme()
{
    ApplyEditorFont();
    ApplyEditorColors();
}

}
