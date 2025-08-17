#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class CommandListManager;


namespace GameCore
{
	extern HWND g_hWnd;
	extern bool isResize;
	// frame work
	class IGameApp
	{
	public:

		virtual void Startup(void) = 0;
		virtual void Cleanup(void) = 0;

		virtual bool IsDone(void) { return false; };

		virtual void Update(float deltaT) = 0;
		virtual void OnResize() = 0;
		virtual void RenderScene(void) = 0;
	public:

		GameTimer m_Timer;
		
	};
}

namespace GameCore
{
	int RunApplication(IGameApp& app, const wchar_t* className, HINSTANCE hInst, int nCmdShow);
}

