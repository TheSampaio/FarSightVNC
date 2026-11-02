<#
.SYNOPSIS
  Minimal fake RFB 3.8 server for viewer input/DPI tests (loopback only).

.DESCRIPTION
  Listens on 127.0.0.1 only (default port 5932), offers security type None and
  serves a Raw-encoded test pattern: a gradient with distinct corner colors
  (top-left red, top-right green, bottom-left blue, bottom-right white), solid
  8x8 corner squares and a 1 pixel grid every 50 pixels, so 1:1 sharpness and
  scaling can be inspected visually.

  Every client message is logged as one JSON object per line to -LogFile:
    {"event":"listening|connected|disconnected|stopped|error", ...}
    {"msg":"PointerEvent","x":..,"y":..,"mask":..}
    {"msg":"KeyEvent","down":true,"keysym":65}
    {"msg":"SetEncodings","encodings":[0,1,-223,...]}
    {"msg":"SetPixelFormat",...}, {"msg":"FramebufferUpdateRequest",...},
    {"msg":"ClientCutText","length":..,"text":".."}
  One client at a time; after a disconnect the server accepts a new client.
  The server stops after -DurationSeconds, when -StopFile appears, or when the
  process is killed. It never touches the registry and never leaves the
  loopback interface.

.EXAMPLE
  .\tools\fake-rfb-server.ps1 -Width 1080 -Height 1920 -LogFile $env:TEMP\rfb.jsonl -DurationSeconds 120
#>
param(
  [int]$Port = 5932,
  [int]$Width = 1920,
  [int]$Height = 1080,
  [string]$DesktopName = 'FarSightVNC fake RFB',
  [string]$LogFile = (Join-Path $env:TEMP 'farsightvnc-fake-rfb.jsonl'),
  [int]$DurationSeconds = 0,
  [string]$StopFile
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

public class FakeRfbServer
{
  int port, width, height;
  string name, stopFile;
  StreamWriter log;
  object logLock = new object();
  volatile bool stop;
  TcpClient active;
  // Client pixel format (default: 32 bpp, little endian, BGRX).
  int bpp = 32; bool bigEndian = false;
  int rMax = 255, gMax = 255, bMax = 255, rShift = 16, gShift = 8, bShift = 0;

  public FakeRfbServer(int port, int w, int h, string name, string logFile, string stopFile)
  {
    this.port = port; width = w; height = h; this.name = name; this.stopFile = stopFile;
    FileStream fs = new FileStream(logFile, FileMode.Create, FileAccess.Write, FileShare.ReadWrite);
    log = new StreamWriter(fs, new UTF8Encoding(false));
    log.AutoFlush = true;
  }

  static string Esc(string s)
  {
    StringBuilder sb = new StringBuilder();
    foreach (char c in s) {
      if (c == '"' || c == '\\') sb.Append('\\').Append(c);
      else if (c < 32) sb.Append("\\u" + ((int)c).ToString("x4"));
      else sb.Append(c);
    }
    return sb.ToString();
  }

  void Log(string json)
  {
    lock (logLock) { log.WriteLine(json); }
  }

  public void Run(int durationSeconds)
  {
    DateTime end = durationSeconds > 0 ? DateTime.UtcNow.AddSeconds(durationSeconds) : DateTime.MaxValue;
    TcpListener listener = new TcpListener(IPAddress.Loopback, port);
    listener.Start();
    Log("{\"event\":\"listening\",\"port\":" + port + ",\"width\":" + width + ",\"height\":" + height + "}");
    Thread worker = null;
    try {
      while (!stop) {
        if (DateTime.UtcNow > end || (stopFile != null && File.Exists(stopFile))) break;
        if (listener.Pending()) {
          TcpClient c = listener.AcceptTcpClient();
          if (worker != null && worker.IsAlive) { c.Close(); continue; }
          active = c;
          worker = new Thread(delegate() { Serve(c); });
          worker.IsBackground = true;
          worker.Start();
        } else {
          Thread.Sleep(50);
        }
      }
    } finally {
      stop = true;
      TcpClient a = active;
      if (a != null) { try { a.Close(); } catch (Exception) { } }
      if (worker != null) worker.Join(2000);
      listener.Stop();
      Log("{\"event\":\"stopped\"}");
      log.Close();
    }
  }

  static void ReadFull(Stream s, byte[] buf, int n)
  {
    int got = 0;
    while (got < n) {
      int r = s.Read(buf, got, n - got);
      if (r <= 0) throw new EndOfStreamException();
      got += r;
    }
  }

  static byte[] Read(Stream s, int n) { byte[] b = new byte[n]; ReadFull(s, b, n); return b; }
  static int U16(byte[] b, int o) { return (b[o] << 8) | b[o + 1]; }
  static int S32(byte[] b, int o) { return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]; }
  static void P16(byte[] b, int o, int v) { b[o] = (byte)(v >> 8); b[o + 1] = (byte)v; }

  void Serve(TcpClient client)
  {
    try {
      client.NoDelay = true;
      NetworkStream s = client.GetStream();
      Log("{\"event\":\"connected\"}");
      bpp = 32; bigEndian = false; rMax = gMax = bMax = 255; rShift = 16; gShift = 8; bShift = 0;
      s.Write(Encoding.ASCII.GetBytes("RFB 003.008\n"), 0, 12);
      string ver = Encoding.ASCII.GetString(Read(s, 12));
      Log("{\"msg\":\"ProtocolVersion\",\"version\":\"" + Esc(ver.Trim()) + "\"}");
      s.Write(new byte[] { 1, 1 }, 0, 2);          // one security type: None
      Read(s, 1);                                   // client's choice
      s.Write(new byte[] { 0, 0, 0, 0 }, 0, 4);     // SecurityResult OK
      Read(s, 1);                                   // ClientInit (shared flag)
      byte[] nameBytes = Encoding.UTF8.GetBytes(name);
      byte[] init = new byte[24 + nameBytes.Length];
      P16(init, 0, width); P16(init, 2, height);
      init[4] = 32; init[5] = 24; init[6] = 0; init[7] = 1;
      P16(init, 8, 255); P16(init, 10, 255); P16(init, 12, 255);
      init[14] = 16; init[15] = 8; init[16] = 0;
      init[20] = (byte)(nameBytes.Length >> 24); init[21] = (byte)(nameBytes.Length >> 16);
      init[22] = (byte)(nameBytes.Length >> 8); init[23] = (byte)nameBytes.Length;
      Array.Copy(nameBytes, 0, init, 24, nameBytes.Length);
      s.Write(init, 0, init.Length);

      while (!stop) {
        int type = s.ReadByte();
        if (type < 0) break;
        if (type == 0) {
          byte[] b = Read(s, 19);
          bpp = b[3]; bigEndian = b[5] != 0;
          rMax = U16(b, 7); gMax = U16(b, 9); bMax = U16(b, 11);
          rShift = b[13]; gShift = b[14]; bShift = b[15];
          Log("{\"msg\":\"SetPixelFormat\",\"bpp\":" + bpp + ",\"bigEndian\":" + (bigEndian ? "true" : "false") +
              ",\"trueColor\":" + (b[6] != 0 ? "true" : "false") + "}");
        } else if (type == 2) {
          byte[] h = Read(s, 3);
          int n = U16(h, 1);
          byte[] e = Read(s, 4 * n);
          StringBuilder sb = new StringBuilder();
          for (int i = 0; i < n; i++) { if (i > 0) sb.Append(','); sb.Append(S32(e, 4 * i)); }
          Log("{\"msg\":\"SetEncodings\",\"encodings\":[" + sb + "]}");
        } else if (type == 3) {
          byte[] b = Read(s, 9);
          int x = U16(b, 1), y = U16(b, 3), w = U16(b, 5), h = U16(b, 7);
          Log("{\"msg\":\"FramebufferUpdateRequest\",\"incremental\":" + (b[0] != 0 ? "true" : "false") +
              ",\"x\":" + x + ",\"y\":" + y + ",\"w\":" + w + ",\"h\":" + h + "}");
          if (b[0] != 0) {
            Thread.Sleep(100);               // throttle idle incremental polling
            SendRect(s, 0, 0, 1, 1);         // tiny valid update keeps the viewer's loop alive
          } else {
            SendRect(s, x, y, w, h);
          }
        } else if (type == 4) {
          byte[] b = Read(s, 7);
          long key = ((long)b[3] << 24) | ((long)b[4] << 16) | ((long)b[5] << 8) | b[6];
          Log("{\"msg\":\"KeyEvent\",\"down\":" + (b[0] != 0 ? "true" : "false") + ",\"keysym\":" + key + "}");
        } else if (type == 5) {
          byte[] b = Read(s, 5);
          Log("{\"msg\":\"PointerEvent\",\"x\":" + U16(b, 1) + ",\"y\":" + U16(b, 3) + ",\"mask\":" + b[0] + "}");
        } else if (type == 6) {
          byte[] h = Read(s, 7);
          long len = ((long)h[3] << 24) | ((long)h[4] << 16) | ((long)h[5] << 8) | h[6];
          byte[] t = Read(s, (int)Math.Min(len, 1 << 20));
          Log("{\"msg\":\"ClientCutText\",\"length\":" + len + ",\"text\":\"" + Esc(Encoding.GetEncoding(28591).GetString(t)) + "\"}");
        } else {
          Log("{\"event\":\"error\",\"detail\":\"unknown client message type " + type + "\"}");
          break;
        }
      }
    } catch (Exception ex) {
      if (!stop) Log("{\"event\":\"error\",\"detail\":\"" + Esc(ex.GetType().Name + ": " + ex.Message) + "\"}");
    } finally {
      try { client.Close(); } catch (Exception) { }
      Log("{\"event\":\"disconnected\"}");
    }
  }

  uint Pixel(int x, int y)
  {
    double fx = width > 1 ? (double)x / (width - 1) : 0, fy = height > 1 ? (double)y / (height - 1) : 0;
    double r = (1 - fx) * (1 - fy), g = fx * (1 - fy), b = (1 - fx) * fy, w = fx * fy;
    double R = 255 * (r + w) * 0.8 + 25, G = 255 * (g + w) * 0.8 + 25, B = 255 * (b + w) * 0.8 + 25;
    int cx = Math.Min(x, width - 1 - x), cy = Math.Min(y, height - 1 - y);
    if (cx < 8 && cy < 8) {
      bool right = x >= width / 2, bottom = y >= height / 2;
      if (right && bottom) { R = G = B = 255; }
      else if (right) { R = 0; G = 255; B = 0; }
      else if (bottom) { R = 0; G = 0; B = 255; }
      else { R = 255; G = 0; B = 0; }
    }
    else if (x % 50 == 0 || y % 50 == 0) { R = G = B = 0; }
    return (uint)(Math.Min(255, R) / 255.0 * rMax + 0.5) << rShift
         | (uint)(Math.Min(255, G) / 255.0 * gMax + 0.5) << gShift
         | (uint)(Math.Min(255, B) / 255.0 * bMax + 0.5) << bShift;
  }

  void SendRect(Stream s, int x, int y, int w, int h)
  {
    if (x >= width || y >= height) { x = 0; y = 0; w = 1; h = 1; }
    w = Math.Min(w, width - x); h = Math.Min(h, height - y);
    if (w < 1 || h < 1) { x = 0; y = 0; w = 1; h = 1; }
    int bytes = bpp / 8;
    byte[] buf = new byte[16 + w * h * bytes];
    buf[0] = 0; buf[2] = 0; buf[3] = 1;
    P16(buf, 4, x); P16(buf, 6, y); P16(buf, 8, w); P16(buf, 10, h);
    int o = 16;
    for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
        uint p = Pixel(x + i, y + j);
        for (int k = 0; k < bytes; k++) {
          int sh = bigEndian ? 8 * (bytes - 1 - k) : 8 * k;
          buf[o++] = (byte)(p >> sh);
        }
      }
    }
    s.Write(buf, 0, buf.Length);
  }
}
'@

$server = New-Object FakeRfbServer($Port, $Width, $Height, $DesktopName, $LogFile, $StopFile)
$server.Run($DurationSeconds)
