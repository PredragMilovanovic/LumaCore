# Native / managed interop

The C++ engine is used from C# (.NET Framework 4.8) through one plain C boundary:
`native/capi/include/lumacore_c.h`, implemented in `lumacore.dll`.

```
C# application ──► LumaCore.Interop.dll ──P/Invoke──► lumacore.dll ──► lumacore_core (static C++)
(LightingScene,      (SafeHandle, structs,             (C ABI: extern "C",
 SimulationResult)    status → exception)               __cdecl, PODs only)
```

## Why a C API instead of exporting C++ classes

C++ has no stable binary interface: name mangling, class layout, `std::vector`/`std::string`
internals and exception handling all depend on the compiler, its version and the runtime
library settings. A C interface with fixed-size types is understood by every compiler and by
the .NET marshaller. The engine stays ordinary modern C++ behind it, statically linked into
the DLL, so no C++ type or runtime state is shared with callers.

## Boundary rules

| Concern | Rule | Where it is enforced |
|---|---|---|
| Types | only `int32_t`/`uint32_t`/`uint64_t`, `double`, `size_t`, POD structs, opaque pointers; no `bool` or `enum`-typed fields (their size is compiler-defined) | header review; `static_assert` on every struct size/offset; C# `Marshal.SizeOf` test with the same numbers |
| Exceptions | never cross the boundary: every export runs its body through `guarded()`, which maps `invalid_argument` → `LC_ERR_INVALID_ARGUMENT`, `bad_alloc` → `LC_ERR_OUT_OF_MEMORY`, everything else → `LC_ERR_INTERNAL` | `lumacore_c.cpp` |
| Error details | thread-local message in a fixed 512-byte buffer (setting it never allocates, so it also works for out-of-memory); `lc_last_error_message` copies it with truncation and returns the needed size | `CApi.ErrorMessageSizeQueryAndTruncation`, `CApi.LastErrorIsPerThread` |
| Ownership | `lc_scene_create` / `lc_scene_destroy` pair; destroy is NULL-safe; output buffers belong to the caller | `CApi.CreateDestroyAndNullSafety`; C# `SceneHandle` |
| Buffers | caller passes capacity; `LC_ERR_BUFFER_TOO_SMALL` if it is less than nx·ny; the engine writes directly into the caller's buffer (no copy) | `CApi.SimulationArgumentChecks` |
| Versioning | `LC_API_VERSION`, checked once by C# before the first call | `LumaCoreLibrary.EnsureCompatible` |
| Exports | exactly the 13 `lc_*` functions, unmangled (hidden visibility for everything else) | `dumpbin /exports lumacore.dll` |
| Bitness | x64 only; C# projects set `PlatformTarget=x64` | `LayoutTests.RunsAs64BitProcess` |

## Managed side

- `NativeMethods` mirrors the header with `[DllImport("lumacore", CallingConvention.Cdecl, ExactSpelling = true)]`.
- `SceneHandle : SafeHandleZeroOrMinusOneIsInvalid` owns the native scene. The runtime releases it
  exactly once, also from the finalizer if `Dispose` is forgotten, and never while a P/Invoke call
  is still using it.
- `LightingScene : IDisposable` is the public API. It turns every non-zero status into a
  `LumaCoreException` with the engine's own message, right after the failing call on the same
  thread (the native message is thread-local).
- `double[]` results are blittable: the marshaller pins the managed array and the engine fills it
  in place.
- All text output uses `CultureInfo.InvariantCulture`; a locale with a decimal comma would
  otherwise corrupt CSV files.

## Verification

- 10 C++ tests call the DLL through its import library like any C caller (`native/tests/test_capi.cpp`).
- 14 xUnit tests run in a 64-bit .NET Framework 4.8 test host (`managed/LumaCore.Interop.Tests`),
  covering layouts, closed-form results, threaded = sequential, shadows, error messages, dispose
  semantics and 2000 create/destroy cycles.
- End to end: the C# console app and the C++ CLI write the office grid as CSV with 17 significant
  digits, and `tools/compare_grids.py` confirms all 5858 measured values are **exactly** equal.

## Deployment note

`lumacore.dll` links the Visual C++ runtime dynamically (`MSVCP140.dll`, `VCRUNTIME140*.dll`).
Machines with Visual Studio have it; others need the Microsoft Visual C++ 2015–2022 Redistributable
(x64). Alternatively, build with the static runtime (`/MT`), which makes the DLL self-contained.
