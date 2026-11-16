# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The Direct3D 12 renderer backend (GR2_RENDERER=D3D12).
#
# Sourced from CMakeLists.txt when the D3D12 renderer is selected for a Windows
# or Xbox UWP build. The backend replaces graphics/gr2port's Vulkan device,
# pipeline and shader compilation; the game-side capture modules
# (capture.cpp, particle_capture.cpp, ui_capture.cpp, scene_ring.cpp, ...) are
# shared and stay in gr2port.cmake.

include(${PROJECT_SOURCE_DIR}/cmake/GR2D3D12Shaders.cmake)

set(GR2_D3D12_DIR ${CMAKE_CURRENT_LIST_DIR})

# --- Host sources -----------------------------------------------------------
target_compile_definitions(gr2port PRIVATE GR2_BLACKBOX=1 GR2_RENDERER_D3D12=1)
target_sources(gr2port PRIVATE
    ${PROJECT_SOURCE_DIR}/src/uwp/blackbox_log.cpp
    ${PROJECT_SOURCE_DIR}/src/uwp/blackbox_log.h
    ${PROJECT_SOURCE_DIR}/src/core/uwp_guest_arena.cpp
    ${GR2_D3D12_DIR}/detile.cpp
    ${GR2_D3D12_DIR}/detile_layout.cpp
    ${GR2_D3D12_DIR}/meshes_d3d12.cpp
    ${GR2_D3D12_DIR}/textures_d3d12.cpp
    ${GR2_D3D12_DIR}/ui_d3d12.cpp
    ${GR2_D3D12_DIR}/ui_d3d12.h
    ${GR2_D3D12_DIR}/world_d3d12.cpp
    ${GR2_D3D12_DIR}/world_d3d12.h
    ${GR2_D3D12_DIR}/look_d3d12.cpp
    ${GR2_D3D12_DIR}/look_d3d12.h
    ${GR2_D3D12_DIR}/core_state.h
    ${GR2_D3D12_DIR}/ui_state.cpp
    ${GR2_D3D12_DIR}/ui_state.h
    ${GR2_D3D12_DIR}/texture_layout.cpp
    ${GR2_D3D12_DIR}/allocator.cpp
    ${GR2_D3D12_DIR}/allocator.h
    ${GR2_D3D12_DIR}/arena.cpp
    ${GR2_D3D12_DIR}/arena.h
    ${GR2_D3D12_DIR}/descriptors.cpp
    ${GR2_D3D12_DIR}/descriptors.h
    ${GR2_D3D12_DIR}/device.cpp
    ${GR2_D3D12_DIR}/device.h
    ${GR2_D3D12_DIR}/command.cpp
    ${GR2_D3D12_DIR}/command.h
    ${GR2_D3D12_DIR}/pipeline.cpp
    ${GR2_D3D12_DIR}/pipeline.h
    # The frontend-facing half of the backend: the renderer's lifetime, the
    # lost-device log, the game's photos, and ImGui's frame capture. These are the
    # names the shared capture modules and ImGui's own code call into, and the Vulkan
    # backend defines them in files that name vk:: types -- so a D3D12 build needs
    # its own, or it compiles every module and then fails at link on exactly these
    # symbols. See graphics/gr2port/backend.h for the contract.
    ${GR2_D3D12_DIR}/backend_surface.cpp
    ${GR2_D3D12_DIR}/backend_capture.cpp
    ${GR2_D3D12_DIR}/imgui_relay.cpp
    ${GR2_D3D12_DIR}/imgui_gpu.cpp
    ${GR2_D3D12_DIR}/uwp/display_mode.h
    ${GR2_D3D12_DIR}/uwp/uwp_app.cpp
    ${GR2_D3D12_DIR}/uwp/uwp_launcher.cpp
    ${GR2_D3D12_DIR}/uwp/uwp_main.cpp
    ${GR2_D3D12_DIR}/uwp/uwp_main.h
)

# --- Vendored third-party ---------------------------------------------------
# D3D12MemoryAllocator, unmodified (see d3d12ma/UPSTREAM.txt). Built as its own
# object library so its warnings do not have to meet the renderer's /W4 bar.
add_library(gr2_d3d12ma OBJECT
    ${GR2_D3D12_DIR}/d3d12ma/D3D12MemAlloc.cpp
    ${GR2_D3D12_DIR}/d3d12ma/Common.cpp
)
target_include_directories(gr2_d3d12ma PUBLIC
    ${GR2_D3D12_DIR}/d3d12ma
    ${GR2_D3D12_DIR}/d3d12ma/..
)
target_compile_definitions(gr2_d3d12ma PRIVATE
    D3D12MA_STATIC_LIBRARY
    $<$<CONFIG:Debug>:D3D12MA_DEBUG_LEVEL=1>
)
if (MSVC)
    # D3D12MA is portable, warning-clean upstream; do not hold it to /W4 /WX.
    target_compile_options(gr2_d3d12ma PRIVATE /W3)
endif()
target_link_libraries(gr2port PRIVATE gr2_d3d12ma)

# --- Windows / DXGI libraries ----------------------------------------------
if (NOT GR2_XBOX_UWP)
    target_link_libraries(gr2port PRIVATE d3d12 dxgi dxguid d3dcompiler)
endif()

# The Xbox package links the onecore UWP surface (WindowsApp.lib), not the
# desktop Win32 import libraries: CreateFile2FromApp, VirtualAllocFromApp and
# friends only resolve from there.
if (GR2_XBOX_UWP)
    # Store's directory supplies /MD import libraries, not the /MT CRT. Use the
    # OneCore static runtime explicitly for the final title link; applying Store
    # vcvars globally breaks configure-time executable probes that use /MT.
    file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" _gr2_vctools)
    find_path(GR2_ONECORE_CRT_DIR NAMES libcmt.lib
        PATHS "${_gr2_vctools}/lib/onecore/x64"
              "${_gr2_vctools}/lib/x64/onecore"
        NO_DEFAULT_PATH REQUIRED)
    foreach(_runtime_lib libcpmt.lib libvcruntime.lib)
        if (NOT EXISTS "${GR2_ONECORE_CRT_DIR}/${_runtime_lib}")
            message(FATAL_ERROR "Missing OneCore static runtime: ${_runtime_lib}")
        endif()
    endforeach()
    target_link_directories(gr2port BEFORE PRIVATE "${GR2_ONECORE_CRT_DIR}")
    message(STATUS "UWP static CRT: ${GR2_ONECORE_CRT_DIR}")
    target_link_libraries(gr2port PRIVATE WindowsApp)
    target_compile_definitions(gr2port PRIVATE
        GR2_XBOX_UWP=1
        WINAPI_FAMILY=WINAPI_FAMILY_APP
    )
endif()

# --- HLSL shaders -----------------------------------------------------------
gr2port_d3d12_configure_shaders(gr2port)

# The shader list is generated from the shader directory by
# tools/shaders/generate_d3d12_manifest.py, so a shader dropped into the tree
# cannot be silently left out of the build (which would make it look ported in
# the coverage report and fail only on a console).
if (NOT EXISTS ${GR2_D3D12_DIR}/shaders_manifest.cmake)
    message(FATAL_ERROR
        "src/graphics/gr2port_d3d12/shaders_manifest.cmake is missing.\n"
        "  Regenerate it: python3 tools/shaders/generate_d3d12_manifest.py")
endif()
include(${GR2_D3D12_DIR}/shaders_manifest.cmake)

add_custom_target(gr2port_d3d12_shaders DEPENDS ${GR2_D3D12_SHADER_HEADERS})
add_dependencies(gr2port gr2port_d3d12_shaders)
target_include_directories(gr2port PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gr2port_d3d12_shaders)

# Symbols are external to the MSIX, but shipped beside it for offline reports.
if (MSVC AND GR2_XBOX_UWP)
    target_compile_options(gr2port PRIVATE /Zi)
    target_link_options(gr2port PRIVATE /DEBUG:FULL
        "/PDB:$<TARGET_FILE_DIR:gr2port>/GR2port.pdb"
        "/MAP:$<TARGET_FILE_DIR:gr2port>/GR2port.map")
endif()

list(LENGTH GR2_D3D12_SHADER_HEADERS GR2_COMPILED_SHADER_COUNT)
target_compile_definitions(gr2port PRIVATE GR2_COMPILED_SHADER_COUNT=${GR2_COMPILED_SHADER_COUNT})
