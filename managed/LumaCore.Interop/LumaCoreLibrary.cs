using System;
using System.Runtime.InteropServices;

namespace LumaCore.Interop
{
    /// <summary>Information about the loaded native library and a one-time compatibility check.</summary>
    public static class LumaCoreLibrary
    {
        private static volatile bool s_verified;

        /// <summary>LC_API_VERSION of the loaded lumacore.dll.</summary>
        public static uint ApiVersion
        {
            get
            {
                EnsureCompatible();
                return NativeMethods.lc_api_version();
            }
        }

        /// <summary>Engine version, e.g. "0.1.0".</summary>
        public static string Version
        {
            get
            {
                EnsureCompatible();
                return Marshal.PtrToStringAnsi(NativeMethods.lc_version_string());
            }
        }

        /// <summary>
        /// Loads lumacore.dll and checks its API version. Called automatically before first use;
        /// throws a descriptive exception instead of failing later with a corrupted call.
        /// </summary>
        public static void EnsureCompatible()
        {
            if (s_verified)
            {
                return;
            }

            uint native;
            try
            {
                native = NativeMethods.lc_api_version();
            }
            catch (DllNotFoundException e)
            {
                throw new DllNotFoundException(
                    "lumacore.dll was not found next to the application. Build the native " +
                    "code first (cmake --build --preset msvc-release); the managed build copies it.", e);
            }
            catch (BadImageFormatException e)
            {
                throw new BadImageFormatException(
                    "lumacore.dll is a 64-bit library; run this process as x64.", e);
            }

            if (native != NativeMethods.ExpectedApiVersion)
            {
                throw new InvalidOperationException(
                    $"lumacore.dll has API version {native}, but LumaCore.Interop was built for " +
                    $"version {NativeMethods.ExpectedApiVersion}. Rebuild both from the same sources.");
            }
            s_verified = true;
        }
    }
}
