#pragma once

#include <entt/entt.hpp>



class Scene
{
public:
	Scene();
	~Scene();

private:
	entt::registry m_Registry;

};