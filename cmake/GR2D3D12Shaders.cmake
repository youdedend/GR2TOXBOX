# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Offline HLSL Shader Model 6.6 compilation for the Direct3D 12 backend.
#
# Every shader in src/graphics/gr2port_d3d12/shaders/ is compiled at build time
# by Microsoft's DirectXShaderCompiler (dxc.exe) to DXIL, and the DXIL container
# is turned into an embedded C++ header, exactly like the Vulkan backend's
# shader_header.cmake turns SPIR-V into a header.
#
# The build FAILS on any shader diagnostic, including warnings: a single broken
# binding or a misspelt semantic would otherwise only show up as a black screen
# on a console, which is the most expensive kind of bug to chase. `-WX` promotes
# warnings to errors for the same reason.
#
# dxc is located in this order:
#   1. -DGR2_DXC_EXECUTABLE=<path> passed to CMake;
#   2. dxc.exe next to the Visual Studio / Windows SDK tools (the CI runners'
#      layout);
#   3. dxc on PATH;
#   4. the versioned Windows SDK bin directory.
#
# The version of dxc matters: SM 6.6 needs a 2021-or-later compiler, and
# `-HV 2021` is required for the HLSL language version these shaders use.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Locate dxc
# ---------------------------------------------------------------------------

function(gr2_find_dxc out_var)
    if (GR2_DXC_EXECUTABLE AND EXISTS "${GR2_DXC_EXECUTABLE}")
        set(${out_var} "${GR2_DXC_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    find_program(GR2_DXC_PROGRAM NAMES dxc dxc.exe DOC "DirectXShaderCompiler executable")
    if (GR2_DXC_PROGRAM)
        set(${out_var} "${GR2_DXC_PROGRAM}" PARENT_SCOPE)
        return()
    endif()

    # Windows SDK layout: C:/Program Files (x86)/Windows Kits/10/bin/<ver>/x64/dxc.exe
    file(GLOB sdk_dxc
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/dxc.exe"
        "C:/Program Files/Windows Kits/10/bin/*/x64/dxc.exe"
    )
    if (sdk_dxc)
        list(SORT sdk_dxc COMPARE NATURAL ORDER DESCENDING)
        list(GET sdk_dxc 0 newest)
        set(${out_var} "${newest}" PARENT_SCOPE)
        return()
    endif()

    # Visual Studio ships dxc under its DXC directory as well.
    set(vs_globs
        "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/dxc/*/x64/dxc.exe"
        "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Tools/dxc/*/x64/dxc.exe"
    )
    file(GLOB vs_dxc ${vs_globs})
    if (vs_dxc)
        list(SORT vs_dxc COMPARE NATURAL ORDER DESCENDING)
        list(GET vs_dxc 0 newest)
        set(${out_var} "${newest}" PARENT_SCOPE)
        return()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Target stage inference
# ---------------------------------------------------------------------------

# Maps an HLSL file's extension to its dxc profile suffix.
function(gr2_hlsl_stage filename out_stage out_suffix)
    # Sources are named after the GLSL stage they replace and carry an .hlsl
    # suffix: world.vert.hlsl, ui_text.geom.hlsl, leaf.tesc.hlsl. The trailing
    # .hlsl must be stripped before the stage can be read, or every file looks
    # like an unknown ".hlsl" stage.
    string(TOLOWER "${filename}" lower_name)
    if (lower_name MATCHES "\\.hlsl$")
        # world.vert.hlsl -> world.vert -> ".vert"
        string(REGEX REPLACE "\\.hlsl$" "" staged "${filename}")
    else()
        set(staged "${filename}")
    endif()
    get_filename_component(ext "${staged}" EXT)
    string(TOLOWER "${ext}" ext)
    if (ext STREQUAL ".vert")
        set(stage "vs")
    elseif (ext STREQUAL ".frag" OR ext STREQUAL ".pix")
        set(stage "ps")
    elseif (ext STREQUAL ".comp")
        set(stage "cs")
    elseif (ext STREQUAL ".geom")
        set(stage "gs")
    elseif (ext STREQUAL ".hull" OR ext STREQUAL ".tesc")
        set(stage "hs")
    elseif (ext STREQUAL ".dom" OR ext STREQUAL ".tese")
        set(stage "ds")
    elseif (ext STREQUAL ".ms")
        set(stage "ms")
    elseif (ext STREQUAL ".as")
        set(stage "as")
    elseif (ext STREQUAL ".lib")
        set(stage "lib")
    else()
        message(FATAL_ERROR
            "gr2port_add_hlsl_shader: '${filename}' has no stage. "
            "Name the file after the GLSL stage it replaces, with an .hlsl "
            "suffix: world.vert.hlsl, ui_text.geom.hlsl, leaf.tesc.hlsl. "
            "The stage maps to HLSL vs/ps/cs/gs/hs/ds.")
    endif()
    set(${out_stage} "${stage}" PARENT_SCOPE)
    # DXR / mesh shaders are 6.3+; everything else is 6.6.
    set(${out_suffix} "${stage}_6_6" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

# gr2port_add_hlsl_shader(<source> <symbol> [ENTRY <name>] [PROFILE <p>])
#
#   <source>  path relative to GR2_D3D12_SHADER_DIR
#   <symbol>  C identifier for the embedded DXIL array
#
# Appends the generated header to GR2_D3D12_SHADER_HEADERS in the PARENT_SCOPE,
# so a target can depend on all of them at once.
function(gr2port_add_hlsl_shader FILENAME SYMBOL)
    cmake_parse_arguments(ARG "" "ENTRY;PROFILE" "" ${ARGN})

    gr2_hlsl_stage("${FILENAME}" stage suffix)
    if (ARG_PROFILE)
        set(profile "${ARG_PROFILE}")
    else()
        set(profile "${suffix}")
    endif()
    if (ARG_ENTRY)
        set(entry "${ARG_ENTRY}")
    else()
        set(entry "main")
    endif()

    set(src "${GR2_D3D12_SHADER_DIR}/${FILENAME}")
    if (NOT EXISTS "${src}")
        message(FATAL_ERROR "gr2port_add_hlsl_shader: '${src}' does not exist")
    endif()

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/gr2port_d3d12_shaders")
    get_filename_component(stem "${FILENAME}" NAME_WE)
    get_filename_component(subdir "${FILENAME}" DIRECTORY)
    if (subdir)
        string(REPLACE "/" "_" subdir "${subdir}")
        set(basename "${subdir}_${stem}")
    else()
        set(basename "${stem}")
    endif()
    set(dxil "${out_dir}/${basename}_${stage}.dxil")
    set(header "${out_dir}/${SYMBOL}.h")

    # Include roots: the shader tree for the shared common/*.hlsli headers that
    # the arena, scene and vertex accessors live in. -WX above makes a shader
    # warning a build failure: a broken binding is cheaper to fix here than on a
    # console.
    set(include_dirs
        "-I${GR2_D3D12_SHADER_DIR}"
        "-I${GR2_D3D12_SHADER_DIR}/common"
    )

    add_custom_command(
        OUTPUT "${dxil}" "${header}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND "${GR2_DXC_EXECUTABLE}"
                -T "${profile}"
                -E "${entry}"
                -HV 2021
                -O3
                -WX
                -enable-16bit-types
                -Qstrip_reflect
                -Qstrip_debug
                -Fo "${dxil}"
                ${include_dirs}
                "${src}"
        COMMAND ${CMAKE_COMMAND}
                "-DDXIL_FILE=${dxil}"
                "-DHEADER_FILE=${header}"
                "-DSYMBOL=${SYMBOL}"
                "-DSTAGE=${stage}"
                -P "${GR2_D3D12_HEADER_SCRIPT}"
        DEPENDS "${src}" "${GR2_D3D12_HEADER_SCRIPT}" ${GR2_D3D12_SHADER_INCLUDES}
        COMMENT "Compiling ${FILENAME} -> ${profile} DXIL"
        VERBATIM)

    set(GR2_D3D12_SHADER_HEADERS
        ${GR2_D3D12_SHADER_HEADERS} "${header}"
        PARENT_SCOPE)
endfunction()

# Configures the shader toolchain for a target. Call once, before
# gr2port_add_hlsl_shader().
function(gr2port_d3d12_configure_shaders target)
    gr2_find_dxc(GR2_DXC_EXECUTABLE)
    if (NOT GR2_DXC_EXECUTABLE)
        message(FATAL_ERROR
            "GR2_RENDERER=D3D12 needs the DirectXShaderCompiler (dxc).\n"
            "  * On Windows: install the Windows SDK 10.0.22621+ or run "
            "scripts/ci/fetch_dxc.ps1.\n"
            "  * Or pass -DGR2_DXC_EXECUTABLE=C:/path/to/dxc.exe.\n"
            "Every HLSL shader is compiled at build time; the build cannot "
            "proceed without it.")
    endif()
    message(STATUS "GR2toXBOX: using dxc at ${GR2_DXC_EXECUTABLE}")
    # Propagate the resolved path to the caller's scope: gr2port_add_hlsl_shader
    # reads it when it builds the custom command, and if dxc was found by the
    # search below (not passed with -D) a function-scope-only value would leave
    # that command with an empty program name.
    set(GR2_DXC_EXECUTABLE "${GR2_DXC_EXECUTABLE}" PARENT_SCOPE)

    set(GR2_D3D12_SHADER_DIR "${PROJECT_SOURCE_DIR}/src/graphics/gr2port_d3d12/shaders"
        PARENT_SCOPE)
    set(GR2_D3D12_HEADER_SCRIPT "${PROJECT_SOURCE_DIR}/cmake/GR2D3D12ShaderHeader.cmake"
        PARENT_SCOPE)
    # The shared headers every shader includes; a change to one rebuilds all.
    file(GLOB GR2_D3D12_SHADER_INCLUDES
        "${PROJECT_SOURCE_DIR}/src/graphics/gr2port_d3d12/shaders/common/*.hlsli")
    set(GR2_D3D12_SHADER_INCLUDES "${GR2_D3D12_SHADER_INCLUDES}" PARENT_SCOPE)
    set(GR2_D3D12_SHADER_HEADERS "" PARENT_SCOPE)
    set(target "${target}" PARENT_SCOPE)
endfunction()
