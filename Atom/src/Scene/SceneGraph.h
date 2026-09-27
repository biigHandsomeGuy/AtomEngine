#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <unordered_set>
#include <vector>

#include "Model.h"
#include "Material.h"

struct Scene
{
	std::vector<Model> Models;
	std::vector<UINT> MaterialIndex;

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
		if (model.Id != InvalidEntityId && FindModel(model.Id) != nullptr)
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
		ids.reserve(models.size());
		EntityId nextId = 1;
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
		if (id != InvalidEntityId && id > m_NextEntityId)
		{
			m_NextEntityId = id;
			MarkModified();
		}
	}

	private:
	void PrepareModelId(Model& model)
	{
		if (model.Id == InvalidEntityId || FindModel(model.Id) != nullptr)
			model.Id = AllocateEntityId();
		else if (model.Id >= m_NextEntityId)
			m_NextEntityId = model.Id == UINT64_MAX ? InvalidEntityId : model.Id + 1;
	}

	EntityId AllocateEntityId()
	{
		for (;;)
		{
			EntityId candidate = m_NextEntityId++;
			if (m_NextEntityId == InvalidEntityId)
				m_NextEntityId = 1;
			if (candidate != InvalidEntityId && FindModel(candidate) == nullptr)
				return candidate;
		}
	}

	EntityId m_NextEntityId = 1;
	uint64_t m_Revision = 0;
};
