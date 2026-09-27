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
- **Scenes describe instances; assets describe reusable content.** A scene object holds an asset reference and a transform. GPU buffers and textures belong to shared assets, not to each object. Runtime scene objects now receive stable 64-bit IDs, selection resolves through those IDs, and scene format version 2 persists them (the loader still accepts version 1).
- **A render graph is the destination for frame orchestration.** Passes declare reads, writes, queue requirements, and outputs. The graph owns resource lifetime and transitions; individual passes own PSOs and draw/dispatch logic. Existing passes migrate one at a time.
- **Frame resources are fence-owned.** Upload memory, transient descriptors, and per-frame command allocators are retired only after their frame fence completes. GPU uploads move off the synchronous model-loading path once the asset workflow exists.
- **The editor viewport is an explicit render view.** Its camera, pixel size, render target, and temporal history are independent from the main window and other future views.

## Migration stages

1. Add a standalone editor target and usable scene hierarchy, inspector, content browser, and scene files while sharing the stable PBR renderer.
2. Move editor UI orchestration and document commands fully out of `PbrRenderer`; add an undoable command boundary. Stable IDs and ID-based selection are in place.
3. Introduce an explicit engine/application lifetime and a per-viewport render-view API. Remove renderer lifecycle from process-wide globals.
4. Add a small render graph for the current forward PBR path: depth/normal, shadow, AO, opaque+sky, temporal/post, editor composition. Compare output against the current path after each pass moves. The graphs now own depth/normal, shadow depth and publication, SSAO dependencies, opaque+sky rendering, and temporal resolve/history publication. The display composition and editor UI remain on the legacy path until migrated.
5. Separate CPU assets from GPU residency and make import/upload asynchronous with cancellation and diagnostics.
6. Add broader renderer features (deferred/clustered lighting, multithreaded recording, richer post) only on top of the graph and frame-resource ownership.

## First editor milestone

The editor starts from the current working PBR scene, allows selecting and transforming model instances, imports `.gltf`/`.glb` assets from the content browser, and saves/loads a portable scene document. It keeps the viewport and post-processing controls available while making scene editing the main workflow.
