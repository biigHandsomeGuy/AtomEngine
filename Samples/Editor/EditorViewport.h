#pragma once

#include "EditorPanels.h"
#include "ViewportInteraction.h"
#include "..\PBR\PbrApplication.h"

namespace AtomEditor
{
    class EditorViewport
    {
    public:
        enum class Operation { Translate, Rotate, Scale };
        void BeginFrame(Scene& scene, CommandHistory& history, bool blocked);
        ViewportRequest Draw(Scene& scene, UiState& state, CommandHistory& history,
            PbrRenderer& renderer, const ViewportImage& image, bool blocked);
        // Also usable inside a host window. Coordinates are in screen pixels.
        ViewportRequest DrawContent(Scene& scene, UiState& state, CommandHistory& history,
            PbrRenderer& renderer, const ViewportImage& image, bool blocked);
        bool Capturing() const { return m_Drag.Active() || m_NavigationButton >= 0; }
        Operation Tool() const { return m_Operation; }
        struct ToolSettings
        {
            bool Local = false;
            bool Snap = false;
            float TranslationStep = 1, RotationStep = 15, ScaleStep = 0.1f;
        };
        ToolSettings& Settings() { return m_ToolSettings; }
        DirectX::XMFLOAT4 ImageRect() const { return m_ImageRect; } // x, y, width, height

    private:
        void Release(Scene& scene, CommandHistory& history);
        void TakeCamera(PbrRenderer& renderer, const Math::Camera& camera);
        void UpdatePivot(const Scene& scene, EntityId selected, const Math::Camera& camera);
        void Navigate(PbrRenderer& renderer, Math::Camera& camera, float height, bool hovered);
        TransformDrag m_Drag;
        Operation m_Operation = Operation::Translate;
        ToolSettings m_ToolSettings;
        int m_NavigationButton = -1;
        bool m_Orbit = false;
        bool m_HasPivot = false;
        EntityId m_PivotSelection = InvalidEntityId;
        Math::Vector3 m_Pivot{0, 0, 0};
        Math::Vector3 m_SelectionCenter{0, 0, 0};
        DirectX::XMFLOAT4 m_ImageRect{};
    };
}
