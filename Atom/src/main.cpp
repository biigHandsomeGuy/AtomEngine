#include "pch.h"

#define FMT_UNICODE 0
#include "Application.h"

#include "d3dUtil.h"
extern Application* CreateApplication(HINSTANCE hInstance);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    auto app = CreateApplication(hInstance);
    try
    {
        if (!app->Initialize())
            return 0;

        return app->Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }


}