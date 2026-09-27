#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <unordered_set>
#include <vector>
#include <variant>
#include <type_traits>
#include <stdexcept>
#include "Camera.h"

#include "Model.h"
#include "Material.h"

struct CameraComponent
{
    float VerticalFovDegrees = 45.0f;
    float NearClip = 0.1f;
    float FarClip = 10000.0f;
};

struct DirectionalLightComponent
{
    DirectX::XMFLOAT3 Color{ 1, 1, 1 }; // Linear RGB radiance multiplier.
    float Intensity = 1.0f;
    bool Enabled = true;
    bool CastsShadows = true;
    DirectX::XMFLOAT3 ShadowCenter{ 10, 10, -10 };
    DirectX::XMFLOAT3 ShadowBounds{ 5000, 3000, 3000 };
};

struct CameraEntity
{
    EntityId Id = InvalidEntityId;
    std::string InstanceName = "Camera";
    TransformComponent Transform;
    CameraComponent Camera;
};

struct DirectionalLightEntity
{
    EntityId Id = InvalidEntityId;
    std::string InstanceName = "Directional Light";
    TransformComponent Transform; // Local -Z is the direction of emitted rays.
    DirectionalLightComponent Light;
};

struct Scene
{
	std::vector<Model> Models;
    std::vector<CameraEntity> Cameras;
    std::vector<DirectionalLightEntity> Lights;
    using EntityData = std::variant<std::shared_ptr<Model>, std::shared_ptr<CameraEntity>, std::shared_ptr<DirectionalLightEntity>>;
    std::vector<UINT> MaterialIndex;

    size_t EntityCount() const { return Models.size() + Cameras.size() + Lights.size(); }
    bool Contains(EntityId id) const { return FindModel(id) || FindCamera(id) || FindLight(id); }
    CameraEntity* FindCamera(EntityId id) { return Find(Cameras, id); }
    const CameraEntity* FindCamera(EntityId id) const { return Find(Cameras, id); }
    DirectionalLightEntity* FindLight(EntityId id) { return Find(Lights, id); }
    const DirectionalLightEntity* FindLight(EntityId id) const { return Find(Lights, id); }
    CameraEntity& AddCamera(const CameraEntity& camera) { return Add(Cameras, std::move(camera)); }
    DirectionalLightEntity& AddLight(const DirectionalLightEntity& light) { return Add(Lights, std::move(light)); }
    EntityId ActiveCameraId() const { return m_ActiveCamera; }
    EntityId ActiveLightId() const { return m_ActiveLight; }
    bool SetActiveCamera(EntityId id)
    {
        if (id != InvalidEntityId && !FindCamera(id)) return false;
        if (m_ActiveCamera != id) { m_ActiveCamera = id; MarkModified(); }
        return true;
    }
    bool SetActiveLight(EntityId id)
    {
        if (id != InvalidEntityId && !FindLight(id)) return false;
        if (m_ActiveLight != id) { m_ActiveLight = id; MarkModified(); }
        return true;
    }
    std::string* EntityName(EntityId id)
    {
        if (auto* item = FindModel(id)) return &item->InstanceName;
        if (auto* item = FindCamera(id)) return &item->InstanceName;
        if (auto* item = FindLight(id)) return &item->InstanceName;
        return nullptr;
    }
    TransformComponent* EntityTransform(EntityId id)
    {
        if (auto* item = FindModel(id)) return &item->Transform;
        if (auto* item = FindCamera(id)) return &item->Transform;
        if (auto* item = FindLight(id)) return &item->Transform;
        return nullptr;
    }
    EntityId FirstEntityId() const
    {
        if (!Models.empty()) return Models.front().Id;
        if (!Cameras.empty()) return Cameras.front().Id;
        return Lights.empty() ? InvalidEntityId : Lights.front().Id;
    }
    size_t EntityIndex(EntityId id) const
    {
        if (FindModel(id)) return Index(Models, id);
        if (FindCamera(id)) return Index(Cameras, id);
        return Index(Lights, id);
    }
    std::optional<EntityData> ExtractEntity(EntityId id)
    {
        std::optional<EntityData> result;
        if (FindModel(id)) result = ExtractOwned(Models, id);
        else if (FindCamera(id)) result = ExtractOwned(Cameras, id);
        else if (FindLight(id)) result = ExtractOwned(Lights, id);
        if (result)
        {
            if (m_ActiveCamera == id) m_ActiveCamera = InvalidEntityId;
            if (m_ActiveLight == id) m_ActiveLight = InvalidEntityId;
            MarkModified();
        }
        return result;
    }
    bool InsertEntity(size_t index, EntityData&& data)
    {
        return std::visit([this, index](auto&& pointer)
        {
            if (!pointer) return false;
            auto& item = *pointer;
            using T = std::decay_t<decltype(item)>;
            if (item.Id != InvalidEntityId && Contains(item.Id)) return false;
            PrepareId(item.Id);
            if constexpr (std::is_same_v<T, Model>) Insert(Models, index, std::move(item));
            else if constexpr (std::is_same_v<T, CameraEntity>) Insert(Cameras, index, std::move(item));
            else Insert(Lights, index, std::move(item));
            MarkModified();
            return true;
        }, std::move(data));
    }
    static DirectionalLightEntity DefaultSun()
    {
        DirectionalLightEntity light;
        light.InstanceName = "Sun";
        Math::Camera orientation;
        orientation.SetLookDirection(Math::Normalize(Math::Vector3(-15, -15, 15)), Math::Vector3(Math::kYUnitVector));
        light.Transform.transform.SetRotation(orientation.GetRotation());
        return light;
    }

    // Validate the entire replacement before changing any active scene data.
    void ReplaceContent(std::vector<Model> models, std::vector<CameraEntity> cameras,
        std::vector<DirectionalLightEntity> lights, EntityId activeCamera, EntityId activeLight, EntityId nextId)
    {
        std::unordered_set<EntityId> ids;
        EntityId requiredNext = 1;
        auto validate = [&](const auto& list)
        {
            for (const auto& item : list)
            {
                if (item.Id == InvalidEntityId || item.Id >= UINT64_MAX - 1 || !ids.insert(item.Id).second)
                    throw std::invalid_argument("Scene entity IDs must be unique and valid.");
                requiredNext = std::max(requiredNext, item.Id + 1);
            }
        };
        validate(models); validate(cameras); validate(lights);
        if ((activeCamera != InvalidEntityId && !Find(cameras, activeCamera)) ||
            (activeLight != InvalidEntityId && !Find(lights, activeLight)) || nextId == InvalidEntityId || nextId == UINT64_MAX)
            throw std::invalid_argument("Invalid scene camera, light or next entity ID.");
        Models.swap(models); Cameras.swap(cameras); Lights.swap(lights);
        m_ActiveCamera = activeCamera; m_ActiveLight = activeLight;
        m_NextEntityId = std::max(nextId, requiredNext);
        MaterialIndex.clear();
        MarkModified();
    }

	uint64_t Revision() const { return m_Revision; }
	void MarkModified() { ++m_Revision; }

	Model& AddModel(Model&& model)
	{
		PrepareModelId(model);

		Models.push_back(std::move(model));
		MarkModified();
		return Models.back();
	}

	bool InsertModel(size_t index, Model&& model)
	{
		if (model.Id != InvalidEntityId && Contains(model.Id))
			return false;
		PrepareModelId(model);
		Models.insert(Models.begin() + std::min(index, Models.size()), std::move(model));
		MarkModified();
		return true;
	}

	std::optional<Model> ExtractModel(EntityId id)
	{
		auto found = std::find_if(Models.begin(), Models.end(), [id](const Model& model) { return model.Id == id; });
		if (found == Models.end())
			return std::nullopt;
		Model model = std::move(*found);
		Models.erase(found);
		MarkModified();
		return model;
	}

	Model* FindModel(EntityId id)
	{
		if (id == InvalidEntityId)
			return nullptr;
		auto found = std::find_if(Models.begin(), Models.end(), [id](const Model& model) { return model.Id == id; });
		return found == Models.end() ? nullptr : &*found;
	}

	const Model* FindModel(EntityId id) const
	{
		if (id == InvalidEntityId)
			return nullptr;
		auto found = std::find_if(Models.begin(), Models.end(), [id](const Model& model) { return model.Id == id; });
		return found == Models.end() ? nullptr : &*found;
	}

	bool RemoveModel(EntityId id)
	{
		return ExtractModel(id).has_value();
	}

	void ReplaceModels(std::vector<Model> models)
	{
		std::unordered_set<EntityId> ids;
        ids.reserve(models.size() + Cameras.size() + Lights.size());
        EntityId nextId = 1;
        for (const auto& camera : Cameras) { ids.insert(camera.Id); nextId = std::max(nextId, camera.Id + 1); }
        for (const auto& light : Lights) { ids.insert(light.Id); nextId = std::max(nextId, light.Id + 1); }
		for (auto& model : models)
		{
			if (model.Id == InvalidEntityId || ids.count(model.Id) != 0)
			{
				do
				{
					model.Id = nextId++;
					if (nextId == InvalidEntityId) nextId = 1;
				} while (model.Id == InvalidEntityId || ids.count(model.Id) != 0);
			}
			else if (model.Id >= nextId)
				nextId = model.Id == UINT64_MAX ? InvalidEntityId : model.Id + 1;
			ids.insert(model.Id);
		}
		Models.swap(models);
		MaterialIndex.clear();
		m_NextEntityId = nextId;
		MarkModified();
	}

	EntityId GetNextEntityId() const { return m_NextEntityId; }

	void SetNextEntityId(EntityId id)
	{
		if (id != InvalidEntityId && id != UINT64_MAX && id > m_NextEntityId)
		{
			m_NextEntityId = id;
			MarkModified();
		}
	}


private:
    template<class T> static T* Find(std::vector<T>& list, EntityId id)
    {
        if (id == InvalidEntityId) return nullptr;
        auto it = std::find_if(list.begin(), list.end(), [id](const auto& item) { return item.Id == id; });
        return it == list.end() ? nullptr : &*it;
    }
    template<class T> static const T* Find(const std::vector<T>& list, EntityId id)
    {
        if (id == InvalidEntityId) return nullptr;
        auto it = std::find_if(list.begin(), list.end(), [id](const auto& item) { return item.Id == id; });
        return it == list.end() ? nullptr : &*it;
    }
    template<class T> static size_t Index(const std::vector<T>& list, EntityId id)
    {
        return std::distance(list.begin(), std::find_if(list.begin(), list.end(), [id](const auto& item) { return item.Id == id; }));
    }
    template<class T> static T Extract(std::vector<T>& list, EntityId id)
    {
        auto it = list.begin() + Index(list, id);
        T result = std::move(*it);
        list.erase(it);
        return result;
    }
    template<class T> static std::shared_ptr<T> ExtractOwned(std::vector<T>& list, EntityId id)
    {
        auto payload = std::make_shared<T>(); // Allocate before changing membership.
        *payload = Extract(list, id);
        return payload;
    }
    template<class T> static void Insert(std::vector<T>& list, size_t index, T&& item)
    {
        list.insert(list.begin() + std::min(index, list.size()), std::move(item));
    }
    template<class T> T& Add(std::vector<T>& list, T item)
    {
        PrepareId(item.Id);
        list.push_back(std::move(item));
        MarkModified();
        return list.back();
    }
    void PrepareModelId(Model& model) { PrepareId(model.Id); }
    void PrepareId(EntityId& id)
    {
        if (id == InvalidEntityId || id >= UINT64_MAX - 1 || Contains(id)) id = AllocateEntityId();
        else if (id >= m_NextEntityId) m_NextEntityId = id + 1;
    }
    EntityId AllocateEntityId()
    {
        for (;;)
        {
            EntityId candidate = m_NextEntityId++;
            if (m_NextEntityId >= UINT64_MAX - 1) m_NextEntityId = 1;
            if (candidate != InvalidEntityId && candidate < UINT64_MAX - 1 && !Contains(candidate)) return candidate;
        }
    }
    EntityId m_ActiveCamera = InvalidEntityId;
    EntityId m_ActiveLight = InvalidEntityId;

	EntityId m_NextEntityId = 1;
	uint64_t m_Revision = 0;
};
