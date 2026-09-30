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
    src/legacy_config/legacy_config.cpp
    src/path_utils.cpp
    src/hotkeys.cpp
    src/tracking_runtime.cpp
    src/build_profile.cpp
    src/runtime_discovery.cpp
    src/runtime_layout.cpp
    src/aim_marker.cpp
    src/hook_install.cpp
    src/camera_hook.cpp
    src/camera_collision.cpp
    src/lean_trace.cpp
    src/crosshair_hook.cpp
    src/fov_hook.cpp
)

add_library(DishonoredHeadTracking SHARED
    ${MOD_SOURCES}
)

# Opt-in native unit tests. Declared here rather than beside the test targets because the
# mod's own output directory depends on it.
option(DISHONORED_BUILD_TESTS "Build DishonoredHeadTracking unit tests" OFF)

# bin/ is written by the SHIPPING tree only. The test tree configures this same target, and
# both scripts/deploy.ps1 and scripts/package-release.ps1 read bin/<config>, so a test run
# would otherwise overwrite the file the packager ships with one built out of a tree
# configured for tests.
if(DISHONORED_BUILD_TESTS)
    set(MOD_OUTPUT_DIR "${CMAKE_BINARY_DIR}/mod")
else()
    set(MOD_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin")
endif()

set_target_properties(DishonoredHeadTracking PROPERTIES
    OUTPUT_NAME "DishonoredHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${MOD_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${MOD_OUTPUT_DIR}"
)

target_include_directories(DishonoredHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/vendor/minhook/src/hde
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
if(DISHONORED_BUILD_TESTS)
    enable_testing()
    add_executable(runtime_discovery_tests tests/runtime_discovery_tests.cpp)
    target_include_directories(runtime_discovery_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/vendor/minhook/src/hde)
    target_link_libraries(runtime_discovery_tests PRIVATE minhook)
    target_compile_options(runtime_discovery_tests PRIVATE /W4 /WX /permissive-)
    target_link_options(runtime_discovery_tests PRIVATE /LARGEADDRESSAWARE)
    add_test(NAME runtime_discovery_tests COMMAND runtime_discovery_tests)
    # The frozen legacy reader is compiled in, not just its headers: the sanitizers were
    # already covered but the CALL SITES were not, so a test drives an INI through
    # legacy::Config::Read and asserts what comes out. src/config.cpp gives the table's
    # defaults.
    add_executable(dishonored_tests
        tests/dishonored_tests.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
        src/path_utils.cpp
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
    endif()
    add_test(NAME dishonored_tests COMMAND dishonored_tests)

    # The config differential test. The oracle is the newest published build's reader and
    # hotkey registration with the core sources they compiled, copied verbatim into
    # tests/config_differential/oracle. It is two libraries because the reader calls the
    # published poller's IsValidHotkeyCode while Hotkeys::Start has to run on oracle_fake's
    # recording poller; each renames the two namespaces its own way, so neither collides
    # with the other or with the current core.
    set(DISHONORED_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(dishonored_config_oracle STATIC
        ${DISHONORED_ORACLE_DIR}/src/config.cpp
        ${DISHONORED_ORACLE_DIR}/core/src/config/ini_reader.cpp
        ${DISHONORED_ORACLE_DIR}/core/src/input/hotkey_poller.cpp
        ${DISHONORED_ORACLE_DIR}/core/src/logging/file_log.cpp
        tests/config_differential/oracle_config_adapter.cpp
    )
    target_include_directories(dishonored_config_oracle PRIVATE
        ${DISHONORED_ORACLE_DIR}/src
        ${DISHONORED_ORACLE_DIR}/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(dishonored_config_oracle PRIVATE
        cameraunlock=dishonored_oracle_core
        DishonoredHeadTracking=dishonored_oracle)

    add_library(dishonored_hotkeys_oracle STATIC
        ${DISHONORED_ORACLE_DIR}/src/hotkeys.cpp
        ${DISHONORED_ORACLE_DIR}/core/src/logging/file_log.cpp
        tests/config_differential/oracle_hotkeys_adapter.cpp
    )
    target_include_directories(dishonored_hotkeys_oracle PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle_fake
        ${DISHONORED_ORACLE_DIR}/src
        ${DISHONORED_ORACLE_DIR}/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(dishonored_hotkeys_oracle PRIVATE
        cameraunlock=dishonored_oracle_hk_core
        DishonoredHeadTracking=dishonored_oracle_hk)

    add_executable(config_differential_tests
        tests/config_differential/differential_tests.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
        src/path_utils.cpp
    )
    target_include_directories(config_differential_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(config_differential_tests PRIVATE
        DISHONORED_DIFFERENTIAL_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/data"
        DISHONORED_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/CameraUnlock.ini")
    target_link_libraries(config_differential_tests PRIVATE
        cameraunlock dishonored_config_oracle dishonored_hotkeys_oracle)
    if(MSVC)
        target_compile_options(config_differential_tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME config_differential_tests COMMAND config_differential_tests)

    # Core's canonical config lint over the committed file and every file the differential
    # test migrated, which it writes beside itself under migrated.
    add_test(NAME config_lint COMMAND node
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:config_differential_tests>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential_tests)
    set_tests_properties(config_differential_tests config_lint PROPERTIES LABELS differential)

    # The committed config against the table, a first start, the toggles' saves and the
    # legacy file's ANSI path. `--render-config <path>` makes it write the committed file
    # instead (pixi run render-config).
    add_executable(config_tests
        tests/config_tests.cpp
        src/config.cpp
        src/legacy_config/legacy_config.cpp
        src/path_utils.cpp
    )
    target_include_directories(config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(config_tests PRIVATE cameraunlock)
    target_compile_definitions(config_tests PRIVATE
        DISHONORED_COMMITTED_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/CameraUnlock.ini")
    if(MSVC)
        target_compile_options(config_tests PRIVATE /W4 /WX /permissive-)
    endif()
    add_test(NAME config_tests COMMAND config_tests)
endif()
