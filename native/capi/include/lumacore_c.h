/*
 * LumaCore C API - the only binary boundary of the engine (lumacore.dll).
 *
 * Rules (see CONTRIBUTING.md, "Native API rules"):
 *  - Plain C: only fixed-width integers, doubles, POD structs, opaque handles and
 *    caller-allocated buffers. No C++ types, no exceptions and no bool cross this boundary.
 *  - Every function that can fail returns an LcStatus. On failure a thread-local,
 *    human-readable message is available from lc_last_error_message() until the next
 *    failing call on the same thread.
 *  - Ownership: a scene created by lc_scene_create() must be released with exactly one
 *    lc_scene_destroy(). Output buffers are allocated and owned by the caller.
 *  - Threads: calls on different scenes may run concurrently. A scene must not be modified
 *    while another thread uses it; concurrent lc_simulate() calls on one scene are safe.
 *  - Units: metres, candela, lux, milliseconds.
 *  - Any change to a signature or struct layout must increment LC_API_VERSION.
 */
#ifndef LUMACORE_C_H
#define LUMACORE_C_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(LUMACORE_BUILDING_DLL)
#    define LC_API __declspec(dllexport)
#  else
#    define LC_API __declspec(dllimport)
#  endif
#  define LC_CALL __cdecl
#else
#  define LC_API __attribute__((visibility("default")))
#  define LC_CALL
#endif

#define LC_API_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Status codes (int32) ------------------------------------------------ */

typedef int32_t LcStatus;
#define LC_OK                   0
#define LC_ERR_NULL_ARGUMENT    1 /* a required pointer argument was NULL               */
#define LC_ERR_INVALID_ARGUMENT 2 /* a value is out of range, inconsistent or not finite */
#define LC_ERR_BUFFER_TOO_SMALL 3 /* out_count < nx * ny                                 */
#define LC_ERR_OUT_OF_MEMORY    4
#define LC_ERR_INTERNAL         5 /* unexpected failure, e.g. a thread could not start   */

/* ---- Value types --------------------------------------------------------- */

typedef struct LcVec3 {
    double x, y, z;
} LcVec3; /* 24 bytes */

/* Planar measurement grid: the rectangle origin .. origin + u_edge + v_edge, divided into
 * nx * ny cells, measured at cell centres. u_edge and v_edge must be perpendicular; the
 * surface normal is u_edge x v_edge (normalized). */
typedef struct LcGridDesc {
    LcVec3 origin;
    LcVec3 u_edge;
    LcVec3 v_edge;
    uint32_t nx;
    uint32_t ny;
} LcGridDesc; /* 80 bytes */

#define LC_BACKEND_SEQUENTIAL 0
#define LC_BACKEND_THREADED   1

typedef struct LcSimOptions {
    int32_t backend;       /* LC_BACKEND_*                                     */
    uint32_t thread_count; /* threaded backend only; 0 = all hardware threads  */
    int32_t shadows;       /* 0 = ignore occluders, non-zero = cast shadows     */
    int32_t reserved;      /* must be 0                                         */
} LcSimOptions; /* 16 bytes */

typedef struct LcSimStats {
    double min_lux;       /* over measured points                                  */
    double max_lux;
    double avg_lux;
    double uniformity_u0; /* min_lux / avg_lux, 0 if avg_lux == 0                   */
    double elapsed_ms;    /* calculation wall time                                 */
    uint64_t valid_points; /* points not inside a solid obstacle                    */
} LcSimStats; /* 48 bytes */

typedef struct LcScene LcScene; /* opaque */

/* ---- Library ------------------------------------------------------------- */

/* LC_API_VERSION the DLL was built with. Callers should refuse to run on a mismatch. */
LC_API uint32_t LC_CALL lc_api_version(void);

/* Engine version, e.g. "0.1.0". Static storage; never free it. */
LC_API const char* LC_CALL lc_version_string(void);

/* Name of a status code, e.g. "LC_ERR_INVALID_ARGUMENT". Static storage. */
LC_API const char* LC_CALL lc_status_name(LcStatus status);

/* Copies the calling thread's last error message (UTF-8, NUL-terminated, truncated to fit)
 * into buffer. Returns the size needed including the NUL. Pass buffer = NULL or
 * buffer_size = 0 to query the size only. Returns 1 (just the NUL) if there is no error. */
LC_API size_t LC_CALL lc_last_error_message(char* buffer, size_t buffer_size);

/* ---- Scene --------------------------------------------------------------- */

/* Creates an empty scene. On success *out_scene receives the handle; on failure it is set
 * to NULL. */
LC_API LcStatus LC_CALL lc_scene_create(LcScene** out_scene);

/* Releases a scene. NULL is ignored. Using the handle afterwards is undefined behaviour. */
LC_API void LC_CALL lc_scene_destroy(LcScene* scene);

/* Optional axis-aligned room [0,width] x [0,depth] x [0,height]; lights must be inside it.
 * Its walls are not occluders. */
LC_API LcStatus LC_CALL lc_scene_set_room(LcScene* scene, double width_m, double depth_m,
                                          double height_m);

LC_API LcStatus LC_CALL lc_scene_add_point_light(LcScene* scene, LcVec3 position,
                                                 double intensity_cd);

/* Solid axis-aligned box (12 triangles). Requires min < max on every axis. */
LC_API LcStatus LC_CALL lc_scene_add_box(LcScene* scene, LcVec3 min, LcVec3 max);

LC_API LcStatus LC_CALL lc_scene_add_sphere(LcScene* scene, LcVec3 center, double radius_m);

/* Open (two-sided) triangle occluder. */
LC_API LcStatus LC_CALL lc_scene_add_triangle(LcScene* scene, LcVec3 a, LcVec3 b, LcVec3 c);

/* Number of lights and of occluder primitives (triangles + spheres). Either output may be
 * NULL. */
LC_API LcStatus LC_CALL lc_scene_get_counts(const LcScene* scene, uint32_t* out_lights,
                                            uint32_t* out_occluders);

/* ---- Simulation ---------------------------------------------------------- */

/* Computes direct illuminance [lx] at every grid point and writes nx * ny values, row-major
 * (index = j * nx + i), into out_lux. out_count is the capacity of out_lux in doubles and
 * must be >= nx * ny. Points inside a solid obstacle are written as NaN (not measured).
 * options = NULL means threaded on all hardware threads with shadows. out_stats may be
 * NULL. On failure out_lux contents are unspecified. */
LC_API LcStatus LC_CALL lc_simulate(const LcScene* scene, const LcGridDesc* grid,
                                    const LcSimOptions* options, double* out_lux,
                                    size_t out_count, LcSimStats* out_stats);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LUMACORE_C_H */
