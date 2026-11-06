using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace Vmex.Core;

internal sealed class FileLog : IDisposable
{
    internal enum Level { Debug, Info, Warn, Error }
    private readonly string directory, stem;
    private static readonly int ProcessId = Process.GetCurrentProcess().Id;
    private readonly long partBytes, retainedBytes;
    private readonly TimeSpan retention;
    private readonly BlockingCollection<string> entries;
    private readonly object criticalGate = new();
    private readonly Task writer;
    private long dropped;
    private int stopped, failed;
    private readonly EventHandler exit;

    internal FileLog(string directory, string role, int capacity = 4096, long partBytes = 5L * 1024 * 1024,
        long retainedBytes = 100L * 1024 * 1024, TimeSpan? retention = null)
    {
        this.directory = directory;
        this.partBytes = partBytes;
        this.retainedBytes = retainedBytes;
        this.retention = retention ?? TimeSpan.FromDays(30);
        stem = role + "-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + "-" +
            ProcessId + "-" + Guid.NewGuid().ToString("N").Substring(0, 6);
        entries = new BlockingCollection<string>(Math.Max(1, capacity));
        writer = Task.Factory.StartNew(WriteLoop, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
        exit = (_, _) => Dispose();
        AppDomain.CurrentDomain.ProcessExit += exit;
    }

    internal static string Redact(string value)
    {
        string scrubbed = Regex.Replace(value, @"(?i)(githubToken[""']?\s*[:=]\s*[""']?)([^\s""']+)", "$1[redacted]");
        return Regex.Replace(scrubbed, @"\b(?:ghp_|github_pat_)[A-Za-z0-9_]+", "[redacted]");
    }

    internal void Write(string tag, string message, Exception? error = null, string? operationId = null,
        Level level = Level.Info, [CallerMemberName] string caller = "", [CallerFilePath] string file = "", [CallerLineNumber] int line = 0)
    {
        if (Volatile.Read(ref stopped) != 0) return;
        string text = Redact($"[{DateTime.UtcNow:yyyy-MM-dd HH:mm:ss.fff}Z {tag}/{level.ToString().ToUpperInvariant()}] " +
            $"{Path.GetFileName(file)}:{line}.{caller} pid={ProcessId} tid={Thread.CurrentThread.ManagedThreadId}" +
            (operationId == null ? "" : " op=" + operationId) + " | " + message + (error == null ? "" : " Exception=" + error));
        if (Volatile.Read(ref failed) == 0)
            try { if (entries.TryAdd(text)) return; } catch (InvalidOperationException) { }
        if (level >= Level.Warn) Critical(text);
        else Interlocked.Increment(ref dropped);
    }

    private void Critical(string line)
    {
        try
        {
            lock (criticalGate)
            {
                Directory.CreateDirectory(directory);
                File.AppendAllText(Path.Combine(directory, stem + "-critical.log"), line + Environment.NewLine, new UTF8Encoding(false));
            }
        }
        catch (Exception error) { Failure(error); }
    }

    private static void Failure(Exception error)
    {
        try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "volumemixerextender-log-error.txt"), Redact(error.ToString()) + Environment.NewLine); }
        catch { }
    }

    private static bool Managed(string name) => new[] { "app-", "manager-", "tap-", "launcher-", "recording-guard-", "updater-" }
        .Any(prefix => name.StartsWith(prefix, StringComparison.Ordinal));

    private void Retain()
    {
        long bytes = 0;
        foreach (var file in new DirectoryInfo(directory).GetFiles("*.log").Where(file => Managed(file.Name)).OrderByDescending(file => file.LastWriteTimeUtc))
        {
            bytes += file.Length;
            if (bytes > retainedBytes || DateTime.UtcNow - file.LastWriteTimeUtc > retention)
                try { file.Delete(); } catch { }
        }
    }

    private StreamWriter Open(int part) => new(new FileStream(Path.Combine(directory, stem + "-" + part + ".log"),
        FileMode.CreateNew, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete), new UTF8Encoding(false), 16384);

    private void WriteLoop()
    {
        StreamWriter? output = null;
        try
        {
            Directory.CreateDirectory(directory);
            Retain();
            int part = 0;
            output = Open(part);
            while (entries.TryTake(out string line, Timeout.Infinite))
            {
                output.WriteLine(line);
                int batch = 1;
                while (batch++ < 256 && entries.TryTake(out line)) output.WriteLine(line);
                long lost = Interlocked.Exchange(ref dropped, 0);
                if (lost > 0) output.WriteLine($"[{DateTime.UtcNow:O} Log/WARN] | Dropped low-priority log entries count={lost}");
                output.Flush();
                if (output.BaseStream.Length >= partBytes) { output.Dispose(); output = Open(++part); }
            }
            long finalLost = Interlocked.Exchange(ref dropped, 0);
            if (finalLost > 0) output.WriteLine($"[{DateTime.UtcNow:O} Log/WARN] | Dropped low-priority log entries count={finalLost}");
        }
        catch (Exception error)
        {
            Interlocked.Exchange(ref failed, 1);
            Failure(error);
            while (entries.TryTake(out string line)) Critical(line);
        }
        finally { try { output?.Dispose(); } catch (Exception error) { Failure(error); } }
    }

    internal sealed class OperationTimer : IDisposable
    {
        private readonly FileLog log;
        private readonly string tag, name, id;
        private readonly Stopwatch timer = Stopwatch.StartNew();
        internal OperationTimer(FileLog log, string tag, string name)
        {
            this.log = log; this.tag = tag; this.name = name; id = Guid.NewGuid().ToString("N");
            log.Write(tag, "OperationStart " + name, operationId: id);
        }
        public void Dispose() => log.Write(tag, $"OperationEnd {name} duration={timer.ElapsedMilliseconds}ms", operationId: id);
    }

    internal OperationTimer StartOperation(string tag, string name) => new(this, tag, name);

    public void Dispose()
    {
        if (Interlocked.Exchange(ref stopped, 1) != 0) return;
        entries.CompleteAdding();
        try { if (!writer.Wait(TimeSpan.FromSeconds(5))) Failure(new TimeoutException("Log shutdown drain timed out")); }
        catch (Exception error) { Failure(error); }
        AppDomain.CurrentDomain.ProcessExit -= exit;
    }
}
