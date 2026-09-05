cmake_minimum_required(VERSION 3.20)
project(DishonoredHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "DishonoredHeadTracking must be built as Win32 (x86). Configure with -A Win32.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# MinHook comes from cameraunlock-core's vendored copy, which this switches on.
# It also builds core's cameraunlock_hooks wrapper, which this mod does not use -
# a static library whose unreferenced objects the linker drops.
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/config.cpp
    src/path_utils.cpp
    src/hotkeys.cpp
    src/tracking_runtime.cpp
    src/build_profile.cpp
    src/aim_marker.cpp
    src/hook_install.cpp
    src/camera_hook.cpp
    src/camera_collision.cpp
    src/crosshair_hook.cpp
    src/fov_hook.cpp
)

add_library(DishonoredHeadTracking SHARED
    ${MOD_SOURCES}
)

set_target_properties(DishonoredHeadTracking PROPERTIES
    OUTPUT_NAME "DishonoredHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(DishonoredHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# minhook carries its own include directory, so MinHook.h resolves through the link.
target_link_libraries(DishonoredHeadTracking PRIVATE cameraunlock minhook)

if(MSVC)
    target_compile_options(DishonoredHeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
endif()

if(WIN32)
    target_link_libraries(DishonoredHeadTracking PRIVATE ws2_32)
endif()

# Opt-in native unit tests for the pure logic the hooks inject through: the INI
# sanitizers, the shared FOV range, the UE3 rotator maths and the crosshair projection.
# Off by default so a normal `pixi run build` never builds a test binary into bin/.
# Enable with -DDISHONORED_BUILD_TESTS=ON, or run `pixi run test`.
option(DISHONORED_BUILD_TESTS "Build DishonoredHeadTracking unit tests" OFF)
if(DISHONORED_BUILD_TESTS)
    enable_testing()
    # src/config.cpp is compiled in, not just its headers: the sanitizers were already
    # covered but the CALL SITES were not, so swapping SanitizePositiveLimit back to
    # SanitizeFinite in ReadLimit passed the whole suite. Linking the real parser lets a
    # test drive an INI through Config::LoadOrCreate and assert what comes out.
    add_executable(dishonored_tests
        tests/dishonored_tests.cpp
        src/config.cpp
    )
    target_include_directories(dishonored_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
    )
    target_link_libraries(dishonored_tests PRIVATE cameraunlock)
    set_target_properties(dishonored_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    if(MSVC)
        target_compile_options(dishonored_tests PRIVATE /W4 /WX /permissive-)
        # Dishonored.exe is linked large-address-aware, which is the whole reason the
        # heap-pointer guard takes its ceiling from the OS. MSVC links x86 EXEs
        # /LARGEADDRESSAWARE:NO by default, so without this the test process reports the
        # 2 GB ceiling and the assertion passes without ever seeing the case it exists for.
        target_link_options(dishonored_tests PRIVATE /LARGEADDRESSAWARE)
    endif()
    add_test(NAME dishonored_tests COMMAND dishonored_tests)
endif()
