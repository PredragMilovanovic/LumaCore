# lumacore_core: the engine as a static library. It has no dependency on the
# C API or on any managed code; the C API DLL (Milestone 5) links it statically.
add_library(lumacore_core STATIC
    src/version.cpp
    src/math/ray.cpp
    src/geometry/shapes.cpp
    src/geometry/intersection.cpp
    src/geometry/primitives.cpp
    src/lighting/lighting.cpp
    src/scene/scene.cpp
    src/scene/measurement_grid.cpp
    src/simulation/simulation.cpp
    src/parallel/threaded_executor.cpp
)
add_library(lumacore::core ALIAS lumacore_core)

target_include_directories(lumacore_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(lumacore_core PUBLIC cxx_std_17)
target_compile_definitions(lumacore_core PUBLIC
    LUMACORE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    LUMACORE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    LUMACORE_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# The C API DLL links this library, so it must be position-independent on non-Windows toolchains.
set_target_properties(lumacore_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

lumacore_set_warnings(lumacore_core)

# lumacore: the C API as a shared library (lumacore.dll). It links the engine statically,
# so no C++ types or C++ runtime state are shared with callers.
add_library(lumacore SHARED capi/src/lumacore_c.cpp)
add_library(lumacore::capi ALIAS lumacore)
target_include_directories(lumacore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/capi/include)
target_link_libraries(lumacore PRIVATE lumacore::core)
target_compile_definitions(lumacore PRIVATE LUMACORE_BUILDING_DLL)
set_target_properties(lumacore PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
lumacore_set_warnings(lumacore)

# Small visual check of the geometry layer: writes a ray-cast image (BMP).
add_executable(lumacore_geometry_demo apps/geometry_demo/main.cpp)
target_link_libraries(lumacore_geometry_demo PRIVATE lumacore::core)
lumacore_set_warnings(lumacore_geometry_demo)

# Built-in scenarios shared by the CLI and the benchmark.
add_library(lumacore_scenarios STATIC apps/common/scenarios.cpp)
target_link_libraries(lumacore_scenarios PUBLIC lumacore::core)
lumacore_set_warnings(lumacore_scenarios)

# Runs a lighting scenario and writes the illuminance grid as CSV.
add_executable(lumacore_cli apps/cli/main.cpp)
target_link_libraries(lumacore_cli PRIVATE lumacore_scenarios)
lumacore_set_warnings(lumacore_cli)

# Sequential vs threaded timing; writes results/bench.csv.
add_executable(lumacore_bench bench/main.cpp)
target_link_libraries(lumacore_bench PRIVATE lumacore_scenarios)
lumacore_set_warnings(lumacore_bench)
