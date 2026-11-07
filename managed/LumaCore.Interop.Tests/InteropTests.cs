using System;
using System.Runtime.InteropServices;
using LumaCore.Interop;
using Xunit;

namespace LumaCore.Interop.Tests
{
    public class LayoutTests
    {
        // Same numbers as the static_asserts in native/capi/src/lumacore_c.cpp.
        [Fact]
        public void StructSizesMatchNativeLayout()
        {
            Assert.Equal(24, Marshal.SizeOf(typeof(Vec3)));
            Assert.Equal(80, Marshal.SizeOf(typeof(NativeMethods.GridDesc)));
            Assert.Equal(72, Marshal.OffsetOf(typeof(NativeMethods.GridDesc), "Nx").ToInt32());
            Assert.Equal(16, Marshal.SizeOf(typeof(NativeMethods.SimOptions)));
            Assert.Equal(48, Marshal.SizeOf(typeof(NativeMethods.SimStats)));
            Assert.Equal(40, Marshal.OffsetOf(typeof(NativeMethods.SimStats), "ValidPoints").ToInt32());
        }

        [Fact]
        public void RunsAs64BitProcess()
        {
            Assert.True(Environment.Is64BitProcess);
        }

        [Fact]
        public void LibraryVersionIsCompatible()
        {
            Assert.Equal(NativeMethods.ExpectedApiVersion, LumaCoreLibrary.ApiVersion);
            Assert.Equal("0.1.0", LumaCoreLibrary.Version);
        }
    }

    public class SimulationTests
    {
        private static MeasurementGrid OneCellAtOrigin() =>
            new MeasurementGrid(new Vec3(-0.5, -0.5, 0), new Vec3(1, 0, 0), new Vec3(0, 1, 0), 1, 1);

        [Fact]
        public void InverseSquareDirectlyBelow()
        {
            using (var scene = new LightingScene())
            {
                scene.AddPointLight(new Vec3(0, 0, 2), 1000.0);
                var r = scene.Simulate(OneCellAtOrigin(), new SimulationOptions { Backend = Backend.Sequential });
                Assert.Equal(250.0, r[0, 0], 12); // E = I / h^2
                Assert.Equal(250.0, r.AvgLux, 12);
                Assert.Equal(1, r.ValidPoints);
            }
        }

        [Fact]
        public void HorizontalOffsetMatchesClosedForm()
        {
            using (var scene = new LightingScene())
            {
                const double intensity = 1200.0, h = 2.0;
                scene.AddPointLight(new Vec3(0, 0, h), intensity);
                var grid = MeasurementGrid.Horizontal(-3, -3, 3, 3, 0, 12, 12);
                var r = scene.Simulate(grid);
                for (int j = 0; j < grid.Ny; ++j)
                {
                    for (int i = 0; i < grid.Nx; ++i)
                    {
                        var p = grid.Point(i, j);
                        double d2 = p.X * p.X + p.Y * p.Y;
                        double expected = intensity * h / Math.Pow(h * h + d2, 1.5);
                        Assert.InRange(Math.Abs(r[i, j] - expected), 0.0, 1e-12 * expected);
                    }
                }
            }
        }

        [Fact]
        public void ThreadedIsBitIdenticalToSequential()
        {
            using (var scene = new LightingScene())
            {
                scene.SetRoom(6, 4, 3);
                for (int x = 1; x <= 5; x += 2) scene.AddPointLight(new Vec3(x, 2, 2.8), 800.0);
                scene.AddBox(new Vec3(2.5, 1.5, 0), new Vec3(3.5, 2.5, 1.5));
                scene.AddSphere(new Vec3(4.5, 2.6, 1.9), 0.25);
                var grid = MeasurementGrid.Horizontal(0, 0, 6, 4, 0.75, 61, 43);

                var seq = scene.Simulate(grid, new SimulationOptions { Backend = Backend.Sequential });
                var thr = scene.Simulate(grid, new SimulationOptions { Backend = Backend.Threaded, ThreadCount = 7 });
                for (int k = 0; k < seq.Values.Count; ++k)
                {
                    Assert.Equal(BitConverter.DoubleToInt64Bits(seq.Values[k]),
                                 BitConverter.DoubleToInt64Bits(thr.Values[k]));
                }
                Assert.True(double.IsNaN(seq[30, 21]));         // inside the box
                Assert.Equal(seq.ValidPoints, thr.ValidPoints);
                Assert.True(seq.ValidPoints < grid.PointCount);
            }
        }

        [Fact]
        public void ShadowsOptionIsPassedThrough()
        {
            using (var scene = new LightingScene())
            {
                scene.AddPointLight(new Vec3(0, 0, 3), 900.0);
                scene.AddBox(new Vec3(-0.5, -0.5, 1), new Vec3(0.5, 0.5, 2));
                var withShadows = scene.Simulate(OneCellAtOrigin(), new SimulationOptions { Shadows = true });
                var without = scene.Simulate(OneCellAtOrigin(), new SimulationOptions { Shadows = false });
                Assert.Equal(0.0, withShadows[0, 0]);
                Assert.Equal(100.0, without[0, 0], 12);
            }
        }

        [Fact]
        public void CountsReflectAddedObjects()
        {
            using (var scene = new LightingScene())
            {
                scene.AddPointLight(new Vec3(0, 0, 1), 1.0);
                scene.AddBox(new Vec3(0, 0, 0), new Vec3(1, 1, 1));
                scene.AddSphere(new Vec3(5, 5, 5), 1.0);
                scene.AddTriangle(new Vec3(0, 0, 0), new Vec3(1, 0, 0), new Vec3(0, 1, 0));
                Assert.Equal(1, scene.LightCount);
                Assert.Equal(12 + 1 + 1, scene.OccluderPrimitiveCount);
            }
        }
    }

    public class ErrorHandlingTests
    {
        [Fact]
        public void InvalidIntensityThrowsWithNativeMessage()
        {
            using (var scene = new LightingScene())
            {
                var e = Assert.Throws<LumaCoreException>(() => scene.AddPointLight(new Vec3(0, 0, 2), -1.0));
                Assert.Equal(Status.InvalidArgument, e.Status);
                Assert.Contains("intensity", e.Message);
                Assert.Equal(0, scene.LightCount); // failed call did not modify the scene
            }
        }

        [Fact]
        public void LightOutsideRoomIsReportedBySimulate()
        {
            using (var scene = new LightingScene())
            {
                scene.SetRoom(2, 2, 2);
                scene.AddPointLight(new Vec3(5, 5, 5), 100.0);
                var e = Assert.Throws<LumaCoreException>(() =>
                    scene.Simulate(MeasurementGrid.Horizontal(0, 0, 2, 2, 0, 2, 2)));
                Assert.Equal(Status.InvalidArgument, e.Status);
                Assert.Contains("outside the room", e.Message);
            }
        }

        [Fact]
        public void NonPerpendicularGridIsRejectedByEngine()
        {
            using (var scene = new LightingScene())
            {
                var skewed = new MeasurementGrid(new Vec3(0, 0, 0), new Vec3(1, 0, 0), new Vec3(1, 1, 0), 2, 2);
                var e = Assert.Throws<LumaCoreException>(() => scene.Simulate(skewed));
                Assert.Contains("perpendicular", e.Message);
            }
        }

        [Fact]
        public void ManagedArgumentChecks()
        {
            Assert.Throws<ArgumentOutOfRangeException>(() =>
                new MeasurementGrid(new Vec3(), new Vec3(1, 0, 0), new Vec3(0, 1, 0), 0, 1));
            using (var scene = new LightingScene())
            {
                Assert.Throws<ArgumentNullException>(() => scene.Simulate(null));
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                    scene.Simulate(MeasurementGrid.Horizontal(0, 0, 1, 1, 0, 1, 1),
                                   new SimulationOptions { ThreadCount = -1 }));
            }
        }

        [Fact]
        public void UseAfterDisposeThrowsAndDoubleDisposeIsSafe()
        {
            var scene = new LightingScene();
            scene.Dispose();
            Assert.True(scene.IsDisposed);
            Assert.Throws<ObjectDisposedException>(() => scene.AddPointLight(new Vec3(0, 0, 1), 1.0));
            scene.Dispose(); // no-op
        }

        [Fact]
        public void ManyScenesCanBeCreatedAndReleased()
        {
            // Leaks or double frees in the handle lifecycle would surface here.
            for (int k = 0; k < 2000; ++k)
            {
                using (var scene = new LightingScene())
                {
                    scene.AddPointLight(new Vec3(0, 0, 1), 1.0);
                }
            }
        }
    }
}
