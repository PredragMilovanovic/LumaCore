using System;

namespace LumaCore.Interop
{
    /// <summary>
    /// A native LumaCore scene: point lights and opaque obstacles. Dispose it (or use a
    /// <c>using</c> block) to release native memory promptly; the SafeHandle finalizer is only
    /// a safety net. Not thread-safe while being modified; concurrent Simulate calls are safe.
    /// </summary>
    public sealed class LightingScene : IDisposable
    {
        private readonly SceneHandle _handle;

        public LightingScene()
        {
            LumaCoreLibrary.EnsureCompatible();
            NativeMethods.Check(NativeMethods.lc_scene_create(out _handle));
        }

        /// <summary>Optional room [0,width] x [0,depth] x [0,height] in metres; lights must lie inside it.</summary>
        public void SetRoom(double widthM, double depthM, double heightM)
        {
            NativeMethods.Check(NativeMethods.lc_scene_set_room(Handle, widthM, depthM, heightM));
        }

        public void AddPointLight(Vec3 position, double intensityCd)
        {
            NativeMethods.Check(NativeMethods.lc_scene_add_point_light(Handle, position, intensityCd));
        }

        /// <summary>Solid axis-aligned box; requires min &lt; max on every axis.</summary>
        public void AddBox(Vec3 min, Vec3 max)
        {
            NativeMethods.Check(NativeMethods.lc_scene_add_box(Handle, min, max));
        }

        public void AddSphere(Vec3 center, double radiusM)
        {
            NativeMethods.Check(NativeMethods.lc_scene_add_sphere(Handle, center, radiusM));
        }

        /// <summary>Open, two-sided triangle obstacle.</summary>
        public void AddTriangle(Vec3 a, Vec3 b, Vec3 c)
        {
            NativeMethods.Check(NativeMethods.lc_scene_add_triangle(Handle, a, b, c));
        }

        public int LightCount
        {
            get
            {
                NativeMethods.Check(NativeMethods.lc_scene_get_counts(Handle, out uint lights, out _));
                return checked((int)lights);
            }
        }

        /// <summary>Triangles + spheres (a box counts as 12 triangles).</summary>
        public int OccluderPrimitiveCount
        {
            get
            {
                NativeMethods.Check(NativeMethods.lc_scene_get_counts(Handle, out _, out uint occluders));
                return checked((int)occluders);
            }
        }

        /// <summary>Computes direct illuminance at every grid point.</summary>
        /// <param name="grid">Where to measure.</param>
        /// <param name="options">Backend, threads and shadows; null = threaded on all cores with shadows.</param>
        public SimulationResult Simulate(MeasurementGrid grid, SimulationOptions options = null)
        {
            if (grid == null) throw new ArgumentNullException(nameof(grid));
            var nativeGrid = grid.ToNative();
            var nativeOptions = (options ?? new SimulationOptions()).ToNative();
            var lux = new double[checked((int)grid.PointCount)];

            NativeMethods.Check(NativeMethods.lc_simulate(
                Handle, ref nativeGrid, ref nativeOptions, lux, new UIntPtr((uint)lux.Length),
                out NativeMethods.SimStats stats));
            return new SimulationResult(grid, lux, stats);
        }

        public bool IsDisposed => _handle == null || _handle.IsClosed;

        public void Dispose()
        {
            _handle?.Dispose();
        }

        private SceneHandle Handle
        {
            get
            {
                if (IsDisposed) throw new ObjectDisposedException(nameof(LightingScene));
                return _handle;
            }
        }
    }
}
