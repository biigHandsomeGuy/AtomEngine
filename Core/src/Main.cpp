
#include "d3dApp.h"
#include <memory>
std::unique_ptr<D3DApp> g_App;
extern D3DApp* CreateApp(HINSTANCE hInstance);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd){
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        g_App.reset(CreateApp(hInstance));
        if (!g_App->Initialize())
            return 0;
        
        return g_App->Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}
