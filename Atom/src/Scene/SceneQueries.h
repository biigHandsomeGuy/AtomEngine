#pragma once

#include "SceneGraph.h"

namespace SceneQueries
{
    struct Ray
    {
        Math::Vector3 Origin;
        Math::Vector3 Direction; // Unit world direction. Distances use world units.
        float MinDistance = 0;
        float MaxDistance = FLT_MAX;
    };

    bool ValidBounds(const AABB& bounds);
    std::optional<AABB> WorldBounds(const Model& model);
    std::optional<AABB> EntityBounds(const Scene& scene, EntityId id);
    std::optional<Ray> ViewRay(const Math::Camera& camera, float u, float v);
    // CPU triangle picking after bounds rejection, nearest visible-range hit.
    // Geometry is tested from both sides; transparency is not evaluated.
    EntityId PickModel(const Scene& scene, const Ray& ray);
}
