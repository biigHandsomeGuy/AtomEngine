#pragma once

#ifdef ATOM_PLATFORM_WINDOWS
	#ifdef DX_BUILD_DLL
		#define ATOM_API __declspec(dllexport)
	#else
		#define ATOM_API __declspec(dllimport)
	#endif // DX_BUILD_DLL
#endif