workspace "Atom"
	architecture "x64"

	configurations
	{
		"Debug",
		"Release"
	}

	outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Atom"
	location "Atom"
	kind "StaticLib"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
		"%{prj.name}/Vendor/Imgui/**.h",
		"%{prj.name}/Vendor/Imgui/**.cpp",
		"%{prj.name}/Vendor/stb_image/**.h",
		"%{prj.name}/Vendor/stb_image/**.cpp",

	}

	includedirs 
	{
		"%{prj.name}/Vendor/spdlog/include",
		"%{prj.name}/Vendor/imgui",
		"%{prj.name}/Vendor/stb_image/include",
		"%{prj.name}/Vendor/assimp/include",
	}

	libdirs
	{
		"%{prj.name}/Vendor/assimp/Win64"
	}

	links
	{
		"assimp-vc143-mt"
	}

	postbuildcommands
	{
		("{copy} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
	}

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"

		defines
		{
			"DX_BUILD_DLL",
			"ATOM_PLATFORM_WINDOWS",
			"_WINDOWS"
		}

project "Sandbox"
	location "Atom"
	kind "WindowedApp"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
	}

	includedirs
	{
		"Atom/Vendor/spdlog/include",
		"Atom/Vendor/imgui",
		"Atom/Vendor/stb_image",
		"Atom/Vendor/assimp/include",
		"Atom/src",
	}

	links
	{
		"Atom"
	}

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"

		defines
		{
			"ATOM_PLATFORM_WINDOWS",
			"_WINDOWS"
		}