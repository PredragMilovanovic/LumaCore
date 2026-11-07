using System;
using System.Collections.Generic;

namespace LumaCore.Interop
{
    /// <summary>
    /// Planar measurement grid: the rectangle Origin .. Origin + UEdge + VEdge in Nx x Ny cells,
    /// measured at cell centres. The edges must be perpendicular (checked by the engine);
    /// the surface faces UEdge x VEdge.
    /// </summary>
    public sealed class MeasurementGrid
    {
        public MeasurementGrid(Vec3 origin, Vec3 uEdge, Vec3 vEdge, int nx, int ny)
        {
            if (nx <= 0) throw new ArgumentOutOfRangeException(nameof(nx), nx, "must be > 0");
            if (ny <= 0) throw new ArgumentOutOfRangeException(nameof(ny), ny, "must be > 0");
            Origin = origin;
            UEdge = uEdge;
            VEdge = vEdge;
            Nx = nx;
            Ny = ny;
        }

        /// <summary>Upward-facing work plane over [x0, x1] x [y0, y1] at height z.</summary>
        public static MeasurementGrid Horizontal(double x0, double y0, double x1, double y1, double z,
                                                 int nx, int ny)
        {
            if (!(x1 > x0) || !(y1 > y0))
            {
                throw new ArgumentException("need x1 > x0 and y1 > y0");
            }
            return new MeasurementGrid(new Vec3(x0, y0, z), new Vec3(x1 - x0, 0, 0),
                                       new Vec3(0, y1 - y0, 0), nx, ny);
        }

        public Vec3 Origin { get; }
        public Vec3 UEdge { get; }
        public Vec3 VEdge { get; }
        public int Nx { get; }
        public int Ny { get; }
        public long PointCount => (long)Nx * Ny;

        /// <summary>Centre of cell (i, j).</summary>
        public Vec3 Point(int i, int j) =>
            Origin + UEdge * ((i + 0.5) / Nx) + VEdge * ((j + 0.5) / Ny);

        internal NativeMethods.GridDesc ToNative() => new NativeMethods.GridDesc
        {
            Origin = Origin,
            UEdge = UEdge,
            VEdge = VEdge,
            Nx = (uint)Nx,
            Ny = (uint)Ny,
        };
    }

    public enum Backend
    {
        Sequential = NativeMethods.BackendSequential,
        Threaded = NativeMethods.BackendThreaded,
    }

    public sealed class SimulationOptions
    {
        public Backend Backend { get; set; } = Backend.Threaded;

        /// <summary>Threaded backend only; 0 = all hardware threads.</summary>
        public int ThreadCount { get; set; }

        public bool Shadows { get; set; } = true;

        internal NativeMethods.SimOptions ToNative()
        {
            if (ThreadCount < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(ThreadCount), ThreadCount, "must be >= 0");
            }
            return new NativeMethods.SimOptions
            {
                Backend = (int)Backend,
                ThreadCount = (uint)ThreadCount,
                Shadows = Shadows ? 1 : 0,
                Reserved = 0,
            };
        }
    }

    /// <summary>Illuminance values [lx] and statistics of one simulation run.</summary>
    public sealed class SimulationResult
    {
        private readonly double[] _lux;

        internal SimulationResult(MeasurementGrid grid, double[] lux, NativeMethods.SimStats stats)
        {
            Grid = grid;
            _lux = lux;
            Values = Array.AsReadOnly(lux);
            MinLux = stats.MinLux;
            MaxLux = stats.MaxLux;
            AvgLux = stats.AvgLux;
            UniformityU0 = stats.UniformityU0;
            ElapsedMs = stats.ElapsedMs;
            ValidPoints = (long)stats.ValidPoints;
        }

        public MeasurementGrid Grid { get; }

        /// <summary>Row-major values, index = j * Nx + i. NaN = not measured (inside an obstacle).</summary>
        public IReadOnlyList<double> Values { get; }

        /// <summary>Illuminance [lx] at cell (i, j); NaN if the point is inside an obstacle.</summary>
        public double this[int i, int j]
        {
            get
            {
                if ((uint)i >= (uint)Grid.Nx) throw new ArgumentOutOfRangeException(nameof(i));
                if ((uint)j >= (uint)Grid.Ny) throw new ArgumentOutOfRangeException(nameof(j));
                return _lux[(long)j * Grid.Nx + i];
            }
        }

        public double MinLux { get; }
        public double MaxLux { get; }
        public double AvgLux { get; }

        /// <summary>U0 = E_min / E_avg (EN 12464-1).</summary>
        public double UniformityU0 { get; }

        /// <summary>Native calculation time, excluding marshalling.</summary>
        public double ElapsedMs { get; }

        public long ValidPoints { get; }
    }
}
