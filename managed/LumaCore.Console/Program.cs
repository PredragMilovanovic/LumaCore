using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using LumaCore.Interop;

namespace LumaCore.ConsoleApp
{
    /// <summary>
    /// Runs the office scenario through lumacore.dll from C#, compares the sequential and
    /// threaded backends, prints the statistics and an ASCII heatmap, and optionally writes
    /// a CSV that tools/heatmap.py can plot.
    ///
    /// Usage: LumaCore.Console [--csv &lt;file&gt;] [--threads &lt;n&gt;] [--cell &lt;m&gt;]
    /// </summary>
    internal static class Program
    {
        // Same office as native/apps/common/scenarios.cpp.
        private const double Width = 6.0, Depth = 4.0, Height = 3.0, WorkPlane = 0.75, Border = 0.5;

        private static readonly Vec3[] Lights =
        {
            new Vec3(1, 1, 2.8), new Vec3(3, 1, 2.8), new Vec3(5, 1, 2.8),
            new Vec3(1, 3, 2.8), new Vec3(3, 3, 2.8), new Vec3(5, 3, 2.8),
        };
        private const double LightIntensityCd = 800.0;

        private static readonly (Vec3 Min, Vec3 Max)[] Boxes =
        {
            (new Vec3(0.05, 2.6, 0.0), new Vec3(0.65, 3.95, 2.0)), // cabinet
            (new Vec3(2.9, 0.2, 0.0), new Vec3(3.1, 1.6, 1.6)),    // partition
        };
        private static readonly (Vec3 Center, double Radius) Globe = (new Vec3(4.5, 2.6, 1.9), 0.25);

        private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        private static int Main(string[] args)
        {
            try
            {
                return Run(args);
            }
            catch (LumaCoreException e)
            {
                Console.Error.WriteLine($"native error ({e.Status}): {e.Message}");
                return 1;
            }
            catch (Exception e) when (e is ArgumentException || e is IOException ||
                                      e is DllNotFoundException || e is InvalidOperationException)
            {
                Console.Error.WriteLine($"error: {e.Message}");
                return 1;
            }
        }

        private static int Run(string[] args)
        {
            string csvPath = null;
            int threads = 0;
            double cell = 0.05;
            for (int k = 0; k < args.Length; ++k)
            {
                if (args[k] == "--csv" && k + 1 < args.Length) csvPath = args[++k];
                else if (args[k] == "--threads" && k + 1 < args.Length) threads = int.Parse(args[++k], Inv);
                else if (args[k] == "--cell" && k + 1 < args.Length) cell = double.Parse(args[++k], Inv);
                else
                {
                    Console.Error.WriteLine("usage: LumaCore.Console [--csv <file>] [--threads <n>] [--cell <m>]");
                    return 2;
                }
            }
            if (!(cell >= 0.005 && cell <= 1.0)) throw new ArgumentException("--cell must be in 0.005..1.0 m");

            Console.WriteLine($"LumaCore.Console (C#, .NET Framework {Environment.Version}, " +
                              $"{(Environment.Is64BitProcess ? "x64" : "x86")})");
            Console.WriteLine($"  native library  lumacore.dll {LumaCoreLibrary.Version}, API v{LumaCoreLibrary.ApiVersion}");

            using (var scene = new LightingScene())
            {
                scene.SetRoom(Width, Depth, Height);
                foreach (var p in Lights) scene.AddPointLight(p, LightIntensityCd);
                foreach (var b in Boxes) scene.AddBox(b.Min, b.Max);
                scene.AddSphere(Globe.Center, Globe.Radius);

                int nx = (int)Math.Round((Width - 2 * Border) / cell);
                int ny = (int)Math.Round((Depth - 2 * Border) / cell);
                var grid = MeasurementGrid.Horizontal(Border, Border, Width - Border, Depth - Border,
                                                      WorkPlane, nx, ny);

                Console.WriteLine($"  scene           {scene.LightCount} lights, {scene.OccluderPrimitiveCount} occluder primitives");
                Console.WriteLine($"  grid            {nx} x {ny} = {grid.PointCount} points, work plane {WorkPlane.ToString(Inv)} m");

                var sequential = scene.Simulate(grid, new SimulationOptions { Backend = Backend.Sequential });
                var threaded = scene.Simulate(grid, new SimulationOptions { Backend = Backend.Threaded, ThreadCount = threads });
                bool identical = BitIdentical(sequential.Values, threaded.Values);

                Console.WriteLine();
                Console.WriteLine(string.Format(Inv, "  E_min {0,7:F1} lx   E_avg {1,7:F1} lx   E_max {2,7:F1} lx   U0 {3:F3}",
                    threaded.MinLux, threaded.AvgLux, threaded.MaxLux, threaded.UniformityU0));
                Console.WriteLine(string.Format(Inv, "  measured {0} of {1} points (rest inside obstacles)",
                    threaded.ValidPoints, grid.PointCount));
                Console.WriteLine(string.Format(Inv, "  sequential {0,8:F2} ms   threaded {1,8:F2} ms   speedup {2:F1}x",
                    sequential.ElapsedMs, threaded.ElapsedMs, sequential.ElapsedMs / threaded.ElapsedMs));
                Console.WriteLine($"  threaded result bit-identical to sequential: {(identical ? "yes" : "NO")}");
                Console.WriteLine();
                PrintAsciiHeatmap(threaded);

                if (csvPath != null)
                {
                    WriteCsv(csvPath, threaded);
                    Console.WriteLine($"  wrote {csvPath}  (plot: python tools\\heatmap.py {csvPath})");
                }
                return identical ? 0 : 1;
            }
        }

        private static bool BitIdentical(IReadOnlyList<double> a, IReadOnlyList<double> b)
        {
            if (a.Count != b.Count) return false;
            for (int k = 0; k < a.Count; ++k)
            {
                if (BitConverter.DoubleToInt64Bits(a[k]) != BitConverter.DoubleToInt64Bits(b[k])) return false;
            }
            return true;
        }

        /// <summary>Downsampled preview: darker character = more light; 'X' = inside an obstacle.</summary>
        private static void PrintAsciiHeatmap(SimulationResult r)
        {
            const string ramp = " .:-=+*#%@";
            const int cols = 60;
            int rows = Math.Max(1, (int)Math.Round(cols * (Depth - 2 * Border) / (Width - 2 * Border) / 2.0));
            Console.WriteLine(string.Format(Inv, "  work-plane preview ({0} lx = ' ' .. {1:F0} lx = '@', X = obstacle):",
                0, r.MaxLux));
            for (int row = rows - 1; row >= 0; --row) // +y at the top, like the PNG
            {
                var line = new StringBuilder("  |");
                for (int col = 0; col < cols; ++col)
                {
                    int i = Math.Min(r.Grid.Nx - 1, (int)((col + 0.5) / cols * r.Grid.Nx));
                    int j = Math.Min(r.Grid.Ny - 1, (int)((row + 0.5) / rows * r.Grid.Ny));
                    double e = r[i, j];
                    line.Append(double.IsNaN(e)
                        ? 'X'
                        : ramp[Math.Min(ramp.Length - 1, (int)(e / r.MaxLux * (ramp.Length - 1) + 0.5))]);
                }
                Console.WriteLine(line.Append('|'));
            }
            Console.WriteLine();
        }

        /// <summary>Same format as lumacore_cli, so tools/heatmap.py can plot it. Invariant culture
        /// throughout: a locale with a decimal comma would otherwise corrupt the CSV.</summary>
        private static void WriteCsv(string path, SimulationResult r)
        {
            var g = r.Grid;
            using (var w = new StreamWriter(path, false, new UTF8Encoding(false)))
            {
                w.WriteLine($"# lumacore,{LumaCoreLibrary.Version}");
                w.WriteLine("# scenario,office (C#)");
                w.WriteLine("# shadows,1");
                w.WriteLine(F("# room,{0},{1},{2}", Width, Depth, Height));
                w.WriteLine(F("# grid,{0},{1},{2},{3},{4},{5},{6}", g.Nx, g.Ny, g.Origin.X, g.Origin.Y,
                              g.Origin.X + g.UEdge.X, g.Origin.Y + g.VEdge.Y, WorkPlane));
                foreach (var p in Lights) w.WriteLine(F("# light,{0},{1},{2},{3}", p.X, p.Y, p.Z, LightIntensityCd));
                foreach (var b in Boxes)
                    w.WriteLine(F("# box,{0},{1},{2},{3},{4},{5}", b.Min.X, b.Min.Y, b.Min.Z, b.Max.X, b.Max.Y, b.Max.Z));
                w.WriteLine(F("# sphere,{0},{1},{2},{3}", Globe.Center.X, Globe.Center.Y, Globe.Center.Z, Globe.Radius));
                w.WriteLine(F("# stats,{0:R},{1:R},{2:R},{3:R},{4}", r.MinLux, r.MaxLux, r.AvgLux, r.UniformityU0, r.ValidPoints));
                w.WriteLine("i,j,x_m,y_m,z_m,lux");
                for (int j = 0; j < g.Ny; ++j)
                {
                    for (int i = 0; i < g.Nx; ++i)
                    {
                        var p = g.Point(i, j);
                        double e = r[i, j];
                        w.WriteLine(F("{0},{1},{2:R},{3:R},{4:R},{5}", i, j, p.X, p.Y, p.Z,
                                      double.IsNaN(e) ? "nan" : e.ToString("R", Inv)));
                    }
                }
            }
        }

        private static string F(string format, params object[] args) => string.Format(Inv, format, args);
    }
}
