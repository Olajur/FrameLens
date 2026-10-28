cmake_minimum_required(VERSION 3.21)
project(FrameLens VERSION 0.3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT for every target => self-contained exes, no VC++ redistributable needed.
# Must be global: mixing /MT libs (fl_core) with /MD exes (tests) fails to link (LNK2038).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# PresentData (ETW frame analysis) is written for MSVC; it is ON by default there.
option(FL_WITH_PRESENTDATA "Build ETW frame monitoring with the vendored PresentData library" ${MSVC})
option(FL_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
    if(WIN32)
        add_compile_definitions(NOMINMAX UNICODE _UNICODE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Portable core (no Windows dependency; unit-tested on any OS)
add_library(fl_core STATIC
    src/core/FrameStats.cpp
    src/core/RollingWindow.cpp
    src/core/OverlayGeometry.cpp
    src/core/CpuCores.cpp
    src/ui/Theme.cpp
    src/sensors/GpuInstanceName.cpp
)
target_include_directories(fl_core PUBLIC src)

if(FL_BUILD_TESTS)
    enable_testing()
    add_executable(fl_tests_framestats tests/test_framestats.cpp)
    target_link_libraries(fl_tests_framestats PRIVATE fl_core)
    add_test(NAME framestats COMMAND fl_tests_framestats)

    add_executable(fl_tests_rolling_window tests/test_rolling_window.cpp)
    target_link_libraries(fl_tests_rolling_window PRIVATE fl_core)
    add_test(NAME rolling_window COMMAND fl_tests_rolling_window)

    add_executable(fl_tests_overlay_geometry tests/test_overlay_geometry.cpp)
    target_link_libraries(fl_tests_overlay_geometry PRIVATE fl_core)
    add_test(NAME overlay_geometry COMMAND fl_tests_overlay_geometry)

    add_executable(fl_tests_theme tests/test_theme.cpp)
    target_link_libraries(fl_tests_theme PRIVATE fl_core)
    add_test(NAME theme COMMAND fl_tests_theme)

    add_executable(fl_tests_cpu_cores tests/test_cpu_cores.cpp)
    target_link_libraries(fl_tests_cpu_cores PRIVATE fl_core)
    add_test(NAME cpu_cores COMMAND fl_tests_cpu_cores)

    add_executable(fl_tests_gpu_instance tests/test_gpu_instance.cpp)
    target_link_libraries(fl_tests_gpu_instance PRIVATE fl_core)
    add_test(NAME gpu_instance COMMAND fl_tests_gpu_instance)

    add_executable(fl_tests_present_order tests/test_present_order.cpp)
    target_link_libraries(fl_tests_present_order PRIVATE fl_core)
    add_test(NAME present_order COMMAND fl_tests_present_order)
endif()

if(NOT WIN32)
    message(STATUS "Non-Windows host: building portable core and tests only.")
    return()
endif()

# ---------------------------------------------------------------------------
# Vendored PresentData (MIT, Intel) + FrameLens compatibility shims
if(FL_WITH_PRESENTDATA)
    set(PM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/presentmon)
    add_library(fl_presentdata STATIC
        ${PM_ROOT}/PresentData/Debug.cpp
        ${PM_ROOT}/PresentData/GpuTrace.cpp
        ${PM_ROOT}/PresentData/InterPresentActivity.cpp
        ${PM_ROOT}/PresentData/NvidiaTraceConsumer.cpp
        ${PM_ROOT}/PresentData/PresentMonTraceConsumer.cpp
        ${PM_ROOT}/PresentData/PresentMonTraceSession.cpp
        ${PM_ROOT}/PresentData/TraceConsumer.cpp
        ${PM_ROOT}/PresentData/TraceLogging.cpp
        ${PM_ROOT}/compat/CompatShims.cpp
    )
    target_include_directories(fl_presentdata PUBLIC ${PM_ROOT})
    target_compile_definitions(fl_presentdata PRIVATE _WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000 WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(fl_presentdata PRIVATE /W3)   # upstream code; don't hold it to our /W4
    endif()
    target_link_libraries(fl_presentdata PUBLIC advapi32 tdh)
endif()

# ---------------------------------------------------------------------------
# Windows monitoring engine
# Vendored Intel Graphics Control Library loader (Intel Software License, see third_party/igcl/README.md).
# Loads ControlLib.dll from System32 at runtime; no link-time dependency.
add_library(fl_igcl STATIC third_party/igcl/Source/cApiWrapper.cpp)
target_include_directories(fl_igcl PUBLIC third_party/igcl/include)
target_compile_definitions(fl_igcl PRIVATE WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(fl_igcl PRIVATE /W3)   # upstream code; don't hold it to our /W4
endif()

add_library(fl_engine STATIC
    src/platform/PdhQuery.cpp
    src/sensors/CpuMonitor.cpp
    src/sensors/MemoryMonitor.cpp
    src/sensors/GpuEnumerator.cpp
    src/sensors/GpuPdhMonitor.cpp
    src/sensors/NvmlBackend.cpp
    src/sensors/AdlBackend.cpp
    src/sensors/IgclBackend.cpp
    src/sensors/GpuMonitor.cpp
    src/frames/EtwFrameSource.cpp
    src/engine/MonitorEngine.cpp
)
target_compile_definitions(fl_engine PUBLIC _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
target_link_libraries(fl_engine PUBLIC fl_core pdh dxgi ole32 advapi32 gdi32 PRIVATE fl_igcl)
if(FL_WITH_PRESENTDATA)
    target_compile_definitions(fl_engine PRIVATE FL_WITH_PRESENTDATA=1)
    target_link_libraries(fl_engine PRIVATE fl_presentdata)
else()
    target_compile_definitions(fl_engine PRIVATE FL_WITH_PRESENTDATA=0)
endif()

add_executable(framelens-cli src/app/cli_main.cpp)
target_link_libraries(framelens-cli PRIVATE fl_engine)

# ---------------------------------------------------------------------------
# FrameLens desktop app: engine + overlay (Tier 1, no uiAccess) + taskbar window + tray icon
enable_language(RC)
add_executable(framelens WIN32 src/app/framelens_main.cpp src/app/MainForm.cpp src/app/Monitors.cpp src/app/OverlaySettings.cpp
    src/app/SettingsWindow.cpp src/app/TrayPanel.cpp src/osd/OsdRenderer.cpp src/ui/Draw.cpp src/ui/Form.cpp src/app/framelens.rc)
target_link_libraries(framelens PRIVATE fl_engine d3d11 dxgi d2d1 dwrite dcomp dwmapi shell32 windowscodecs comdlg32)
target_compile_definitions(framelens PRIVATE FL_VERSION="${PROJECT_VERSION}")
if(MSVC)
    # Our .rc embeds the manifest; stop the linker from generating a second one.
    target_link_options(framelens PRIVATE /MANIFEST:NO /ENTRY:wWinMainCRTStartup)
else()
    target_link_options(framelens PRIVATE -municode)
endif()

# Overlay layout preview: renders sample panels (e.g. multi-GPU) to PNG, offscreen. Developer tool.
add_executable(framelens-osd-preview tools/osd-preview/osd_preview.cpp src/osd/OsdRenderer.cpp src/ui/Draw.cpp src/ui/Form.cpp
    src/app/MainForm.cpp src/app/SettingsWindow.cpp src/app/TrayPanel.cpp src/app/OverlaySettings.cpp src/app/Monitors.cpp)
target_link_libraries(framelens-osd-preview PRIVATE fl_core d3d11 dxgi d2d1 dwrite dcomp dwmapi windowscodecs ole32 shell32 comdlg32)
if(MSVC)
    target_link_options(framelens-osd-preview PRIVATE /ENTRY:wmainCRTStartup)
endif()

# ---------------------------------------------------------------------------
# M3.0 OSD spike: same source, two manifests (Tier 1: uiAccess=false, Tier 2: uiAccess=true)
enable_language(RC)
foreach(variant basic uia)
    if(variant STREQUAL "uia")
        set(tgt framelens-osd-spike-uia)
    else()
        set(tgt framelens-osd-spike)
    endif()
    add_executable(${tgt} WIN32 src/osd_spike/osd_spike.cpp src/osd_spike/${variant}.rc)
    target_link_libraries(${tgt} PRIVATE d3d11 dxgi d2d1 dwrite dcomp shell32 ole32 psapi)
    if(MSVC)
        # Our .rc embeds the manifest; stop the linker from generating a second one.
        target_link_options(${tgt} PRIVATE /MANIFEST:NO /ENTRY:wWinMainCRTStartup)
    else()
        target_link_options(${tgt} PRIVATE -municode)
    endif()
endforeach()
