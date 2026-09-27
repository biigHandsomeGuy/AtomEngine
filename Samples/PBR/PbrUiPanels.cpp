#include "pch.h"
#include "PbrUiPanels.h"

#include <algorithm>
#include <cmath>
#include <Imgui/imgui.h>
#include "GameInput.h"

void DrawRendererSettings(PbrRenderer& renderer)
{
    const bool disabled = GameCore::g_ViewportCapturingMouse;
    if (disabled)
        ImGui::BeginDisabled();
    ImGui::Begin("Renderer Settings");
    auto& shading = renderer.ShadingSettings();
    auto& environment = renderer.EnvironmentSettings();
    auto& post = renderer.PostSettings();
    DirectX::XMFLOAT4 position;
    DirectX::XMStoreFloat4(&position, renderer.Camera().GetPosition());
    ImGui::Text("Camera Position: (%.3f, %.3f, %.3f)", position.x, position.y, position.z);
    ImGui::DragFloat("Free camera near", &renderer.CameraNearClip(), 0.05f, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Free camera far", &renderer.CameraFarClip(), 10.0f, 10.0f, 200000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Env mip map", &environment.EnvMapMipLevel, 0.0f, 10.0f);
    ImGui::SliderFloat("exposure", &post.exposure, 0.1f, 5.0f);
    ImGui::Checkbox("UseFXAA", &post.isRenderingLuminance);
    ImGui::Checkbox("UseTAA", &post.useTAA);
    if (post.useTAA)
        ImGui::SliderFloat("TAA Feedback", &post.taaFeedback, 0.85f, 0.99f);
    ImGui::Checkbox("UseReinhard", &post.reinhard);
    ImGui::Checkbox("UseFilmic", &post.filmic);
    ImGui::Checkbox("UseAces", &post.aces);
    ImGui::Checkbox("UseGama", &post.useGama);
    ImGui::Checkbox("Use Bloom", &post.useBloom);
    if (post.useBloom)
    {
        ImGui::SliderFloat("Bloom Threshold", &post.bloomThreshold, 0.1f, 5.0f);
        ImGui::SliderFloat("Bloom Strength", &post.bloomStrength, 0.0f, 1.0f);
    }
    ImGui::Checkbox("UseSsao", &shading.UseSSAO);
    ImGui::Checkbox("UseShadow", &shading.UseShadow);
    ImGui::Checkbox("UseTexture", &shading.UseTexture);
    ImGui::Checkbox("UseEmu", &shading.UseEmu);
    ImGui::Checkbox("UseSSS", &shading.UseSSS);
    if (shading.UseSSS)
    {
        ImGui::SliderFloat("CurveFactor", &shading.CurveFactor, 0.0f, 10.0f);
        ImGui::SliderFloat("SpecularFactor", &shading.SpecularFactor, 0.0f, 1.0f);
    }
    if (!shading.UseTexture)
    {
        ImGui::ColorPicker3("albedo", shading.albedo);
        ImGui::SliderFloat("metallic", &shading.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("roughness", &shading.roughness, 0.0f, 1.0f);
    }
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("%.3f ms/frame (%.1f FPS)", fps > 0.0f ? 1000.0f / fps : 0.0f, fps);
    ImGui::End();
    if (disabled)
        ImGui::EndDisabled();
}

ViewportRequest DrawRenderViewport(const ViewportImage& image)
{
    const bool visible = ImGui::Begin("ViewPort");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ViewportRequest request;
    GameCore::g_ViewportHovered = false;
    if (visible && image.TextureId != 0 && available.x > 0.0f && available.y > 0.0f)
    {
        request.Width = static_cast<uint32_t>(std::max(8.0f, std::floor(available.x)));
        request.Height = static_cast<uint32_t>(std::max(8.0f, std::floor(available.y)));
        ImGui::Image(static_cast<ImTextureID>(image.TextureId), available);
        GameCore::g_ViewportHovered = ImGui::IsItemHovered();
    }
    GameCore::g_ViewportCapturingMouse = GameInput::IsMouseLookActive();
    ImGui::End();
    return request;
}
