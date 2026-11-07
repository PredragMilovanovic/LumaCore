using System;

namespace LumaCore.Interop
{
    /// <summary>Native status codes (LC_OK, LC_ERR_*).</summary>
    public enum Status
    {
        Ok = 0,
        NullArgument = 1,
        InvalidArgument = 2,
        BufferTooSmall = 3,
        OutOfMemory = 4,
        Internal = 5,
    }

    /// <summary>A native call failed. <see cref="Exception.Message"/> is the engine's own text.</summary>
    [Serializable]
    public sealed class LumaCoreException : Exception
    {
        public LumaCoreException(Status status, string message) : base(message)
        {
            Status = status;
        }

        private LumaCoreException(System.Runtime.Serialization.SerializationInfo info,
                                  System.Runtime.Serialization.StreamingContext context)
            : base(info, context)
        {
            Status = (Status)info.GetInt32(nameof(Status));
        }

        public Status Status { get; }

        public override void GetObjectData(System.Runtime.Serialization.SerializationInfo info,
                                           System.Runtime.Serialization.StreamingContext context)
        {
            base.GetObjectData(info, context);
            info.AddValue(nameof(Status), (int)Status);
        }

        public override string ToString() => $"LumaCoreException ({Status}): {Message}";
    }
}
