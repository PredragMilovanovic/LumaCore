using System;
using System.Runtime.InteropServices;
using System.Security;
using System.Text;

namespace LumaCore.Interop
{
    /// <summary>
    /// P/Invoke declarations mirroring native/capi/include/lumacore_c.h one to one.
    /// Keep in sync with the header; <see cref="ExpectedApiVersion"/> guards against drift
    /// and the tests compare every struct size with the native static_asserts.
    /// </summary>
    [SuppressUnmanagedCodeSecurity]
    internal static class NativeMethods
    {
        internal const string Library = "lumacore";
        internal const uint ExpectedApiVersion = 1;

        internal const int BackendSequential = 0;
        internal const int BackendThreaded = 1;

        [StructLayout(LayoutKind.Sequential)]
        internal struct GridDesc // 80 bytes
        {
            public Vec3 Origin;
            public Vec3 UEdge;
            public Vec3 VEdge;
            public uint Nx;
            public uint Ny;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct SimOptions // 16 bytes
        {
            public int Backend;
            public uint ThreadCount;
            public int Shadows;
            public int Reserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct SimStats // 48 bytes
        {
            public double MinLux;
            public double MaxLux;
            public double AvgLux;
            public double UniformityU0;
            public double ElapsedMs;
            public ulong ValidPoints;
        }

        // ---- Library ----

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern uint lc_api_version();

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr lc_version_string();

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr lc_status_name(int status);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern UIntPtr lc_last_error_message(byte[] buffer, UIntPtr bufferSize);

        // ---- Scene ----

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_create(out SceneHandle scene);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void lc_scene_destroy(IntPtr scene);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_set_room(SceneHandle scene, double widthM, double depthM,
                                                     double heightM);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_add_point_light(SceneHandle scene, Vec3 position,
                                                            double intensityCd);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_add_box(SceneHandle scene, Vec3 min, Vec3 max);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_add_sphere(SceneHandle scene, Vec3 center, double radiusM);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_add_triangle(SceneHandle scene, Vec3 a, Vec3 b, Vec3 c);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_scene_get_counts(SceneHandle scene, out uint lights,
                                                       out uint occluders);

        // ---- Simulation ----

        // double[] is blittable: the marshaller pins it and the native code writes in place.
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int lc_simulate(SceneHandle scene, ref GridDesc grid,
                                               ref SimOptions options, [Out] double[] outLux,
                                               UIntPtr outCount, out SimStats stats);

        // ---- Helpers ----

        /// <summary>Throws <see cref="LumaCoreException"/> if a native call failed.</summary>
        internal static void Check(int status)
        {
            if (status != 0)
            {
                throw new LumaCoreException((Status)status, LastErrorMessage());
            }
        }

        /// <summary>
        /// The native thread-local message of the last failure. Must be called on the thread
        /// that made the failing call, before any other native call (Check does exactly that).
        /// </summary>
        internal static string LastErrorMessage()
        {
            int needed = checked((int)lc_last_error_message(null, UIntPtr.Zero).ToUInt64());
            if (needed <= 1)
            {
                return "unknown native error";
            }
            var buffer = new byte[needed];
            lc_last_error_message(buffer, new UIntPtr((uint)buffer.Length));
            return Encoding.UTF8.GetString(buffer, 0, needed - 1);
        }
    }
}
