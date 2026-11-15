# SPDX-FileCopyrightText: Copyright 2026 GR2port
# SPDX-License-Identifier: GPL-2.0-or-later

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set_directory_properties(PROPERTIES
    EXCLUDE_FROM_ALL ON
    SYSTEM ON
)

# Set CMP0069 policy to "NEW" in order to ensure consistent behavior when building external targets with LTO enabled
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

if (MSVC)
    # Silence "deprecation" warnings
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
endif()

# Boost
if (NOT TARGET Boost::headers)
    add_subdirectory(ext-boost)
endif()

# spdlog
set(SPDLOG_NO_EXCEPTIONS ON)
set(SPDLOG_DISABLE_GLOBAL_LOGGER ON)
if (TARGET fmt::fmt)
    set(SPDLOG_FMT_EXTERNAL ON)
endif()
add_subdirectory(spdlog EXCLUDE_FROM_ALL SYSTEM)
target_compile_definitions(spdlog PUBLIC SPDLOG_FUNCTION=__func__)

# fmtlib
if (NOT TARGET fmt::fmt)
    add_subdirectory(fmt)
endif()

# FFmpeg
if (NOT TARGET FFmpeg::ffmpeg)
    if (GR2_XBOX_UWP)
        # The desktop prebuilt contains DXVA2/GetDesktopWindow and COM capture.
        # CI builds the same FFmpeg version with software codecs for APP family.
        file(TO_CMAKE_PATH "$ENV{GR2_FFMPEG_UWP_ROOT}" _gr2_ffmpeg_root)
        if (NOT EXISTS "${_gr2_ffmpeg_root}/include/libavcodec/avcodec.h")
            message(FATAL_ERROR "Set GR2_FFMPEG_UWP_ROOT to scripts/ci/build_ffmpeg_uwp.sh output")
        endif()
        add_library(ffmpeg INTERFACE)
        target_include_directories(ffmpeg INTERFACE "${_gr2_ffmpeg_root}/include")
        foreach(_fflib avformat avcodec swscale avutil avfilter swresample)
            set(_archive "${_gr2_ffmpeg_root}/lib/lib${_fflib}.a")
            if (NOT EXISTS "${_archive}")
                message(FATAL_ERROR "Missing UWP software codec archive: ${_archive}")
            endif()
            target_link_libraries(ffmpeg INTERFACE "${_archive}")
        endforeach()
        target_link_libraries(ffmpeg INTERFACE WindowsApp)
    else()
        add_subdirectory(ffmpeg-core)
    endif()
    add_library(FFmpeg::ffmpeg ALIAS ffmpeg)
endif()

# LibAtrac9
file(GLOB LIBATRAC9_SOURCES LibAtrac9/C/src/*.c)
add_library(LibAtrac9 STATIC ${LIBATRAC9_SOURCES})
target_include_directories(LibAtrac9 INTERFACE LibAtrac9/C/src)

# zlib
if (NOT TARGET ZLIB::ZLIB)
    set(ZLIB_ENABLE_TESTS OFF)
    set(WITH_GTEST OFF)
    set(WITH_NEW_STRATEGIES ON)
    set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
        ZLIB
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib-ng"
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlib)
    # libpng expects this variable to exist after its find_package(ZLIB)
    set(ZLIB_INCLUDE_DIRS "${FETCHCONTENT_BASE_DIR}/zlib-build")
endif()

# FreeType
if (NOT TARGET Freetype::Freetype)
    # Keep optional deps off to reduce build surface; zlib stays enabled for compressed fonts.
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    add_subdirectory(freetype)
    add_library(Freetype::Freetype ALIAS freetype)

    # FreeType picks its stream back end by platform, and the Windows one
    # (builds/windows/ftsystem.c) calls CreateFileA directly. A packaged UWP
    # process may not: that API is not in the app surface, and the CRT's stdio is
    # what maps onto the allowed path. builds/.../ftsystem.c and the generic
    # src/base/ftsystem.c both do all their file work through stdio, so the
    # generic one is swapped in for the UWP build and nothing else changes.
    #
    # Done by rewriting the target's sources rather than by patching the
    # dependency: a patched submodule cannot be updated without re-applying the
    # patch, and this project has already been through that with vendor code.
    #
    # The removed entry is spelled relative to the freetype subdirectory because
    # that is how freetype's own CMakeLists appended it -- a remove with an
    # absolute path silently matches nothing, and the build then fails exactly as
    # before while the configure log claims the swap happened. The replacement is
    # absolute so it cannot be resolved against the wrong directory.
    if (GR2_XBOX_UWP)
        get_target_property(_ft_sources freetype SOURCES)
        list(REMOVE_ITEM _ft_sources "builds/windows/ftsystem.c")
        list(APPEND _ft_sources "${CMAKE_CURRENT_SOURCE_DIR}/freetype/src/base/ftsystem.c")
        set_target_properties(freetype PROPERTIES SOURCES "${_ft_sources}")
        message(STATUS "freetype: using the stdio stream back end (the Windows one calls CreateFileA)")
    endif()
endif()

# SDL3
if (NOT TARGET SDL3::SDL3)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_PIPEWIRE OFF)
    set(SDL_AVX512F OFF)
    if (GR2_XBOX_UWP)
        # The Xbox package does not use SDL's library at all: src/uwp/sdl3_shim/
        # provides every SDL call the emulator makes, over CoreWindow,
        # Windows.Gaming.Input and XAudio2. SDL3 is still needed here because its
        # *real headers* are what the shim compiles against -- that is what stops a
        # signature from drifting -- but building its sources would be wasted work
        # that cannot succeed: SDL 3 dropped UWP support and its own internal
        # sources call APIs the onecore surface does not have.
        #
        # So the target exists, carries the headers, and has no sources. The link
        # line still names SDL3::SDL3 because gr2port links it unconditionally on
        # every platform; an interface library keeps that uniform.
        add_library(SDL3-shim-headers INTERFACE)
        target_include_directories(SDL3-shim-headers SYSTEM INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/sdl3/include")
        add_library(SDL3::SDL3 ALIAS SDL3-shim-headers)
    else()
        add_subdirectory(sdl3)
    endif()
endif()

# vulkan-headers
if (NOT TARGET Vulkan::Headers)
    set(VULKAN_HEADERS_ENABLE_MODULE OFF)
    add_subdirectory(vulkan-headers)
    set(VulkanHeaders_VERSION "1.4.330")
endif()

# VMA
if (NOT TARGET GPUOpen::VulkanMemoryAllocator)
    add_subdirectory(vma)
endif()

# RenderDoc
if (NOT TARGET RenderDoc::API)
    add_library(renderdoc INTERFACE)
    target_include_directories(renderdoc INTERFACE ./renderdoc)
    add_library(RenderDoc::API ALIAS renderdoc)
endif()

# glslang
if (NOT TARGET glslang::glslang)
    set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "")
    set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "")
    set(ENABLE_CTEST OFF CACHE BOOL "")
    set(ENABLE_HLSL OFF CACHE BOOL "")
    set(BUILD_EXTERNAL OFF CACHE BOOL "")
    set(ENABLE_OPT OFF CACHE BOOL "")
    add_subdirectory(glslang)
    file(COPY glslang/SPIRV DESTINATION glslang/glslang FILES_MATCHING PATTERN "*.h")
    target_include_directories(glslang INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/glslang")
endif()

# Robin-map
if (NOT TARGET tsl::robin_map)
    add_subdirectory(robin-map)
endif()

# Xbyak
if (NOT TARGET xbyak::xbyak)
    add_subdirectory(xbyak)
endif()

# MagicEnum
if (NOT TARGET magic_enum::magic_enum)
    add_subdirectory(magic_enum)
endif()

# Toml11
if (NOT TARGET toml11::toml11)
    add_subdirectory(toml11)

    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        get_target_property(_toml11_compile_options toml11 INTERFACE_COMPILE_OPTIONS)
        list(REMOVE_ITEM _toml11_compile_options "/Zc:preprocessor")
        set_target_properties(toml11 PROPERTIES INTERFACE_COMPILE_OPTIONS ${_toml11_compile_options})
      endif()
    endif()
endif()

# Zydis
if (NOT TARGET Zydis::Zydis)
    option(ZYDIS_BUILD_TOOLS "" OFF)
    option(ZYDIS_BUILD_EXAMPLES "" OFF)
    add_subdirectory(zydis)
endif()

# sirit: only the SPIR-V headers that come with it are used, so its library is not built.
add_subdirectory(sirit EXCLUDE_FROM_ALL)

# SPIRV-Tools: spirv-opt optimises GR2port's shaders at build time, and only it is built. It
# takes the SPIR-V headers of its own release.
if (NOT TARGET spirv-opt)
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "")
    set(SPIRV_WERROR OFF CACHE BOOL "")
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "")
    set(SPIRV-Headers_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/spirv-headers)
    add_subdirectory(spirv-tools EXCLUDE_FROM_ALL)
    unset(SPIRV-Headers_SOURCE_DIR)
endif()

# half
if (NOT TARGET half::half)
    add_library(half INTERFACE)
    target_include_directories(half INTERFACE half/include)
    add_library(half::half ALIAS half)
endif()

# libpng
if (NOT TARGET PNG::PNG)
    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC ON CACHE BOOL "" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(libpng)
    add_library(PNG::PNG ALIAS png_static)
endif()

# Dear ImGui
add_library(Dear_ImGui
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_internal.h
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
)
target_include_directories(Dear_ImGui INTERFACE imgui/)

# Tracy
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    option(TRACY_ENABLE "" OFF)
else()
    option(TRACY_ENABLE "" ON)
endif()
option(TRACY_NO_CRASH_HANDLER "" ON) # Otherwise texture cache exceptions will be treaten as a crash
option(TRACY_ON_DEMAND "" ON)
option(TRACY_NO_FRAME_IMAGE "" ON)
option(TRACY_FIBERS "" OFF) # For AmdGpu frontend profiling, disabled due to instability
option(TRACY_NO_SYSTEM_TRACING "" ON)
option(TRACY_NO_CALLSTACK "" ON)
option(TRACY_NO_CODE_TRANSFER "" ON)
option(TRACY_NO_SAMPLING "" ON)
option(TRACY_ONLY_LOCALHOST "" ON)
option(TRACY_NO_CONTEXT_SWITCH "" ON)
add_subdirectory(tracy)

# pugixml
if (NOT TARGET pugixml::pugixml)
    option(PUGIXML_NO_EXCEPTIONS "" ON)
    add_subdirectory(pugixml)
endif()

# libusb
#
# The Windows port is WinUSB plus SetupAPI/CfgMgr32 -- device enumeration is
# SetupDiGetClassDevs, descriptor reads go through CfgMgr32, and transfers open a
# WinUSB handle. None of that is in the onecore surface an AppContainer links
# against, and libusb has no UWP branch (nor a null backend wired up on Windows,
# which is what it uses where there is no OS USB API at all). So the console build
# does not build it: usbd_uwp.cpp answers libSceUsbd instead. The GIT_SUBMODULE
# checkout still happens, it is just not compiled.
if (GR2_XBOX_UWP)
    message(STATUS "libusb: not built (WinUSB/SetupAPI are not in the UWP surface)")
elseif (NOT TARGET libusb::usb)
    if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
        # YOU MUST USE NATIVE LIBUSB
        # using anything else will crash instantly, also freebsd will NOT like it
        # no you cant vendor this libusb, its builtin on freebsd
        find_package(libusb)
    else()
        add_subdirectory(libusb)
        add_library(libusb::usb ALIAS usb-1.0)
    endif()
endif()

# stb
if (NOT TARGET stb::headers)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE stb)
    add_library(stb::headers ALIAS stb)
endif()

# hwinfo
#
# hwinfo reads the machine: its Windows backend drives WMI to enumerate the CPU,
# the battery and the GPU, and WMI is not in the onecore surface a UWP app links
# against. It failed on the first UWP run that got this far ("unknown type name
# 'IWbemServices'" from wmi_wrapper.h and "'WBEM_INFINITE' undeclared" from
# battery.cpp). The console's hardware is known anyway -- it is an Xbox -- so the
# UWP package takes the headers, for the hwinfo:: type names GR2port.cpp mentions,
# and no implementation. The caller's use of it is guarded below.
if (GR2_XBOX_UWP)
    add_library(lfreist-hwinfo-hwinfo-headers INTERFACE)
    target_include_directories(lfreist-hwinfo-hwinfo-headers INTERFACE hwinfo/include)
    add_library(lfreist-hwinfo::hwinfo ALIAS lfreist-hwinfo-hwinfo-headers)
    message(STATUS "hwinfo: headers only (WMI is not in the UWP surface)")
else()
    set(HWINFO_STATIC ON)
    add_subdirectory(hwinfo)
endif()

if (APPLE OR ${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    # date
    if (NOT TARGET date::date-tz)
        option(BUILD_TZ_LIB "" ON)
        option(USE_SYSTEM_TZ_DB "" ON)
        add_subdirectory(date)
    endif()
    if (NOT TARGET epoll-shim)
        add_subdirectory(epoll-shim)
    endif()
endif()

#windows only
#
# wepoll's epoll_create1 calls SetHandleInformation for EPOLL_CLOEXEC, and that
# function is not in the onecore surface a UWP app links against. The fix is two
# lines in wepoll.c, but wepoll is a submodule pointing at upstream's dist branch,
# so it cannot carry a local commit -- a dirty submodule does not travel with this
# repo and the Actions runner checks out the pristine tree. The patch is therefore
# applied here, at configure time, and only on UWP: the desktop build keeps
# upstream's file byte for byte, which matters because wepoll is a dependency we
# do not own and a fix that silently changed it everywhere would be a fork.
#
# It is idempotent -- the patch's own context fails once applied -- so a rebuild
# or a second configure is harmless.
if (GR2_XBOX_UWP AND WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext-wepoll/wepoll.c")
    execute_process(
        COMMAND git apply --reverse --check
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/ci/patches/wepoll-uwp.patch"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ext-wepoll"
        RESULT_VARIABLE wepoll_already_patched
        OUTPUT_QUIET ERROR_QUIET)
    if (NOT wepoll_already_patched EQUAL 0)
        execute_process(
            COMMAND git apply "${CMAKE_CURRENT_SOURCE_DIR}/../tools/ci/patches/wepoll-uwp.patch"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ext-wepoll"
            RESULT_VARIABLE wepoll_patch_result
            OUTPUT_VARIABLE wepoll_patch_out
            ERROR_VARIABLE wepoll_patch_err)
        if (NOT wepoll_patch_result EQUAL 0)
            message(FATAL_ERROR
                "failed to apply wepoll's UWP patch:\n${wepoll_patch_err}")
        endif()
        message(STATUS "wepoll: applied the UWP SetHandleInformation patch")
    endif()
endif()

if (WIN32)
add_subdirectory(ext-wepoll)
endif()

add_subdirectory(aacdec)

#nlohmann json 
if (NOT TARGET nlohmann_json::nlohmann_json)
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(json)
endif()

# miniz
if (NOT TARGET miniz::miniz)
add_subdirectory(miniz)
add_library(miniz::miniz ALIAS miniz)
endif()

# cli11
if (NOT TARGET CLI11::CLI11)
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(CLI11)
endif()

# miniupnpc
if (NOT TARGET miniupnpc::miniupnpc)
set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(miniupnp/miniupnpc)
endif()

# minimp3
add_library(minimp3 INTERFACE)
target_include_directories(minimp3 INTERFACE minimp3)

# dr_flac
add_library(dr_flac INTERFACE)
target_include_directories(dr_flac INTERFACE dr_flac)

# ImGuiFileDialog
#
# The folder picker in settings_dialog_imgui.cpp. Its Windows path enumerates
# drives with GetLogicalDriveStringsA, which the onecore surface does not have, and
# a console has no drives to pick from anyway: the packaged app's storage is its
# own local folder. Skipped on UWP, and the caller falls back to a built-in picker
# (see the GR2_XBOX_UWP block in settings_dialog_imgui.cpp).
if (NOT GR2_XBOX_UWP)
    add_library(ImGuiFileDialog ImGuiFileDialog/ImGuiFileDialog.cpp)
    target_compile_definitions(ImGuiFileDialog PRIVATE USE_STD_FILESYSTEM)
    target_include_directories(ImGuiFileDialog INTERFACE ImGuiFileDialog/)
    target_link_libraries(ImGuiFileDialog PRIVATE Dear_ImGui)
endif()

#openal
if (NOT TARGET OpenAL::OpenAL)
set(ALSOFT_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
set(LIBTYPE "STATIC" CACHE STRING "" FORCE)
# Disable everything we don't need
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_CONFIG OFF CACHE BOOL "" FORCE)

# Backends (platform-specific)
#
# UWP is the case to get right rather than skip: OpenAL Soft is a real output
# path the guest can select (openal_audio_out.cpp, and audio3d's positional
# output), and its source already knows about UWP -- it sets ALSOFT_UWP itself
# when CMAKE_SYSTEM_NAME is WindowsStore, and skips the avrt/DSound/WinMM backends
# there because they are outside the onecore surface. What it cannot know is that
# *this* build forced WASAPI on from outside, which is what the line below does on
# every Windows target. So the override becomes desktop-only and UWP falls through
# to OpenAL Soft's own choice.
#
# On UWP that leaves the null backend (ALSOFT_BACKEND_NULL, set below for every
# platform), so a guest that selects OpenAL gets silence through a working mixer
# instead of a failed device -- which is what its own fallback expects, and what
# the SDL path through the shim's XAudio2 output does not have to share.
if (WIN32 AND NOT GR2_XBOX_UWP)
    set(ALSOFT_BACKEND_WASAPI ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_DSOUND OFF CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_WINMM OFF CACHE BOOL "" FORCE)
elseif (APPLE)
    set(ALSOFT_BACKEND_COREAUDIO ON CACHE BOOL "" FORCE)
else()
    set(ALSOFT_BACKEND_ALSA ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_PULSEAUDIO ON CACHE BOOL "" FORCE)
    set(ALSOFT_BACKEND_PIPEWIRE OFF CACHE BOOL "" FORCE)
endif()
# Headless-safe
set(ALSOFT_BACKEND_NULL ON CACHE BOOL "" FORCE)

# Static build
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(openal-soft EXCLUDE_FROM_ALL)
endif()

# LibreSSL
if (TARGET LibreSSL::SSL OR TARGET LibreSSL::Crypto)
    set(SSL_DEPENDENCIES LibreSSL::SSL LibreSSL::Crypto)
elseif (TARGET OpenSSL::SSL OR TARGET OpenSSL::Crypto)
    set(SSL_DEPENDENCIES OpenSSL::SSL OpenSSL::Crypto)
else()
    set(LIBRESSL_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(LIBRESSL_APPS OFF CACHE BOOL "" FORCE)
    set(LIBRESSL_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(libressl EXCLUDE_FROM_ALL)
    get_directory_property(SSL_DEPENDENCIES DIRECTORY libressl DEFINITION OPENSSL_LIBS)
endif()

# cpp-httplib
add_library(Cpp_Httplib INTERFACE)
target_include_directories(Cpp_Httplib INTERFACE cpp-httplib/)

if (TARGET ZLIB::ZLIB)
    target_link_libraries(Cpp_Httplib INTERFACE ZLIB::ZLIB)
    target_compile_definitions(Cpp_Httplib INTERFACE CPPHTTPLIB_ZLIB_SUPPORT)
    message(STATUS "cpp-httplib: zlib support enabled (gzip response decompression)")
else()
    message(STATUS "cpp-httplib: zlib not available — gzip responses will arrive uncompressed")
endif()

if (DEFINED SSL_DEPENDENCIES)
    target_link_libraries(Cpp_Httplib INTERFACE ${SSL_DEPENDENCIES})
    target_compile_definitions(Cpp_Httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
    message(STATUS "cpp-httplib: SSL support enabled (TLS connections)")
else()
    message(STATUS "cpp-httplib: OpenSSL/LibreSSL not available — libSceHttp HTTPS will fall back to mock at runtime.")
endif()

# macOS Vulkan driver
if (APPLE AND NOT ENABLE_SYSTEM_VULKAN AND NOT ENABLE_TESTS)
    add_subdirectory(vulkan-loader)
    set(VULKAN_LOADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/vulkan-loader/loader/libvulkan.dylib" PARENT_SCOPE)

    add_subdirectory(mesa-kosmickrisp)
    set(VULKAN_DRIVER_PATH "${CMAKE_CURRENT_BINARY_DIR}/mesa-kosmickrisp/outputs/libvulkan_kosmickrisp.dylib" PARENT_SCOPE)
    set(VULKAN_ICD_PATH "${CMAKE_CURRENT_BINARY_DIR}/mesa-kosmickrisp/outputs/kosmickrisp_mesa_icd.json" PARENT_SCOPE)
endif()

# abseil (for protobuf)
# Like protobuf, Abseil overrides the inherited runtime selection. Set its
# independent switch before creating targets, not when protobuf is added later.
if (MSVC AND GR2_XBOX_UWP)
    set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
endif()
if (NOT TARGET absl::strings)
    add_subdirectory(abseil-cpp EXCLUDE_FROM_ALL)
endif()

# protobuf
#
# libprotoc is the console compiler and it spawns a child process to run the
# compiler plugins a .proto names. A packaged UWP process cannot create a process
# at all -- CreateProcess and the STARTUPINFO fields it needs are desktop-only --
# so libprotoc fails on src/google/protobuf/compiler/subprocess.cc before
# anything else in the build runs.
#
# The UWP configuration therefore builds the runtime and no compiler. That is
# only sound because nothing in this build shells out to protoc: shadnet.pb.cc is
# generated by the desktop job and checked in (see the SHADNET block in the
# top-level CMakeLists), and the UWP package compiles the checked-in output.
#
# All three options, because protobuf's own CMakeLists re-enables
# protobuf_BUILD_LIBPROTOC whenever the binaries or the tests are wanted. Turning
# off only the binaries option left libprotoc building and failing.
set(protobuf_BUILD_TESTS              OFF CACHE INTERNAL "")
set(protobuf_BUILD_EXAMPLES           OFF CACHE INTERNAL "")
set(protobuf_BUILD_PROTOC_BINARIES    ON  CACHE INTERNAL "")
set(protobuf_BUILD_LIBPROTOC          OFF CACHE INTERNAL "")
if (GR2_XBOX_UWP)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE INTERNAL "")
    set(protobuf_BUILD_LIBPROTOC       OFF CACHE INTERNAL "")
    message(STATUS "protobuf: runtime only for the UWP package (libprotoc spawns child processes)")
endif()
set(protobuf_INSTALL                  OFF CACHE INTERNAL "")
set(protobuf_WITH_ZLIB                OFF CACHE INTERNAL "")
set(protobuf_BUILD_SHARED_LIBS        OFF CACHE INTERNAL "")
set(protobuf_LOCAL_DEPENDENCIES_ONLY  ON  CACHE INTERNAL "")
set(protobuf_FORCE_FETCH_DEPENDENCIES OFF CACHE INTERNAL "")
if(MSVC)
    # protobuf overrides CMAKE_MSVC_RUNTIME_LIBRARY in its own directory.
    # Preserve the UWP-wide /MT contract: forcing OFF here silently rebuilt its
    # runtime with /MD and lld rejected the final link with /FAILIFMISMATCH.
    if (GR2_XBOX_UWP)
        set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    else()
        set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
    endif()
endif()
add_subdirectory(protobuf EXCLUDE_FROM_ALL)

if (NOT TARGET zstd::libzstd)
    # zstd (dependency of ZArchive)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    add_subdirectory(zstd/build/cmake)
    add_library(zstd::libzstd ALIAS libzstd_static)
endif()

# ZArchive (.zar archive support)
if (NOT TARGET ZArchive::zarchive)
    add_library(zarchive STATIC
        zarchive/src/zarchivereader.cpp
        zarchive/src/zarchivewriter.cpp
        zarchive/src/sha_256.c
    )
    target_include_directories(zarchive PUBLIC zarchive/include)
    target_include_directories(zarchive PRIVATE zstd/lib)
    target_link_libraries(zarchive PRIVATE zstd::libzstd)
    target_compile_features(zarchive PRIVATE cxx_std_20)
    add_library(ZArchive::zarchive ALIAS zarchive)
endif()
