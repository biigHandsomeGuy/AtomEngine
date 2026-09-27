# AtomEngine

Windows / Direct3D 12 renderer with a standalone editor and a PBR sample.

## Build and run

Install Visual Studio C++ tools (v143), the Windows SDK, and PowerShell 7. Initialize the repository's dependency submodules, then run from the repository root:

```powershell
git submodule update --init --recursive
pwsh -File scripts/Build-Pbr.ps1 -Target All -Configuration Debug -SmokeTest
```

The script builds only PBR and AtomEditor and runs each for 60 frames, including four view sizes, odd-size SSAO, TAA toggling/history reset, a real main-window resize, and resource-isolation checks. It also checks asynchronous model loading, shared asset residency, malformed/missing-file diagnostics, and cancellation before/after GPU submission. Debug smoke tests require D3D12 validation and fail on validation errors, device removal, failed view/asset checks, premature exit, or timeout. Logs are saved under `bin-int/build-logs`. Smoke runs do not save ImGui layout preferences. Windows Graphics Tools must be available for Debug validation.

```powershell
# Interactive editor
& .\bin\Debug-windows-x86_64\AtomEditor\AtomEditor.exe
# Standalone rendering sample
& .\bin\Debug-windows-x86_64\PBR\PBR.exe
# Release build and basic runtime check (without the Debug validation layer)
pwsh -File scripts/Build-Pbr.ps1 -Target All -Configuration Release -SmokeTest
```

The editor provides scene hierarchy, transforms, asynchronous model import with status/cancellation, scene save/load, and undo/redo. Its view owns its render targets, camera, and temporal history and renders at the panel's pixel dimensions. See [architecture and migration stages](docs/architecture/editor-and-renderer.md) for the implemented boundaries and remaining work.

#### Some features of AtomEngine


![](./screenshot/pbr.jpg)
> pbr

![](./screenshot/skin.png)
> skin shading

#### FEATURE
- [x] Physicis Based Rendering
- [x] Image Based Lighting(Spilt Sum Approximate)
- [x] pecentage Closer Soft Shadow Mapping
- [x] Screen Space Ambient Occlusion
- [x] Fast Approximate Anti Alising
- [x] Pre-integral Sub-Surface Scatter
- [ ] Async compute scheduling (the PBR frame currently uses one direct queue)
- [x] Editor scene workflow (hierarchy, inspector, import, save/load, undo/redo)
- [ ] Deffer Rendering
- [x] Bloom (screen-space bright extraction and blur)
- [ ] Depth of Field
- [ ] Motion Blur
- [ ] MultiThread
- [ ] SSSS

#### Refrence

MiniEngine

DiligentEngine

d3d12book
