# D3D12MemoryAllocator (D3D12MA)

Vendored third-party source, unmodified except where noted in `UPSTREAM.txt`.

- Upstream: https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
- Version: v3.0.1
- License: MIT (see `LICENSE.txt`); bundled VulkanMemoryAllocator notices are in `NOTICES.txt`
- Files: `D3D12MemAlloc.h`, `D3D12MemAlloc.cpp`, `Common.h`, `Common.cpp`

It is vendored as a source pair rather than a git submodule so that the Xbox
UWP build has no network dependency at configure time and so that the exact
allocator source is pinned in this repository.

D3D12MA is not the renderer's policy layer. GR2toXBOX wraps it in
`graphics/gr2port_d3d12/allocator.h`, which enforces the console memory budget
(1,200 MiB D3D12 heap ceiling) and LRU eviction at 80% / 90% of the platform
usage limit.
