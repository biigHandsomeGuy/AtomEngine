#pragma once

#include "EditorCommandHistory.h"
#include "SceneQueries.h"

class PbrRenderer;

namespace AtomEditor
{
    // One gesture owns one undo record. No entity pointer survives a frame.
    class TransformDrag
    {
    public:
        bool Begin(Scene& scene, EntityId id);
        bool Apply(Scene& scene, const DirectX::XMFLOAT4X4& matrix);
        bool Commit(Scene& scene, CommandHistory& history);
        void Cancel(Scene& scene);
        bool Active() const { return m_Id != InvalidEntityId; }
        EntityId Entity() const { return m_Id; }
    private:
        EntityId m_Id = InvalidEntityId;
        TransformComponent m_Before;
    };

    Math::Camera ViewportCamera(const Scene& scene, const PbrRenderer& renderer, float heightOverWidth);
    bool FocusCamera(Math::Camera& camera, const AABB& bounds, float heightOverWidth, Math::Vector3& pivot);
    void OrbitCamera(Math::Camera& camera, Math::Vector3 pivot, float dx, float dy);
    void PanCamera(Math::Camera& camera, Math::Vector3& pivot, float dx, float dy, float height);
    void DollyCamera(Math::Camera& camera, Math::Vector3 pivot, float wheel);
    void LookCamera(Math::Camera& camera, float dx, float dy);
    // D3D clip volume; clips edges that straddle the near/far plane.
    bool ProjectEdge(const Math::Camera& camera, Math::Vector3 a, Math::Vector3 b,
        DirectX::XMFLOAT2& first, DirectX::XMFLOAT2& last);
}
