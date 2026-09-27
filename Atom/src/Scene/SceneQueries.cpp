#include "pch.h"
#include "SceneQueries.h"
#include <cmath>

namespace SceneQueries
{
    namespace
    {
        XMFLOAT3 Store(Math::Vector3 value)
        {
            XMFLOAT3 result;
            XMStoreFloat3(&result, value);
            return result;
        }
        bool Finite(const XMFLOAT3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
        bool BoxHit(const AABB& bounds, Math::Vector3 origin, Math::Vector3 direction, float first, float last)
        {
            if (!ValidBounds(bounds)) return false;
            const auto lo = Store(bounds.GetMin()), hi = Store(bounds.GetMax());
            const auto o = Store(origin), d = Store(direction);
            const float lows[] = {lo.x, lo.y, lo.z}, highs[] = {hi.x, hi.y, hi.z};
            const float origins[] = {o.x, o.y, o.z}, directions[] = {d.x, d.y, d.z};
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(directions[axis]) < 1e-12f)
                {
                    if (origins[axis] < lows[axis] || origins[axis] > highs[axis]) return false;
                    continue;
                }
                float a = (lows[axis] - origins[axis]) / directions[axis];
                float b = (highs[axis] - origins[axis]) / directions[axis];
                if (a > b) std::swap(a, b);
                first = std::max(first, a); last = std::min(last, b);
                if (first > last) return false;
            }
            return true;
        }
        std::optional<float> TriangleHit(Math::Vector3 origin, Math::Vector3 direction,
            Math::Vector3 a, Math::Vector3 b, Math::Vector3 c)
        {
            const XMVECTOR e1 = b - a, e2 = c - a;
            const XMVECTOR p = XMVector3Cross(direction, e2);
            const float det = XMVectorGetX(XMVector3Dot(e1, p));
            // Relative degeneracy test also works for very small or large assets.
            const float magnitude = XMVectorGetX(XMVector3Length(e1)) * XMVectorGetX(XMVector3Length(p));
            if (magnitude == 0 || std::abs(det) <= magnitude * 1e-7f) return {};
            const XMVECTOR t = origin - a;
            const float u = XMVectorGetX(XMVector3Dot(t, p)) / det;
            if (u < 0 || u > 1) return {};
            const XMVECTOR q = XMVector3Cross(t, e1);
            const float v = XMVectorGetX(XMVector3Dot(direction, q)) / det;
            if (v < 0 || u + v > 1) return {};
            const float distance = XMVectorGetX(XMVector3Dot(e2, q)) / det;
            return std::isfinite(distance) ? std::optional<float>(distance) : std::nullopt;
        }
    }

    bool ValidBounds(const AABB& bounds)
    {
        const auto lo = Store(bounds.GetMin()), hi = Store(bounds.GetMax());
        return Finite(lo) && Finite(hi) && lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z;
    }

    std::optional<AABB> WorldBounds(const Model& model)
    {
        if (!model.Asset || !ValidBounds(model.Asset->Bounds)) return {};
        const auto lo = Store(model.Asset->Bounds.GetMin()), hi = Store(model.Asset->Bounds.GetMax());
        const XMMATRIX matrix = model.Transform.GetMatrix();
        AABB world;
        for (int corner = 0; corner < 8; ++corner)
            world.AddPoint(Math::Vector3(XMVector3TransformCoord(XMVectorSet(
                corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z, 1), matrix)));
        return ValidBounds(world) ? std::optional<AABB>(world) : std::nullopt;
    }

    std::optional<AABB> EntityBounds(const Scene& scene, EntityId id)
    {
        if (const auto* model = scene.FindModel(id)) return WorldBounds(*model);
        const TransformComponent* transform = nullptr;
        if (const auto* camera = scene.FindCamera(id)) transform = &camera->Transform;
        if (const auto* light = scene.FindLight(id)) transform = &light->Transform;
        if (!transform) return {};
        const auto position = transform->transform.GetTranslation();
        const Math::Vector3 radius(0.5f, 0.5f, 0.5f);
        return AABB(position - radius, position + radius);
    }

    std::optional<Ray> ViewRay(const Math::Camera& camera, float u, float v)
    {
        if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1) return {};
        XMFLOAT4X4 projection;
        XMStoreFloat4x4(&projection, camera.GetProjMatrix());
        const auto direction = Math::Normalize(camera.GetForwardVec() + camera.GetRightVec() * ((2 * u - 1) / projection._11)
            + camera.GetUpVec() * ((1 - 2 * v) / projection._22));
        const float cosine = XMVectorGetX(XMVector3Dot(direction, camera.GetForwardVec()));
        if (!Finite(Store(direction)) || cosine <= 0) return {};
        return Ray{camera.GetPosition(), direction, camera.GetNearClip() / cosine, camera.GetFarClip() / cosine};
    }

    EntityId PickModel(const Scene& scene, const Ray& ray)
    {
        if (!Finite(Store(ray.Origin)) || !Finite(Store(ray.Direction)) || ray.MinDistance < 0 ||
            !std::isfinite(ray.MinDistance) || !std::isfinite(ray.MaxDistance) || ray.MaxDistance < ray.MinDistance) return InvalidEntityId;
        EntityId hit = InvalidEntityId;
        float nearest = ray.MaxDistance;
        for (const auto& model : scene.Models)
        {
            if (!model.Asset || !model.GPU || !std::isfinite(model.Transform.scale) || model.Transform.scale <= 0) continue;
            const auto world = WorldBounds(model);
            if (!world || !BoxHit(*world, ray.Origin, ray.Direction, ray.MinDistance, nearest)) continue;
            const XMMATRIX inverse = XMMatrixInverse(nullptr, model.Transform.GetMatrix());
            const Math::Vector3 origin(XMVector3TransformCoord(ray.Origin, inverse));
            // Do not normalize: t remains distance along the original world ray.
            const Math::Vector3 direction(XMVector3TransformNormal(ray.Direction, inverse));
            for (const auto& mesh : model.Asset->Meshes)
            {
                if (!BoxHit(mesh.Bounds, origin, direction, ray.MinDistance, nearest)) continue;
                for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
                {
                    const auto a = mesh.Indices[i], b = mesh.Indices[i + 1], c = mesh.Indices[i + 2];
                    if (a >= mesh.Vertices.size() || b >= mesh.Vertices.size() || c >= mesh.Vertices.size()) continue;
                    const auto distance = TriangleHit(origin, direction, mesh.Vertices[a].Position, mesh.Vertices[b].Position, mesh.Vertices[c].Position);
                    if (distance && *distance >= ray.MinDistance && *distance <= nearest)
                    {
                        if (hit && *distance == nearest) continue;
                        nearest = *distance; hit = model.Id;
                    }
                }
            }
        }
        return hit;
    }
}
