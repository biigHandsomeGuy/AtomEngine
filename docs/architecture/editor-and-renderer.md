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
3. Introduce an explicit engine/application lifetime and a per-viewport render-view API. Implemented: the application owns the scene, `RenderView`, renderer, camera controller, UI, and shutdown order. Each view owns its camera, pixel dimensions, color/depth/normal targets, shadow target, SSAO intermediates, and TAA history/state. UI returns a `ViewportRequest` in pixels; the application applies it before rendering. Presentation resize no longer recreates PBR view resources. GPU work finishes before target recreation and cleanup. Input capture and device/shared IBL resources still use process-wide state.
4. Add a small render graph for the current forward PBR path: depth/normal, shadow, AO, opaque+sky, temporal/post, editor composition. Compare output against the current path after each pass moves. The graphs now own depth/normal, shadow depth and publication, SSAO dependencies, opaque+sky rendering, and temporal resolve/history publication. The display composition and editor UI remain on the legacy path until migrated.
5. Separate CPU assets from GPU residency and make import/upload asynchronous with cancellation and diagnostics. Implemented: CPU-only glTF decoding produces meshes, decoded RGBA images, and material image indices. `ModelAssetManager` owns a CPU worker and canonical-path cache. The render thread submits one complete asset upload per update and publishes residency only after its fence. Instances share immutable CPU assets and GPU geometry/materials. The content browser exposes task state/cancellation and records a scene command only when an asset is ready. `SceneDocumentLoader` parses documents on its own worker, requests shared assets incrementally, and commits the complete scene on the render thread.
6. Add broader renderer features (deferred/clustered lighting, multithreaded recording, richer post) only on top of the graph and frame-resource ownership.

## First editor milestone

The editor starts from the current working PBR scene, allows selecting and transforming model instances, imports `.gltf`/`.glb` assets from the content browser, and saves/loads a portable scene document. It keeps the viewport and post-processing controls available while making scene editing the main workflow.

## Current frame and resource rules

- `PbrApplication` builds UI, records geometry, AO, lighting, and post processing, then draws the main UI on the same direct command list. It submits without a per-frame completion wait. Secondary ImGui windows use the same direct queue and submit afterwards, before the main window presents. Same-queue ordering protects view targets/history across queued frames and detached windows. Compute shaders still run, but this path currently makes no async compute overlap claim.
- ImGui uses its current DX12 initialization API with the engine's direct queue and descriptor allocation/free callbacks. The backend manages dynamic font textures; the application does not call the legacy font-atlas `Build` method. Retired UI descriptor slots are reused within the application session.
- The main backbuffer transitions to render target before UI drawing and to present afterwards. The scene target matches the viewport panel's pixel dimensions; camera aspect and TAA history update when that target changes. Zero-sized requests preserve the previous target. Target dimensions are clamped to 8–16384; odd dimensions use rounded-up half-resolution SSAO targets and clamped edge loads.
- The primary swapchain obtains its current backbuffer index from `IDXGISwapChain3` after creation, resize and Present, including occluded/minimized states. Application frame slots and UI backend buffer rings are bounded independently of this presentation index.
- A directly indexed root signature binds its descriptor heap before setting the signature. The command-context cache restores both after a pass switches to a dynamic heap, even when the signature pointer is unchanged.
- Texture SRVs retain a CPU descriptor mirror for descriptor copies; shader-visible descriptors are destinations. Resource transitions update the owning wrapper, including initial texture uploads and the SSAO random texture.
- IBL specular prefilter constants use a floating-point roughness matching the shader constant buffer.
- Geometry, lighting, post, and SSAO graphs reference view-owned resources. PBR descriptor tables are local to the renderer/view pair, and PSO creation receives explicit attachment formats. Two immutable post tables reference the two TAA history variants; these and the common table are rewritten only after GPU-idle view resize. PBR/Editor disable the legacy display-sized scene buffers; the shared IBL textures and SSAO random texture remain device-scoped. The default legacy-buffer path is retained for the other samples.
- `FrameScheduler` owns three application slots. Begin waits only when reusing an incomplete slot, then releases that slot's retained owners. UI viewport descriptors are indexed by this slot, independently of the swapchain backbuffer index. Each recorded scene frame retains distinct shared `ModelGPU` owners until its slot fence completes. `GameCore::OnFramePresented` lets the application signal its fence after main rendering, texture uploads, detached-window rendering and presentation. The application drains its slots before UI/scene/renderer cleanup; view and swapchain resize still idle the GPU. Command allocators, constant/upload pages and dynamic descriptor heaps retire against their individual submission fences.
- Scene membership, inspector edits, command undo/redo, and document commits increment the scene revision. The renderer invalidates view history when that revision changes, preventing old TAA samples from surviving an edit.

## Asset loading rules

```text
Request → Queued → Decoding (CPU worker) → ReadyForUpload
        → Uploading (render thread submission + fence) → Ready → scene instance
        ↘ Failed / Cancelled
```

- CPU materials reference decoded images by asset-local index. They contain no device texture IDs. Model instances hold shared immutable CPU assets and GPU residency; importing/reopening the same canonical source reuses those entries.
- Texture and geometry copies for one asset share a direct command context. Upload pages are retired by its fence; texture placement respects 512-byte alignment and each allocation's offset. No per-texture/per-mesh GPU wait occurs on the asynchronous import path.
- A cancelled CPU task never submits. Cancellation after submission retains residency until its fence completes, then discards unpublished geometry. Public snapshots expose drawable GPU residency only in `Ready`.
- Cancellation is cooperative between decode stages and within mesh conversion/rebuild loops. The underlying TinyGLTF file/image read cannot be interrupted mid-call. Shutdown joins the worker and waits for pending uploads before releasing the cache.
- Loader errors, warnings, unsupported primitive/accessor formats, and malformed buffer/index ranges are reported through task snapshots. The existing forward path supports triangle primitives and float vertex attributes; unsupported sparse/quantized accessors fail explicitly.
- Opening a scene or shutting down the editor cancels pending scene imports. Imports that finish normally remain undoable. The cache lives for the application session; individual texture eviction, descriptor recycling, memory budgets, and source-change/reimport detection remain future work.

## Scene document transactions

`SceneDocument` owns serialization and validation independently of ImGui panels. Version 2 stores stable IDs and the next ID; version 1 loads through an ID migration. Duplicate/invalid IDs, unsupported versions, empty asset paths, malformed transform arrays, nonfinite values, and zero rotation quaternions fail before scene replacement. Asset paths inside the project are saved relative to the project root.

Opening a document follows `Queued → Reading → Parsed → LoadingAssets → Ready → Applied`. The worker reads and validates JSON. The render thread requests at most 64 model records per update and waits for every asset's ready residency. It builds the candidate instances before replacing the scene, then updates selection and clears the old document's undo history. Failure, cancellation, and superseded jobs leave the previous scene and history intact. Cancelling a document detaches its shared asset requests; it does not cancel cache work another consumer may still need. The viewport and renderer settings remain usable during loading; scene-editing panels, save, and undo/redo are disabled until it ends.

Save and Save As serialize validated data to an owned sibling temporary file and replace the destination only after the write closes successfully. A failed save preserves the destination. Save still serializes and writes on the main thread; JSON parsing cannot interrupt the underlying stream parser mid-call, and large-document save/commit times remain work for a later stage.

## Reproducible validation

Use PowerShell 7 from the repository root:

```powershell
pwsh -File scripts/Build-Pbr.ps1 -Target All -Configuration Debug -SmokeTest
pwsh -File scripts/Build-Pbr.ps1 -Target All -Configuration Release -SmokeTest
```

The build script discovers MSBuild, fixes the solution root, and normalizes inherited environment variable names. PBR, AtomEditor, Atom core, and ImGui use separate intermediate directories; the dependency project is configured through project-reference properties. The script does not modify dependency source files.

Each runtime check renders 60 frames and exits through normal application cleanup. It renders at 640×360, 641×359, 1024×768, and 8×8; toggles TAA; checks history reset and rounded-up SSAO sizes; and resizes the real main window while checking that view dimensions/history survive. It also creates/resizes a second view, checks resource independence, and checks that zero-size requests preserve the active target and legacy scene buffers are absent. The log records frame count, D3D12 error count, device removal, validation availability, view errors, four resize checks, and one window resize. Debug checks require GPU validation and all these gates. Release checks exercise the same lifecycle and resize gates without claiming Debug-layer coverage. These checks do not prove simultaneous rendering of multiple views, visual correctness, or interactive editor workflow behavior.

The same smoke run also checks six import cases: a valid `.glb` with canonical-path and shared-residency checks, a missing file, an accessor beyond its buffer view, malformed JSON, cancellation before submission, and cancellation after submission. It checks that GPU data is hidden until ready, that failed/pre-submit-cancelled jobs have no upload fence, and that post-submit cancellation waits for fence completion. The imported valid model is added to the scene and drawn. Fixtures are generated only under ignored `bin-int/smoke-assets`. The build script requires `assetErrors=0 assetChecks=6`. These service checks do not replace UI interaction tests or an import benchmark on large assets.

AtomEditor additionally runs 12 checks against an isolated scene using the same asset manager: transform, deletion, and import undo/redo; a failed save preserving an existing file; version 2 roundtrip with IDs/transforms/shared residency; version 1 migration; duplicate IDs; invalid rotation; partial asset failure; cancellation; superseded loads; and an empty document. Pending and failed loads must preserve scene revision, selection, and command history. Successful loads must clear history and set selection. Fixtures live under ignored `bin-int/smoke-documents`. Both application exit status and the script enforce completion (`uiErrors=0 uiChecks=12 uiExpected=12`); PBR has no editor dependency and reports zero expected UI checks. These exercise document/command services, not mouse interaction with dialogs or panels.

Both applications run five frame-scheduler checks. A CPU-signalled D3D12 fence gates the direct queue so that three pending frames and retained owners are deterministic, independent of GPU speed. Checks cover API protocol, deduplicated owner retention, distinct slots and the three-frame limit, safe slot reuse, and drain/release. The regular 60 render frames use asynchronous submissions; they also minimize and restore the actual main window and verify that the scene view/history survive both transitions. Both exit status and the script require `frameErrors=0 frameChecks=5 submissions=60 slotReuses=57 drained=1 presentationChecks=2`. Debug GPU validation covers the queued frame path, resizing/minimization, constants, descriptor tables and cleanup. These checks establish bounded queueing and ownership; they are not a frame-rate benchmark, do not exercise detached panels through mouse input, and do not establish cross-queue rendering safety. Any future async compute path must add explicit queue dependencies.
