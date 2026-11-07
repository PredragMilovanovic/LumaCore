using Microsoft.Win32.SafeHandles;

namespace LumaCore.Interop
{
    /// <summary>
    /// Owns one native LcScene. The runtime guarantees ReleaseHandle runs exactly once, even
    /// if Dispose is never called (finalizer) or a thread is aborted, and it prevents the
    /// handle from being released while a P/Invoke call is still using it.
    /// </summary>
    internal sealed class SceneHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        // Used by the P/Invoke marshaller for "out SceneHandle".
        private SceneHandle() : base(ownsHandle: true)
        {
        }

        protected override bool ReleaseHandle()
        {
            NativeMethods.lc_scene_destroy(handle);
            return true;
        }
    }
}
