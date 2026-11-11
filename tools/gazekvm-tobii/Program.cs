// gazekvm-tobii: reads gaze, head pose and presence through Tobii.Interaction
// (the Tobii Core SDK, which talks to Tobii Experience) and forwards them to
// gazekvm over localhost UDP. gazekvm starts and stops this process itself.
//
//   gazekvm-tobii [--port 4243] [--parent PID] [--print]
//
// Packet (48 bytes, little-endian), sent at ~60 Hz:
//   uint32 magic 'GKT1' | uint32 flags | double gazeX, gazeY (screen pixels)
//   | double yaw, pitch, roll (degrees)
// flags: 1 = user present (Tobii presence), 2 = gaze fresh, 4 = head pose fresh.
// Nothing is stored; data only goes to 127.0.0.1.
using System;
using System.Diagnostics;
using System.Net.Sockets;
using System.Threading;
using Tobii.Interaction;

namespace GazekvmTobii
{
    static class Program
    {
        const uint Magic = 0x31544B47;  // "GKT1"
        const uint FlagPresent = 1, FlagGaze = 2, FlagHead = 4;
        const double FreshMs = 100;

        static readonly object Gate = new object();
        static double gazeX, gazeY, yaw, pitch, roll;
        static long gazeAt, headAt;  // Stopwatch ticks of last valid sample
        static bool userPresent;

        static int Main(string[] args)
        {
            int port = 4243, parentPid = 0;
            bool print = false;
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "--port" && i + 1 < args.Length) port = int.Parse(args[++i]);
                else if (args[i] == "--parent" && i + 1 < args.Length) parentPid = int.Parse(args[++i]);
                else if (args[i] == "--print") print = true;
                else { Console.Error.WriteLine($"unknown argument: {args[i]}"); return 2; }
            }

            Process parent = null;
            if (parentPid != 0)
            {
                try { parent = Process.GetProcessById(parentPid); }
                catch (ArgumentException) { return 0; }  // already gone
            }

            var host = new Host();
            var gaze = host.Streams.CreateGazePointDataStream();
            gaze.GazePoint((x, y, ts) =>
            {
                if (double.IsNaN(x) || double.IsNaN(y)) return;
                lock (Gate) { gazeX = x; gazeY = y; gazeAt = Stopwatch.GetTimestamp(); }
            });

            var head = host.Streams.CreateHeadPoseStream();
            head.Next += (o, h) =>
            {
                var d = h.Data;
                var r = d.HeadRotation;  // radians; NaN when not tracked
                if (!d.HasHeadPosition || double.IsNaN(r.X) || double.IsNaN(r.Y) || double.IsNaN(r.Z)) return;
                lock (Gate)
                {
                    pitch = r.X * 180.0 / Math.PI;
                    yaw = r.Y * 180.0 / Math.PI;
                    roll = r.Z * 180.0 / Math.PI;
                    headAt = Stopwatch.GetTimestamp();
                }
            };

            var presence = host.States.CreateUserPresenceObserver();
            presence.Changed += (s, e) =>
            {
                lock (Gate) { userPresent = e.IsValid && e.Value.ToString() == "Present"; }
            };

            using (var udp = new UdpClient())
            {
                udp.Connect("127.0.0.1", port);
                var buf = new byte[48];
                var lastParentCheck = Stopwatch.StartNew();
                var lastPrint = Stopwatch.StartNew();
                while (true)
                {
                    Thread.Sleep(16);
                    if (parent != null && lastParentCheck.ElapsedMilliseconds >= 1000)
                    {
                        lastParentCheck.Restart();
                        parent.Refresh();
                        if (parent.HasExited) break;
                    }

                    uint flags = 0;
                    double gx, gy, yw, pt, rl;
                    long now = Stopwatch.GetTimestamp();
                    lock (Gate)
                    {
                        if (userPresent) flags |= FlagPresent;
                        if (gazeAt != 0 && Ms(now - gazeAt) < FreshMs) flags |= FlagGaze;
                        if (headAt != 0 && Ms(now - headAt) < FreshMs) flags |= FlagHead;
                        gx = gazeX; gy = gazeY; yw = yaw; pt = pitch; rl = roll;
                    }
                    Write(buf, 0, Magic);
                    Write(buf, 4, flags);
                    Write(buf, 8, gx);
                    Write(buf, 16, gy);
                    Write(buf, 24, yw);
                    Write(buf, 32, pt);
                    Write(buf, 40, rl);
                    try { udp.Send(buf, buf.Length); }
                    catch (SocketException) { /* nobody listening yet */ }

                    if (print && lastPrint.ElapsedMilliseconds >= 250)
                    {
                        lastPrint.Restart();
                        Console.WriteLine($"present={(flags & FlagPresent) != 0} gaze={(flags & FlagGaze) != 0} ({gx:F0},{gy:F0}) " +
                                          $"head={(flags & FlagHead) != 0} yaw={yw:F1} pitch={pt:F1} roll={rl:F1}");
                    }
                }
            }
            host.DisableConnection();
            return 0;
        }

        static double Ms(long ticks) => ticks * 1000.0 / Stopwatch.Frequency;

        static void Write(byte[] b, int at, uint v) => BitConverter.GetBytes(v).CopyTo(b, at);
        static void Write(byte[] b, int at, double v) => BitConverter.GetBytes(v).CopyTo(b, at);
    }
}
