#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx12.h"
#include "imgui/backends/imgui_impl_win32.h"

#include "CommandListManager.h"
#include "Display.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace Graphics;

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;
	
		switch (msg)
		{
			// WM_SIZE is sent when the user resizes the window.  
		case WM_SIZE:
			Display::Resize((UINT)(UINT64)lParam & 0xFFFF, (UINT)(UINT64)lParam >> 16);
			GameCore::isResize = true;
			return 0;

			// WM_DESTROY is sent when the window is being destroyed.
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		
		}	
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

// void GameCore::CalculateFrameStats()
// {
// 	// 该代码计算每秒帧速，并计算每一帧渲染需要的时间，显示在窗口标题
// 	static int frameCnt = 0;
// 	static float timeElapsed = 0.0f;
// 
// 	frameCnt++;
// 
// 	if ((m_Timer.TotalTime() - timeElapsed) >= 1.0f) {
// 		float fps = (float)frameCnt; // fps = frameCnt / 1
// 		float mspf = 1000.0f / fps;
// 
// 		std::wostringstream outs;
// 		outs.precision(6);
// 		outs << mMainWndCaption << L"    "
// 			<< L"FPS: " << fps << L"    "
// 			<< L"Frame Time: " << mspf << L" (ms)";
// 		SetWindowText(g_hWnd, outs.str().c_str());
// 
// 		// Reset for next average.
// 		frameCnt = 0;
// 		timeElapsed += 1.0f;
// 	}
// }
namespace GameCore
{
	bool isResize = false;

	HWND g_hWnd = nullptr;
	void InitializeApplication(IGameApp& game)
	{
		Graphics::Initialize();

		game.Startup();
	}

	bool UpdateApplication(IGameApp& game)
	{
		if (isResize)
		{
			game.OnResize();
			isResize = false;
		}
		game.Update(1);
		game.RenderScene();
		
		Display::Present();
		return 1;
	}

	void TerminateApplication(IGameApp& game)
	{
		
	}

	int GameCore::RunApplication(IGameApp& app, const wchar_t* className, HINSTANCE hInst, int nCmdShow)
	{
		// Register class
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = MainWndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInst;
		wcex.hIcon = LoadIcon(hInst, IDI_APPLICATION);
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = className;
		wcex.hIconSm = LoadIcon(hInst, IDI_APPLICATION);

		if (!RegisterClassEx(&wcex))
		{
			MessageBox(0, L"RegisterClass Failed.", 0, 0);
			return false;
		}

		// Compute window rectangle dimensions based on requested client area dimensions.
		RECT R = { 0, 0, g_DisplayWidth, g_DisplayHeight };
		AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
		int width = R.right - R.left;
		int height = R.bottom - R.top;

		g_hWnd = CreateWindow(className, className, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
			width, height, nullptr, nullptr, hInst, nullptr);

		if (!g_hWnd)
		{
			MessageBox(0, L"CreateWindow Failed.", 0, 0);
			return false;
		}

		InitializeApplication(app);

		ShowWindow(g_hWnd, SW_SHOW);
		UpdateWindow(g_hWnd);


		MSG msg = { 0 };

		while (msg.message != WM_QUIT)
		{
			// If there are Window messages then process them.
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			// Otherwise, do animation/game stuff.
			else
			{
				UpdateApplication(app);
			}
		}
		TerminateApplication(app);
		Graphics::Shutdown();
		                                                            
		return 0;
	}

}
