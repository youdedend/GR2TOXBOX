<!-- SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors -->
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# A D3D12 header stub, for compile-checking the backend off Windows

There is no Windows SDK in the development container, and no MSVC or clang-cl,
so the Direct3D 12 backend used to be compiled only by CI. That is why a wrong
method name or a field that does not exist cost a 25-minute Windows runner per
typo.

These headers declare just enough of `d3d12.h`, `dxgi1_6.h` and
`<wrl/client.h>` for `g++ -fsyntax-only` to check the backend's own translation
units. They are **not** an implementation: every method is pure virtual and
nothing here links. The point is the type-checker.

What this catches:

* a member that does not exist on a D3D12 struct
* a misspelled or wrongly-typed D3D12 call argument
* a missing `#include` in a new backend file
* a type that does not exist in this project (`i32` instead of `s32`)

What it cannot catch: anything about the real SDK's enum values or the semantics
of a call. CI is still the oracle for those; this only removes the cheap
mistakes from the expensive loop.

Run it through `tools/ci/local_precheck.sh`, which does so automatically when the
backend's files change. Vendored D3D12MA's own errors are filtered out: the stub
does not declare the whole of `d3d12.h`, and that header is not ours to change.
