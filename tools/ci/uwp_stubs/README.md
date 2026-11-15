# SDK contract subset (never used in a production build)

`check_uwp_types.py` compiles the real UWP display-query translation unit, the
actual matrix call-site extracted from device.cpp, and the early FromApp arena
reservation. It also requires both original broken expressions to fail. Run with
`clang++` (CI), or native PyPI `libclang` in `/tmp/gr2-clang-venv` when the sandbox
has no driver. An explicitly labelled g++ fallback is available.

`common/logging/log.h` evaluates logging arguments without requiring any logging
submodule for the ABI-only check. The black-box logger is separately compiled by
local_precheck against **real pinned spdlog/fmt headers**, including its custom
`log_msg.log_level` member. `GR2_DEPS_ROOT` can point to header archives outside
this checkout; no local submodule initialization is required.

These are strict declarations, not a complete SDK or an implementation. The
black-box syntax check covers both platform branches, but only Windows CI checks
real SDK declarations, SEH, linking, imports and MSIX packaging. Do not treat a
stub check as proof of UWP API certification or runtime correctness.
