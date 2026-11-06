# lumacore_core: the engine as a static library. It has no dependency on the
# C API or on any managed code; the C API DLL (Milestone 5) links it statically.
add_library(lumacore_core STATIC
    src/version.cpp
    src/math/ray.cpp
    src/geometry/shapes.cpp
    src/geometry/intersection.cpp
    src/geometry/primitives.cpp
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

# Small visual check of the geometry layer: writes a ray-cast image (BMP).
add_executable(lumacore_geometry_demo apps/geometry_demo/main.cpp)
target_link_libraries(lumacore_geometry_demo PRIVATE lumacore::core)
lumacore_set_warnings(lumacore_geometry_demo)
