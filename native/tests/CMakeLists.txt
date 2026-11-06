include(FetchContent)

# Pinned GoogleTest release; downloaded once into build/_deps.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Use the same (dynamic) CRT as our targets on MSVC.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(lumacore_tests
    test_version.cpp
    test_vec3.cpp
    test_ray.cpp
    test_intersection_plane.cpp
    test_intersection_sphere.cpp
    test_intersection_triangle.cpp
    test_primitives.cpp
    test_illuminance.cpp
    test_measurement_grid.cpp
    test_scene.cpp
    test_simulation.cpp
)
target_link_libraries(lumacore_tests PRIVATE lumacore::core GTest::gtest_main)
lumacore_set_warnings(lumacore_tests)

include(GoogleTest)
gtest_discover_tests(lumacore_tests DISCOVERY_MODE PRE_TEST)
