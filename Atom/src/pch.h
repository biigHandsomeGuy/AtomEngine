
// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX apps don't need GDI
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP

// Include <mcx.h> if you need this
#define NOMCX

// Include <winsvc.h> if you need this
#define NOSERVICE

// WinHelp is deprecated
#define NOHELP

#define WIN32_LEAN_AND_MEAN

#define D3D12_GPU_VIRTUAL_ADDRESS_NULL      ((D3D12_GPU_VIRTUAL_ADDRESS)0)
#define D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN   ((D3D12_GPU_VIRTUAL_ADDRESS)-1)

#include <wingdi.h>
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXPackedVector.h>
#include <d3dcompiler.h>
#include <Windows.h>
#include <wrl/client.h>
#include "d3dx12.h"
#include <comdef.h>


#include <queue>
#include <vector>
#include <array>
#include <unordered_map>
#include <map>

#include <utility>
#include <mutex>
#include <string>

#include <cstdio>
#include <sstream>
#include <fstream>
#include <iostream>

#include "d3dUtil.h"
#include "VectorMath.h"

