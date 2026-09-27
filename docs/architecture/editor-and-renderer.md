# AtomEngine editor and renderer direction

## Target shape

Keep the native Direct3D 12 backend. Replace global, sample-owned control flow in measured steps instead of rewriting the backend before there is a working editor.

```text
Editor application
  ├─ Editor document, selection, undo/redo, panels, commands
  ├─ Scene: stable entities + Transform, Camera, Light, and Model components
  ├─ Asset database: source paths/IDs, import settings, CPU asset data
  └─ Engine runtime
       ├─ Renderer: view extraction and frame submission
       ├─ Render graph: pass/resource dependencies and barriers
       └─ D3D12 backend: device, queues, frame contexts, descriptors, resources
```

## Decisions

- **Direct3D 12 remains the backend.** It already supports the project's bindless materials, async compute, temporal post processing, and Windows target. A second backend would dilute effort without helping editor workflows.
- **The editor is a separate application target.** `PBR` remains a useful sample. Both targets reuse the same engine and scene renderer; editor panels do not live inside the renderer implementation over time.
- **Scenes describe instances; assets describe reusable content.** A scene object holds an asset reference and a transform. GPU buffers and textures belong to shared assets, not to each object. Runtime scene objects now receive stable 64-bit IDs, selection resolves through those IDs, and scene format version 2 persists them (the loader still accepts version 1). Editor edits, imports, and deletions use an explicit command boundary with undo/redo; opening a different document clears the previous history.
- **A render graph is the destination for frame orchestration.** Passes declare reads, writes, queue requirements, and outputs. The graph owns resource lifetime and transitions; individual passes own PSOs and draw/dispatch logic. Existing passes migrate one at a time.
- **Frame resources are fence-owned.** Upload memory, transient descriptors, and per-frame command allocators are retired only after their frame fence completes. GPU uploads move off the synchronous model-loading path once the asset workflow exists.
- **The editor viewport is an explicit render view.** Its camera, pixel size, render target, and temporal history are independent from the main window and other future views.

## Migration stages

1. Add a standalone editor target and usable scene hierarchy, inspector, content browser, and scene files while sharing the stable PBR renderer.
2. Move editor UI orchestration and document commands fully out of `PbrRenderer`; add an undoable command boundary. Implemented: `EditorMain` creates `EditorLayer`, which owns panels, selection, scene dialogs, and command history. `PbrMain` creates a sample UI with no editor dependency. Both use `PbrApplication` for ImGui and application lifecycle. `PbrRenderer` takes a scene and records rendering into a caller-owned command context; it has no ImGui dependency or entry point.
3. Introduce an explicit engine/application lifetime and a per-viewport render-view API. The application now owns the scene, renderer, UI, and shutdown order. GPU work finishes before UI and scene resources are released, and cleanup precedes device shutdown. Render targets and input capture still use process-wide state; independent viewport size, camera control, targets, and history remain the next migration.
4. Add a small render graph for the current forward PBR path: depth/normal, shadow, AO, opaque+sky, temporal/post, editor composition. Compare output against the current path after each pass moves. The graphs now own depth/normal, shadow depth and publication, SSAO dependencies, opaque+sky rendering, and temporal resolve/history publication. The display composition and editor UI remain on the legacy path until migrated.
5. Separate CPU assets from GPU residency and make import/upload asynchronous with cancellation and diagnostics.
6. Add broader renderer features (deferred/clustered lighting, multithreaded recording, richer post) only on top of the graph and frame-resource ownership.

## First editor milestone

The editor starts from the current working PBR scene, allows selecting and transforming model instances, imports `.gltf`/`.glb` assets from the content browser, and saves/loads a portable scene document. It keeps the viewport and post-processing controls available while making scene editing the main workflow.

## Current frame and resource rules

- `PbrApplication` builds UI, records geometry, AO, lighting, and post processing, then draws the main UI on the same direct command list. The scene output is ready before the viewport samples it. Secondary ImGui windows submit afterwards. Compute shaders still run, but this path currently makes no async compute overlap claim.
- The main backbuffer transitions to render target before UI drawing and to present afterwards. The viewport preserves the scene target's aspect ratio.
- A directly indexed root signature binds its descriptor heap before setting the signature. The command-context cache restores both after a pass switches to a dynamic heap, even when the signature pointer is unchanged.
- Texture SRVs retain a CPU descriptor mirror for descriptor copies; shader-visible descriptors are destinations. Resource transitions update the owning wrapper, including initial texture uploads and the SSAO random texture.
- IBL specular prefilter constants use a floating-point roughness matching the shader constant buffer.
- Frame submission currently waits for completion. Moving to multiple frames in flight requires fence-owned upload allocations, descriptors, and target/history residency first.

## Reproducible validation

Use PowerShell 7 from the repository root:

```powershell
pwsh -File scripts/Build-Pbr.ps1 -Target All -Configuration Debug -SmokeTest
pwsh -File scripts/Build-Pbr.ps1 -Target All -Configuration Release -SmokeTest
```

The build script discovers MSBuild, fixes the solution root, and normalizes inherited environment variable names. PBR, AtomEditor, Atom core, and ImGui use separate intermediate directories; the dependency project is configured through project-reference properties. The script does not modify dependency source files.

Each runtime check renders 60 frames and exits through normal application cleanup. The log records frame count, D3D12 error count, device removal, and validation availability. Debug checks require the validation interface, with GPU validation enabled by the engine. Release checks exercise startup, frames, and shutdown without claiming Debug-layer coverage. These checks do not replace visual comparison, resize testing, or interactive editor workflow checks.
