# Batch 3: presenter, console session, deformation and vegetation

## Scope and evidence

This batch adds 18 engine stages, taking the checked-in set to **71/184 engine
stages + 1/1 GCN detile kernel** (72 HLSL stages total, 113 engine stages remain).
Local validation uses real Microsoft libdxcompiler, `-WX -HV 2021`, and SM 6.4
profiles, plus an independent Slang front end. Neither compiler establishes GPU
parity, a successful game boot, or playable scene rendering.

| Upstream stage | HLSL stage / profile |
| --- | --- |
| skin.comp | skin.comp.hlsl / cs_6_4 |
| soft_body.comp | soft_body.comp.hlsl / cs_6_4 |
| soft_body_write.comp | soft_body_write.comp.hlsl / cs_6_4 |
| light_grid.comp | light_grid.comp.hlsl / cs_6_4 |
| world_lines.comp | world_lines.comp.hlsl / cs_6_4 |
| branch.vert | branch.vert.hlsl / vs_6_4 |
| branch.frag | branch.frag.hlsl / ps_6_4 |
| branch.comp | branch.comp.hlsl / cs_6_4 |
| grass.vert | grass.vert.hlsl / vs_6_4 |
| grass.frag | grass.frag.hlsl / ps_6_4 |
| grass.comp | grass.comp.hlsl / cs_6_4 |
| leaf.vert | leaf.vert.hlsl / vs_6_4 |
| leaf.frag | leaf.frag.hlsl / ps_6_4 |
| leaf.geom | leaf.geom.hlsl / gs_6_4 |
| leaf.tesc | leaf.hull.hlsl / hs_6_4 |
| leaf.tese | leaf.dom.hlsl / ds_6_4 |
| leaf_shadow.geom | leaf_shadow.geom.hlsl / gs_6_4 |
| leaf_shadow.tese | leaf_shadow.dom.hlsl / ds_6_4 |

`tiling.comp` remains covered by the existing host GCN detile kernel; no second
implementation or extra coverage credit is claimed.

## ABI and dispatch contracts

The root remains **37 DWORDs**: b0 = 20 bytes (5 DWORD arena descriptors), b1 up
to 64 bytes (16 DWORD), b2 up to 16 bytes (4 DWORD), twelve tables in
space100..111 (12 DWORD). No SM 6.6 direct heap indexing is used.
Arena references are `uint2` root fields, converted by `ArenaMake`; embedding an
ArenaRef struct in a cbuffer would introduce 16-byte alignment holes.
`shader_batch_layout.h` checks C++ root sizes and scene offsets; the Python root
checker checks all shader cbuffers against the fixed root limits.

`common/batch3.hlsli` loads the upstream std430 records explicitly:

| Record | Bytes |
| --- | ---: |
| Leaf | 304 |
| Branch | 176 |
| Grass | 496 |
| Blade | 32 |
| SkinJob | 64 |
| BranchJob | 240 |
| SoftBodyJob | 384 |

Bulk matrices remain in arenas, not root constants. The scene upload side must
translate guest/host addresses to arena pairs before these loaders can consume
them; adding shader bytecode does not implement that missing scene upload path.

* Skinning preserves half-weight/bone-index unpacking, the blended row order,
  normal packing, and the tangent's fourth half. Dispatch X spans vertices and
  group Y selects a job; the CPU preserves other destination attributes.
* Soft-body simulation and writeback are separate dispatches with raw arena
  writes; the future frame graph must insert UAV barriers between producers and
  consumers. Effector grids use 6×6×10 cells with 128 entries each.
* Branches and grass preserve the simulation state and procedural geometry;
  grass has 131072 blade slots. The blade count can exceed capacity; reads and
  writes must respect the existing maximum. Counters/grids require initialization
  by the future frame graph, not by arbitrary racing threads in these kernels.
* Point lights use a 48-byte stride. Light-grid dispatch is 60×36×8 groups with
  64 threads per cell, 48 words/cell including a terminating sentinel.
* World lines reuse the raw scratch output contract from Batch 2 (RGBA32 float
  placed footprints), with texture handles in b2. These raw outputs are not
  automatically wired into presentation.
* Leaves use three-control-point isoline patches. Hull/domain must be paired;
  the PSO uses PATCH and the command list binds an N-control-point PATCHLIST.
  Patch arity is IA topology, not a fictitious PSO-stream field. Leaf color stages
  share a 24-byte root; shadow geometry needs its first arena pair only. In both
  domain shaders, domain Y locates a branch knot and domain X grows along a leaf;
  `leaf_along = 1-X` matches upstream. Shadow matrix offset is
  `64 + cascade*3*64`, after inverse_view and with three caster tables/cascade.
  Geometry converts OpenGL depth to reverse color depth or forward shadow depth.

## Actual present and ImGui path

`backend_surface.cpp` now owns a Device, CommandRing, root, descriptor heaps,
backbuffer RTVs and ImGui PSO. Desktop creates an HWND swapchain; UWP uses the
UI thread's CoreWindow and the existing source-size/inverse-composition-scale
matrix setup. A frame transitions PRESENT→RENDER_TARGET, clears, records ImGui,
transitions back, submits/signals its ring fence and calls Present. Resize waits
for the GPU and releases old backbuffers; UWP recreates its single swapchain to
refresh the scale transform. Shutdown joins the desktop worker and retires GPU
resources. Command/fence/present failures record device-removal breadcrumbs.

The relay publishes immutable, owned snapshots under a mutex. Vertices, indices,
commands, clip rectangles, display origin/size/scale and RGBA pixels survive the
producer's next frame or texture destruction. Application callbacks are omitted,
not retained as dangling pointers or invoked on another thread. Atlas create,
update and destroy requests are handled during capture. GPU VB/IB/texture
uploads are retained per fence slot; texture SRV reuse waits a complete ring.
Existing imgui_relay shaders do the draw. Local tests mutate producer buffers,
forget/recreate texture IDs and concurrently acquire snapshots.

**This displays a clear/launcher/ImGui overlay, not the game's scene.**
`backend_capture.cpp` still lacks mesh/texture scene installation and frame-graph
execution. There is no claim of valid scene GPU output or hardware verification.

## Blocking UWP session and boot handoff

RunGame remains in a blocking CoreWindow session. It processes the dispatcher,
updates shim gamepads and presents while the launcher has no dump. It restores a
FutureAccessList token, then a LocalState UTF-8 path, and probes accessible D:, E:
and F: candidates. Select the folder containing the game and update directories
(or their supported archives); GameCheck applies the same v1.11/eboot/archive
checks without a modal failure dialog. A opens the UI-thread FolderPicker; X
reselects; A/Menu launches a validated selection. The picker is asynchronous, so
its UI thread is never blocked on `.get()`. Valid selection persists both token
and path. Blackbox attaches to the validated game root's `gr2_logs` mirror.

LocalState userdata paths and normal EmulatorSettings/UserSettings are initialized.
Boot installs prompt packs, marks EmulatorState and calls the existing
Core::Emulator::Run in process, which owns mounts, HLE/linker, eboot loading and
the normal event loop. The UWP WaitEvent path continues pumping/presenting using a linked, tested
SDL_WaitEventTimeout shim (1 ms session wait, FIFO queue and producer wakeups).
Memory::Instance still adopts the early 5 GiB reservation at 0x100000000 rather
than making a second arena. No game dump was available for executing this bridge;
no Xbox hardware was used. Actual game progress is still blocked by unimplemented
scene capture/rendering and potentially other runtime work.

## Checks

Before push: full local_precheck (including narrow WinRT picker/access-cache/
dispatcher contract stubs and real SDL/ImGui headers), 72/72 Microsoft DXC,
72/72 Slang, manifest/coverage/root checks, host-tool regression tests, chord 25,
memory 16, display 64, the immutable snapshot ownership/concurrency test, and a linked timed-event-queue
test (zero/finite/infinite waits, null peek, FIFO and producer wakeups).
The Windows/UWP Actions jobs remain the authority for SDK compilation, linkage,
DLL/symbol imports and signed MSIX packaging. CI evidence is reported separately
from unperformed console boot/rendering tests.
