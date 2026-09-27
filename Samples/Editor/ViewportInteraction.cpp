#include "pch.h"
#include "ViewportInteraction.h"
#include "PbrRenderer.h"
#include <cmath>

namespace AtomEditor
{
    namespace
    {
        bool Same(const TransformComponent& a, const TransformComponent& b)
        {
            XMFLOAT4X4 ma, mb;
            XMStoreFloat4x4(&ma, a.GetMatrix()); XMStoreFloat4x4(&mb, b.GetMatrix());
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    if (std::abs(ma.m[r][c] - mb.m[r][c]) > 1e-5f) return false;
            return true;
        }
        float Distance(Math::Vector3 a, Math::Vector3 b)
        {
            return XMVectorGetX(XMVector3Length(a - b));
        }
        Math::Vector3 Direction(float yaw, float pitch)
        {
            return Math::Vector3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
        }
    }

    bool TransformDrag::Begin(Scene& scene, EntityId id)
    {
        if (Active()) return false;
        const auto* transform = scene.EntityTransform(id);
        if (!transform) return false;
        m_Id = id; m_Before = *transform;
        return true;
    }

    bool TransformDrag::Apply(Scene& scene, const XMFLOAT4X4& matrix)
    {
        auto* target = scene.EntityTransform(m_Id);
        if (!Active() || !target) return false;
        for (const auto& row : matrix.m)
            for (float value : row) if (!std::isfinite(value)) return false;
        XMVECTOR scale, rotation, translation;
        if (!XMMatrixDecompose(&scale, &rotation, &translation, XMLoadFloat4x4(&matrix))) return false;
        XMFLOAT3 s; XMStoreFloat3(&s, scale);
        const float tolerance = std::max(1e-5f, std::abs(s.x) * 1e-4f);
        if (s.x < 0.001f || s.x > 10000 || std::abs(s.x - s.y) > tolerance || std::abs(s.x - s.z) > tolerance) return false;
        TransformComponent result;
        result.scale = s.x;
        result.transform.SetTranslation(Math::Vector3(translation));
        result.transform.SetRotation(Math::Quaternion(XMQuaternionNormalize(rotation)));
        // Reject perspective, shear and reflection instead of silently changing them.
        XMFLOAT4X4 reconstructed;
        XMStoreFloat4x4(&reconstructed, result.GetMatrix());
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (std::abs(reconstructed.m[r][c] - matrix.m[r][c]) > std::max(1e-4f, std::abs(matrix.m[r][c]) * 1e-4f)) return false;
        if (!scene.FindModel(m_Id) && std::abs(result.scale - m_Before.scale) > tolerance) return false;
        if (!Same(*target, result)) { *target = result; scene.MarkModified(); }
        return true;
    }

    bool TransformDrag::Commit(Scene& scene, CommandHistory& history)
    {
        if (!Active()) return false;
        const auto id = m_Id;
        m_Id = InvalidEntityId;
        const auto* target = scene.EntityTransform(id);
        if (!target || Same(m_Before, *target)) return false;
        const auto before = m_Before, after = *target;
        history.RecordApplied("Viewport Transform", [id, before](Scene& scene) {
            if (auto* transform = scene.EntityTransform(id)) *transform = before;
        }, [id, after](Scene& scene) {
            if (auto* transform = scene.EntityTransform(id)) *transform = after;
        });
        return true;
    }

    void TransformDrag::Cancel(Scene& scene)
    {
        if (Active())
            if (auto* transform = scene.EntityTransform(m_Id))
                if (!Same(*transform, m_Before)) { *transform = m_Before; scene.MarkModified(); }
        m_Id = InvalidEntityId;
    }

    Math::Camera ViewportCamera(const Scene& scene, const PbrRenderer& renderer, float heightOverWidth)
    {
        Math::Camera camera = renderer.EditorCamera();
        const auto source = renderer.SceneCameraSource();
        if (const auto* entity = scene.FindCamera(source ? source : scene.ActiveCameraId()); entity && renderer.UsesSceneCamera())
        {
            camera.SetTransform(entity->Transform.transform);
            camera.SetPerspectiveMatrix(XMConvertToRadians(entity->Camera.VerticalFovDegrees), heightOverWidth,
                entity->Camera.NearClip, entity->Camera.FarClip);
        }
        camera.SetAspectRatio(heightOverWidth);
        camera.Update();
        return camera;
    }

    bool FocusCamera(Math::Camera& camera, const AABB& bounds, float heightOverWidth, Math::Vector3& pivot)
    {
        if (!SceneQueries::ValidBounds(bounds) || !std::isfinite(heightOverWidth) || heightOverWidth <= 0) return false;
        const float radius = std::max(0.05f, XMVectorGetX(XMVector3Length(bounds.GetDimensions())) * 0.5f);
        const float halfFov = std::min(camera.GetFOV() * 0.5f, std::atan(std::tan(camera.GetFOV() * 0.5f) / heightOverWidth));
        const float distance = radius * 1.2f / std::sin(halfFov);
        if (!std::isfinite(distance)) return false;
        pivot = bounds.GetCenter();
        camera.SetPosition(pivot - camera.GetForwardVec() * distance);
        camera.SetAspectRatio(heightOverWidth);
        camera.SetZRange(std::min(camera.GetNearClip(), std::max(0.001f, (distance - radius) * 0.5f)),
            std::max(camera.GetFarClip(), (distance + radius) * 2));
        return true;
    }

    void LookCamera(Math::Camera& camera, float dx, float dy)
    {
        const auto f = camera.GetForwardVec();
        const float yaw = std::atan2(float(f.GetX()), float(f.GetZ())) - dx * 0.006f;
        const float pitch = std::clamp(std::asin(std::clamp(float(f.GetY()), -1.0f, 1.0f)) - dy * 0.006f, -1.5533f, 1.5533f);
        camera.SetLookDirection(Direction(yaw, pitch), Math::Vector3(Math::kYUnitVector));
    }

    void OrbitCamera(Math::Camera& camera, Math::Vector3 pivot, float dx, float dy)
    {
        const float distance = std::max(0.01f, Distance(camera.GetPosition(), pivot));
        camera.SetLookDirection(Math::Normalize(pivot - camera.GetPosition()), Math::Vector3(Math::kYUnitVector));
        LookCamera(camera, dx, dy);
        camera.SetPosition(pivot - camera.GetForwardVec() * distance);
    }

    void PanCamera(Math::Camera& camera, Math::Vector3& pivot, float dx, float dy, float height)
    {
        if (height <= 0) return;
        const float units = 2 * std::max(0.01f, Distance(camera.GetPosition(), pivot)) * std::tan(camera.GetFOV() * 0.5f) / height;
        const auto offset = (camera.GetRightVec() * -dx + camera.GetUpVec() * dy) * units;
        camera.SetPosition(camera.GetPosition() + offset); pivot += offset;
    }

    void DollyCamera(Math::Camera& camera, Math::Vector3 pivot, float wheel)
    {
        const auto offset = camera.GetPosition() - pivot;
        const float distance = Distance(camera.GetPosition(), pivot);
        if (distance < 1e-6f) return;
        const float next = std::clamp(distance * std::exp(std::clamp(-wheel * 0.15f, -10.0f, 10.0f)),
            std::max(0.01f, camera.GetNearClip() * 2), std::max(camera.GetNearClip() * 2, camera.GetFarClip() * 0.8f));
        camera.SetPosition(pivot + offset * (next / distance));
    }

    bool ProjectEdge(const Math::Camera& camera, Math::Vector3 a, Math::Vector3 b, XMFLOAT2& first, XMFLOAT2& last)
    {
        XMFLOAT4 p, q;
        XMStoreFloat4(&p, XMVector4Transform(XMVectorSetW(a, 1), camera.GetViewProjMatrix()));
        XMStoreFloat4(&q, XMVector4Transform(XMVectorSetW(b, 1), camera.GetViewProjMatrix()));
        const float pa[] = {p.z, p.w - p.z, p.w - 1e-5f}, qa[] = {q.z, q.w - q.z, q.w - 1e-5f};
        float lo = 0, hi = 1;
        for (int plane = 0; plane < 3; ++plane)
        {
            if (!std::isfinite(pa[plane]) || !std::isfinite(qa[plane])) return false;
            if (pa[plane] < 0 && qa[plane] < 0) return false;
            if (pa[plane] < 0) lo = std::max(lo, pa[plane] / (pa[plane] - qa[plane]));
            if (qa[plane] < 0) hi = std::min(hi, pa[plane] / (pa[plane] - qa[plane]));
        }
        if (lo > hi) return false;
        auto project = [&](float t) {
            const float w = p.w + (q.w - p.w) * t;
            return XMFLOAT2((p.x + (q.x - p.x) * t) / w * 0.5f + 0.5f, 0.5f - (p.y + (q.y - p.y) * t) / w * 0.5f);
        };
        first = project(lo); last = project(hi);
        return std::isfinite(first.x) && std::isfinite(first.y) && std::isfinite(last.x) && std::isfinite(last.y);
    }
}
