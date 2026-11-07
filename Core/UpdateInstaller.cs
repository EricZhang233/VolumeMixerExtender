using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace Vmex.Core;

public static class UpdateTransaction
{
    public const string ManifestName = ".vmex-update-manifest.json";
    private const string BackupName = ".vmex-update-backup";
    private static readonly string[] RetiredFiles = {
        "vmex_cli.exe", "Driver/VBCABLE/vbMmeCable64_win10.inf", "Driver/VBCABLE/vbaudio_cable64_win10.cat",
        "Driver/VBCABLE/vbaudio_cable64_win10.sys", "Driver/VBCABLE/vbaudio_cable64arm_win10.sys",
        "Driver/VBCABLE/VBCABLE_Setup_x64.exe", "Driver/VBCABLE/VBCABLE_ControlPanel.exe",
        "Driver/VBCABLE/pin_in.ico", "Driver/VBCABLE/pin_out.ico", "Driver/VBCABLE/readme.txt"
    };
    public static readonly string[] RequiredFiles = {
        "vmex.exe", "vmex_core.dll", "vmex_manager.exe",
        "vmex_launcher.dll", "vmex_tap.dll", "xamldiagnostics.dll"
    };

    public sealed class Journal
    {
        public List<string> BackedUp { get; set; } = new List<string>();
        public List<string> Installed { get; set; } = new List<string>();
        public bool ActivationStarted { get; set; }
        public bool Committed { get; set; }
    }

    public static string Contained(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.Contains(':'))
            throw new InvalidDataException("UPDATE_PATH");
        var parts = relative.Replace('\\', '/').Split('/');
        foreach (string part in parts)
        {
            string stem = part.Split('.')[0];
            if (part.Length == 0 || part == "." || part == ".." || part.EndsWith(".") || part.EndsWith(" ") ||
                part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                new[] { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" }.Contains(stem, StringComparer.OrdinalIgnoreCase))
                throw new InvalidDataException("UPDATE_PATH");
        }
        string canonical = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string path = Path.GetFullPath(Path.Combine(canonical, Path.Combine(parts)));
        if (!path.StartsWith(canonical, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("UPDATE_PATH");
        for (string? ancestor = path; ancestor != null; ancestor = Path.GetDirectoryName(ancestor))
        {
            if ((File.Exists(ancestor) || Directory.Exists(ancestor)) &&
                (File.GetAttributes(ancestor) & FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("UPDATE_REPARSE");
        }
        return path;
    }

    private static bool Owned(string relative)
    {
        string path = relative.Replace('\\', '/');
        return RequiredFiles.Contains(path, StringComparer.OrdinalIgnoreCase) ||
            path.StartsWith("Driver/VBCABLE/", StringComparison.OrdinalIgnoreCase);
    }

    public static bool Verify(Stream package, string digest)
    {
        if (digest == null || digest.Length != 71 || !digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase)) return false;
        byte[] expected = new byte[32];
        for (int i = 0; i < expected.Length; ++i)
            if (!byte.TryParse(digest.Substring(7 + 2 * i, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out expected[i])) return false;
        using var algorithm = SHA256.Create();
        byte[] actual = algorithm.ComputeHash(package);
        int difference = 0;
        for (int i = 0; i < actual.Length; ++i) difference |= actual[i] ^ expected[i];
        return difference == 0;
    }

    public static void ValidateBinary(string path, Version version)
    {
        using (var reader = new BinaryReader(File.OpenRead(path)))
        {
            if (reader.ReadUInt16() != 0x5A4D) throw new InvalidDataException("UPDATE_PE");
            reader.BaseStream.Position = 0x3C;
            uint offset = reader.ReadUInt32();
            if (offset > reader.BaseStream.Length - 6) throw new InvalidDataException("UPDATE_PE");
            reader.BaseStream.Position = offset;
            if (reader.ReadUInt32() != 0x4550 || reader.ReadUInt16() != 0x8664) throw new InvalidDataException("UPDATE_ARCHITECTURE");
        }
        if (Path.GetFileName(path).Equals("xamldiagnostics.dll", StringComparison.OrdinalIgnoreCase)) return;
        var file = FileVersionInfo.GetVersionInfo(path);
        if (new Version(file.FileMajorPart, file.FileMinorPart, file.FileBuildPart, file.FilePrivatePart) != Normalize(version))
            throw new InvalidDataException("UPDATE_VERSION");
    }

    internal static Version Normalize(Version version) => new Version(version.Major, version.Minor,
        Math.Max(0, version.Build), Math.Max(0, version.Revision));

    public static FileStream AcquireLease(string directory) => new FileStream(
        Contained(directory, ".vmex-update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);

    public static void InstallVerified(Stream package, string digest, string directory, Version version,
        Action stop, Action restart, Action<string>? checkpoint = null, Action<string, Version>? validate = null,
        Action? stopCandidate = null, FileStream? heldLease = null)
    {
        string root = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar);
        using var lease = heldLease == null ? AcquireLease(root) : null;
        RecoverPending(root, stopCandidate, checkpoint);
        package.Position = 0;
        if (!Verify(package, digest)) throw new InvalidDataException("UPDATE_HASH_MISMATCH");
        package.Position = 0;
        string staging = Contained(root, ".vmex-update-stage-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        bool stopped = false;
        try
        {
            using (var archive = new ZipArchive(package, ZipArchiveMode.Read, true))
            {
                if (archive.Entries.Count > 4096) throw new InvalidDataException("UPDATE_ENTRY_COUNT");
                var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                long expanded = 0;
                foreach (var entry in archive.Entries)
                {
                    string relative = entry.FullName.Replace('\\', '/');
                    bool folder = relative.EndsWith("/", StringComparison.Ordinal);
                    relative = folder ? relative.Substring(0, relative.Length - 1) : relative;
                    string target = Contained(staging, relative);
                    if (!names.Add(relative) || (entry.ExternalAttributes & 0x400) != 0 ||
                        ((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000) throw new InvalidDataException("UPDATE_ENTRY");
                    if (folder)
                    {
                        if (!relative.Equals("Driver", StringComparison.OrdinalIgnoreCase) &&
                            !relative.Equals("Driver/VBCABLE", StringComparison.OrdinalIgnoreCase) &&
                            !relative.StartsWith("Driver/VBCABLE/", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("UPDATE_DIRECTORY");
                        Directory.CreateDirectory(target);
                        continue;
                    }
                    if (!Owned(relative)) throw new InvalidDataException("UPDATE_UNEXPECTED_FILE");
                    expanded = checked(expanded + entry.Length);
                    if (expanded > 2L * 1024 * 1024 * 1024) throw new InvalidDataException("UPDATE_EXPANDED_SIZE");
                    Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    using var input = entry.Open();
                    using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None);
                    var buffer = new byte[81920];
                    long remaining = entry.Length;
                    for (int read; (read = input.Read(buffer, 0, buffer.Length)) > 0;)
                    {
                        remaining -= read;
                        if (remaining < 0) throw new InvalidDataException("UPDATE_ENTRY_SIZE");
                        output.Write(buffer, 0, read);
                    }
                    if (remaining != 0) throw new InvalidDataException("UPDATE_ENTRY_SIZE");
                    output.Flush(true);
                }
                foreach (string name in RequiredFiles)
                {
                    string path = Contained(staging, name);
                    if (!File.Exists(path)) throw new InvalidDataException("UPDATE_MISSING_FILE");
                    (validate ?? ValidateBinary)(path, version);
                }
            }
            checkpoint?.Invoke("verified");
            stop();
            stopped = true;
            Apply(root, staging, restart, stopCandidate, checkpoint);
        }
        catch
        {
            if (stopped && !NeedsRecovery(root)) restart();
            throw;
        }
        finally
        {
            CleanDirectory(staging, checkpoint);
        }
    }

    private static void Apply(string root, string staging, Action restart, Action? stopCandidate, Action<string>? checkpoint)
    {
        string backup = Contained(root, BackupName);
        string[] incoming = Directory.GetFiles(staging, "*", SearchOption.AllDirectories)
            .Select(path => path.Substring(staging.Length + 1)).ToArray();
        string manifest = Contained(root, ManifestName);
        string[] previous = File.Exists(manifest)
            ? new JavaScriptSerializer().Deserialize<string[]>(File.ReadAllText(manifest)) ?? throw new InvalidDataException("UPDATE_MANIFEST")
            : RequiredFiles;
        previous = previous.Concat(RetiredFiles).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        foreach (string name in previous)
        {
            Contained(root, name);
            if (!Owned(name) && !RetiredFiles.Contains(name, StringComparer.OrdinalIgnoreCase)) throw new InvalidDataException("UPDATE_MANIFEST");
        }
        File.WriteAllText(Path.Combine(staging, ManifestName), new JavaScriptSerializer().Serialize(incoming));
        var journal = new Journal();
        Directory.CreateDirectory(backup);
        Save(backup, journal);
        try
        {
            foreach (string relative in previous.Concat(incoming).Append(ManifestName).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                string original = Contained(root, relative);
                if (!File.Exists(original)) continue;
                string destination = Contained(Path.Combine(backup, "files"), relative);
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                journal.BackedUp.Add(relative); Save(backup, journal);
                checkpoint?.Invoke("backup:" + relative);
                File.Move(original, destination);
            }
            foreach (string source in Directory.GetFiles(staging, "*", SearchOption.AllDirectories))
            {
                string relative = source.Substring(staging.Length + 1);
                string destination = Contained(root, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                journal.Installed.Add(relative); Save(backup, journal);
                checkpoint?.Invoke("install:" + relative);
                File.Move(source, destination);
            }
            journal.ActivationStarted = true; Save(backup, journal);
            checkpoint?.Invoke("activate");
            restart();
            checkpoint?.Invoke("commit");
            journal.Committed = true; Save(backup, journal);
        }
        catch
        {
            RecoverPending(root, stopCandidate, checkpoint);
            throw;
        }
        try { RetireBackup(root, backup, checkpoint); }
        catch (IOException) { checkpoint?.Invoke("cleanup-pending"); }
        catch (UnauthorizedAccessException) { checkpoint?.Invoke("cleanup-pending"); }
    }

    internal static bool NeedsRecovery(string root)
    {
        string backup = Contained(root, BackupName);
        return Directory.Exists(backup) && ReadJournal(backup)?.Committed != true;
    }

    private static Journal? ReadJournal(string backup)
    {
        string path = Contained(backup, "journal.json");
        if (!File.Exists(path))
        {
            if (Directory.Exists(Contained(backup, "files"))) throw new InvalidDataException("UPDATE_JOURNAL");
            return null;
        }
        return new JavaScriptSerializer().Deserialize<Journal>(File.ReadAllText(path))
            ?? throw new InvalidDataException("UPDATE_JOURNAL");
    }

    internal static bool RecoverPending(string root, Action? stopCandidate, Action<string>? checkpoint = null)
    {
        string backup = Contained(root, BackupName);
        if (!Directory.Exists(backup)) return false;
        var journal = ReadJournal(backup);
        if (journal == null || journal.Committed)
        {
            RetireBackup(root, backup, checkpoint);
            return false;
        }
        if (journal.ActivationStarted)
        {
            stopCandidate?.Invoke();
            journal.ActivationStarted = false;
            Save(backup, journal);
        }
        foreach (string relative in journal.Installed.AsEnumerable().Reverse())
        {
            if (!Owned(relative) && !RetiredFiles.Contains(relative, StringComparer.OrdinalIgnoreCase) && relative != ManifestName) throw new InvalidDataException("UPDATE_JOURNAL");
            checkpoint?.Invoke("rollback-delete:" + relative);
            File.Delete(Contained(root, relative));
        }
        foreach (string relative in journal.BackedUp)
        {
            if (!Owned(relative) && !RetiredFiles.Contains(relative, StringComparer.OrdinalIgnoreCase) && relative != ManifestName) throw new InvalidDataException("UPDATE_JOURNAL");
            string source = Contained(Path.Combine(backup, "files"), relative);
            if (!File.Exists(source)) continue;
            checkpoint?.Invoke("rollback-restore:" + relative);
            string destination = Contained(root, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.Copy(source, destination, true);
        }
        journal.Committed = true;
        Save(backup, journal);
        RetireBackup(root, backup, checkpoint);
        return true;
    }

    private static void RetireBackup(string root, string backup, Action<string>? checkpoint)
    {
        string retired = Contained(root, BackupName + "-done-" + Guid.NewGuid().ToString("N"));
        Directory.Move(backup, retired);
        CleanDirectory(retired, checkpoint);
    }

    private static void CleanDirectory(string directory, Action<string>? checkpoint)
    {
        try { if (Directory.Exists(directory)) Directory.Delete(directory, true); }
        catch (IOException) { checkpoint?.Invoke("cleanup-pending"); }
        catch (UnauthorizedAccessException) { checkpoint?.Invoke("cleanup-pending"); }
    }

    private static void Save(string backup, Journal journal)
    {
        string path = Contained(backup, "journal.json");
        string temporary = path + ".tmp";
        using (var stream = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            byte[] bytes = Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(journal));
            stream.Write(bytes, 0, bytes.Length);
            stream.Flush(true);
        }
        if (File.Exists(path)) File.Replace(temporary, path, null);
        else File.Move(temporary, path);
    }
}

public static class UpdateRuntime
{
    public sealed class Request
    {
        public string directory { get; set; } = "";
        public string version { get; set; } = "";
        public string tag { get; set; } = "";
        public string url { get; set; } = "";
        public string digest { get; set; } = "";
        public long size { get; set; }
        public int managerProcessId { get; set; }
    }

    public sealed class RecoveryRequest
    {
        public string Directory { get; set; } = "";
        public bool ReopenManager { get; set; }
    }

    private static readonly string Cache = Path.Combine(Path.GetTempPath(), "eric", "VolumeMixerExtender");
    private static readonly string RecoveryDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "EricSoft", "VolumeMixerExtender", "Recovery");
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private static readonly Lazy<FileLog> Logger = new(() => new FileLog(Path.Combine(Cache, "log"), "updater"));
    private static void Log(string message, Exception? error = null, FileLog.Level level = FileLog.Level.Info,
        [CallerMemberName] string caller = "", [CallerFilePath] string file = "", [CallerLineNumber] int line = 0)
        => Logger.Value.Write("update", message, error, level: level, caller: caller, file: file, line: line);

    public static int Run(string[] args)
    {
        if (args.Length is 2 or 4 && args[0] == "--recover")
            return RunRecovery(args);
        if (args.Length != 1) return 1;
        string requestPath;
        try
        {
            requestPath = Path.GetFullPath(args[0]);
            if (!string.Equals(Path.GetDirectoryName(requestPath), Path.Combine(Cache, "updater"), StringComparison.OrdinalIgnoreCase) ||
                !Path.GetExtension(requestPath).Equals(".json", StringComparison.OrdinalIgnoreCase) ||
                !Guid.TryParse(Path.GetFileNameWithoutExtension(requestPath), out _)) return 1;
        }
        catch { return 1; }
        string? packagePath = null;
        Process? manager = null;
        Process? candidate = null;
        bool succeeded = false;
        string? installDirectory = null;
        try
        {
            var request = new JavaScriptSerializer().Deserialize<Request>(File.ReadAllText(requestPath)) ?? throw new InvalidDataException("UPDATE_REQUEST");
            string root = Path.GetFullPath(request.directory).TrimEnd(Path.DirectorySeparatorChar);
            if (root.Length <= Path.GetPathRoot(root).Length || !Directory.Exists(root) ||
                !Version.TryParse(request.version, out Version target) || target.Build < 0 ||
                (request.tag != "v" + request.version && request.tag != "V" + request.version && request.tag != request.version) ||
                request.url != "https://github.com/EricZhang233/VolumeMixerExtender/releases/download/" + request.tag +
                    "/VolumeMixerExtender_" + request.version + ".zip" || request.size <= 0 || request.size > 1024L * 1024 * 1024 ||
                request.managerProcessId < 0)
                throw new InvalidDataException("UPDATE_REQUEST");
            using var mutex = new Mutex(false, "Local\\VmExt.Update.S" + Process.GetCurrentProcess().SessionId);
            bool acquired;
            try { acquired = mutex.WaitOne(TimeSpan.FromSeconds(10)); }
            catch (AbandonedMutexException) { acquired = true; }
            if (!acquired) throw new IOException("UPDATE_IN_PROGRESS");
            try
            {
                installDirectory = root;
                string physical = ResolveRoot(root);
                using var lease = UpdateTransaction.AcquireLease(physical);
                if (UpdateTransaction.RecoverPending(physical, () => StopCandidate(root, null)))
                    EnsureHost(root);
                var current = FileVersionInfo.GetVersionInfo(UpdateTransaction.Contained(physical, "vmex_core.dll"));
                if (UpdateTransaction.Normalize(target) <= new Version(current.FileMajorPart, current.FileMinorPart, current.FileBuildPart, current.FilePrivatePart))
                    throw new InvalidDataException("UPDATE_NOT_NEWER");
                manager = OpenManager(request.managerProcessId, root);
                using var operation = Logger.Value.StartOperation("update", "Install " + target);
                Log("Downloading update version=" + target);
                using var deadline = new CancellationTokenSource(TimeSpan.FromMinutes(35));
                packagePath = Path.Combine(Path.GetDirectoryName(requestPath)!, "package-" + Guid.NewGuid().ToString("N") + ".zip");
                Download(request, packagePath, deadline.Token).GetAwaiter().GetResult();
                using var package = new FileStream(packagePath, FileMode.Open, FileAccess.Read, FileShare.Read);
                string? recovery = null;
                UpdateTransaction.InstallVerified(package, request.digest, physical, target,
                    () =>
                    {
                        WaitForClients(root, deadline.Token, manager);
                        deadline.Token.ThrowIfCancellationRequested();
                        recovery = ArmRecovery(root, physical, manager != null);
                        Log("Package verified; stopping host and injection");
                        StopCore(root, forUpdate: true);
                    },
                    () =>
                    {
                        Log("Restarting host and injection");
                        candidate?.Dispose();
                        candidate = StartHost(root);
                        WaitForHealthyHost(root, candidate);
                    },
                    step => Log("Update transaction step=" + step),
                    stopCandidate: () => StopCandidate(root, candidate), heldLease: lease);
                succeeded = true;
                if (recovery != null)
                {
                    try { DisarmRecovery(recovery); }
                    catch (Exception error) { Log("Recovery registration cleanup deferred", error, FileLog.Level.Warn); }
                }
                Log("Silent update completed version=" + target);
                return 0;
            }
            finally { mutex.ReleaseMutex(); }
        }
        catch (Exception error) { Log("Silent update failed", error, FileLog.Level.Error); return 3; }
        finally
        {
            if (manager != null)
            {
                try
                {
                    if (manager.HasExited && installDirectory != null)
                        ReopenManager(installDirectory, failed: !succeeded);
                }
                catch (Exception error) { Log("Manager reopen failed", error, FileLog.Level.Warn); }
                finally { manager.Dispose(); }
            }
            candidate?.Dispose();
            try { if (packagePath != null) File.Delete(packagePath); } catch { }
            try { File.Delete(requestPath); } catch { }
            if (Logger.IsValueCreated) Logger.Value.Dispose();
        }
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security,
        uint disposition, uint flags, IntPtr template);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandle(SafeFileHandle handle, StringBuilder path, uint capacity, uint flags);

    internal static string ResolveRoot(string directory)
    {
        using var handle = CreateFile(directory, 0, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero);
        if (handle.IsInvalid) throw new IOException("UPDATE_ROOT_OPEN:" + Marshal.GetLastWin32Error());
        var path = new StringBuilder(32768);
        uint length = GetFinalPathNameByHandle(handle, path, (uint)path.Capacity, 1);
        if (length == 0) length = GetFinalPathNameByHandle(handle, path, (uint)path.Capacity, 0);
        if (length == 0 || length >= path.Capacity) throw new IOException("UPDATE_ROOT_PATH:" + Marshal.GetLastWin32Error());
        return path.ToString().TrimEnd(Path.DirectorySeparatorChar);
    }

    private static string RecoveryName(string id) => "VolumeMixerExtenderRecovery-" + id;

    private static string ArmRecovery(string root, string physical, bool reopenManager)
    {
        Directory.CreateDirectory(RecoveryDirectory);
        using var hash = SHA256.Create();
        string identity = BitConverter.ToString(hash.ComputeHash(Encoding.Unicode.GetBytes(physical.ToUpperInvariant()))).Replace("-", "");
        string executable = Path.Combine(RecoveryDirectory, identity + ".exe");
        using var updater = Process.GetCurrentProcess();
        File.Copy(updater.MainModule!.FileName, executable, true);
        using (var file = new FileStream(executable, FileMode.Open, FileAccess.ReadWrite, FileShare.Read)) file.Flush(true);
        string id = Guid.NewGuid().ToString("N");
        string request = Path.Combine(RecoveryDirectory, id + ".json");
        string args = "--recover " + id;
        string command = "\"" + executable + "\" " + args;
        if (command.Length >= 260) throw new IOException("UPDATE_RECOVERY_PATH");
        using (var stream = new FileStream(request, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
        {
            byte[] bytes = Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(
                new RecoveryRequest { Directory = root, ReopenManager = reopenManager }));
            stream.Write(bytes, 0, bytes.Length);
            stream.Flush(true);
        }
        try
        {
            using var key = Registry.CurrentUser.CreateSubKey(RunKey);
            key.SetValue(RecoveryName(id), command, RegistryValueKind.String);
            key.Flush();
            using var watchdog = Process.Start(new ProcessStartInfo(executable)
            {
                WorkingDirectory = RecoveryDirectory, UseShellExecute = false, CreateNoWindow = true,
                Arguments = args + " " + updater.Id + " " + updater.StartTime.ToUniversalTime().Ticks
            }) ?? throw new IOException("UPDATE_RECOVERY_START");
            return id;
        }
        catch
        {
            DisarmRecovery(id);
            throw;
        }
    }

    private static void DisarmRecovery(string id)
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, true);
        key?.DeleteValue(RecoveryName(id), false);
        key?.Flush();
        File.Delete(Path.Combine(RecoveryDirectory, id + ".json"));
    }

    private static int RunRecovery(string[] args)
    {
        if (!Guid.TryParseExact(args[1], "N", out _)) return 1;
        try
        {
            if (args.Length == 4)
            {
                if (!int.TryParse(args[2], out int pid) || !long.TryParse(args[3], out long started)) return 1;
                try
                {
                    using var owner = Process.GetProcessById(pid);
                    if (owner.StartTime.ToUniversalTime().Ticks == started) owner.WaitForExit();
                }
                catch (ArgumentException) { }
                catch (InvalidOperationException) { }
            }
            string request = Path.Combine(RecoveryDirectory, args[1] + ".json");
            if (!File.Exists(request)) return 0;
            var pending = new JavaScriptSerializer().Deserialize<RecoveryRequest>(File.ReadAllText(request))
                ?? throw new InvalidDataException("UPDATE_RECOVERY_REQUEST");
            string root = pending.Directory;
            string physical = ResolveRoot(root);
            using var lease = UpdateTransaction.AcquireLease(physical);
            if (!File.Exists(request)) return 0;
            bool recovered = UpdateTransaction.RecoverPending(physical, () => StopCandidate(root, null),
                step => Log("Update recovery step=" + step));
            EnsureHost(root);
            DisarmRecovery(args[1]);
            if (pending.ReopenManager)
            {
                using var manager = FindClient("vmex_manager", root);
                if (manager == null) ReopenManager(root, failed: true);
            }
            Log("Interrupted update recovery completed rolledBack=" + recovered);
            return 0;
        }
        catch (Exception error) { Log("Interrupted update recovery failed", error, FileLog.Level.Error); return 3; }
        finally { if (Logger.IsValueCreated) Logger.Value.Dispose(); }
    }

    private static async Task Download(Request request, string path, CancellationToken token)
    {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        using var handler = new HttpClientHandler { AllowAutoRedirect = false };
        using var client = new HttpClient(handler) { Timeout = Timeout.InfiniteTimeSpan };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("VolumeMixerExtender-Updater");
        using var total = CancellationTokenSource.CreateLinkedTokenSource(token);
        total.CancelAfter(TimeSpan.FromMinutes(30));
        using var idle = CancellationTokenSource.CreateLinkedTokenSource(total.Token);
        string url = request.url;
        for (int redirect = 0; redirect < 6; ++redirect)
        {
            var uri = new Uri(url);
            if (uri.Scheme != "https" || !new[] { "github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com" }
                .Contains(uri.Host, StringComparer.OrdinalIgnoreCase)) throw new InvalidDataException("UPDATE_DOWNLOAD_URL");
            idle.CancelAfter(TimeSpan.FromSeconds(60));
            using var response = await client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, idle.Token).ConfigureAwait(false);
            if ((int)response.StatusCode >= 300 && (int)response.StatusCode < 400)
            {
                url = new Uri(uri, response.Headers.Location ?? throw new IOException("UPDATE_REDIRECT")).AbsoluteUri;
                continue;
            }
            response.EnsureSuccessStatusCode();
            using var abort = idle.Token.Register(() => response.Dispose());
            if (response.Content.Headers.ContentLength is long length && length != request.size) throw new InvalidDataException("UPDATE_SIZE");
            using var body = await response.Content.ReadAsStreamAsync().ConfigureAwait(false);
            using var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 81920, true);
            byte[] buffer = new byte[81920];
            long received = 0;
            for (;;)
            {
                idle.CancelAfter(TimeSpan.FromSeconds(60));
                int count = await body.ReadAsync(buffer, 0, buffer.Length, idle.Token).ConfigureAwait(false);
                if (count == 0) break;
                received += count;
                if (received > request.size) throw new InvalidDataException("UPDATE_SIZE");
                await file.WriteAsync(buffer, 0, count, idle.Token).ConfigureAwait(false);
            }
            if (received != request.size) throw new InvalidDataException("UPDATE_SIZE");
            file.Flush(true);
            return;
        }
        throw new IOException("UPDATE_REDIRECT_LIMIT");
    }

    private static bool SameDirectory(Process process, string root)
    {
        try
        {
            string? directory = Path.GetDirectoryName(process.MainModule!.FileName);
            return directory != null && (string.Equals(directory, root, StringComparison.OrdinalIgnoreCase) ||
                string.Equals(ResolveRoot(directory), ResolveRoot(root), StringComparison.OrdinalIgnoreCase));
        }
        catch (InvalidOperationException) { return false; }
        catch (System.ComponentModel.Win32Exception) { throw new IOException("UPDATE_CLIENT_IDENTITY"); }
    }

    public static Process? OpenManager(int processId, string root)
    {
        if (processId == 0) return null;
        Process process;
        try { process = Process.GetProcessById(processId); }
        catch (ArgumentException) { return null; }
        try
        {
            _ = process.Handle;
            if (process.ProcessName != "vmex_manager" || process.SessionId != Process.GetCurrentProcess().SessionId ||
                !SameDirectory(process, root)) throw new IOException("UPDATE_MANAGER_IDENTITY");
            return process;
        }
        catch { process.Dispose(); throw; }
    }

    private static void WaitForClients(string root, CancellationToken token, Process? manager)
    {
        if (manager != null) Log("Package verified; closing initiating manager");
        for (;;)
        {
            token.ThrowIfCancellationRequested();
            if (manager != null && !manager.HasExited) manager.CloseMainWindow();
            bool busy = false;
            foreach (var process in Process.GetProcessesByName("vmex_manager"))
                using (process) busy |= SameDirectory(process, root);
            if (!busy) return;
            token.ThrowIfCancellationRequested();
            if (token.WaitHandle.WaitOne(250)) token.ThrowIfCancellationRequested();
        }
    }

    private static Process StartHost(string root) => Process.Start(new ProcessStartInfo(Path.Combine(root, "vmex.exe"))
    {
        WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true
    }) ?? throw new IOException("UPDATE_RESTART");

    private static void ReopenManager(string root, bool failed)
    {
        using var reopened = Process.Start(new ProcessStartInfo(Path.Combine(root, "vmex_manager.exe"))
        {
            WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
            Arguments = failed ? "--update-failed" : ""
        }) ?? throw new IOException("UPDATE_MANAGER_RESTART");
    }

    private static Process? FindClient(string name, string root)
    {
        using var current = Process.GetCurrentProcess();
        foreach (var process in Process.GetProcessesByName(name))
        {
            using (process)
                if (process.SessionId == current.SessionId && SameDirectory(process, root))
                    return Process.GetProcessById(process.Id);
        }
        return null;
    }

    private static void EnsureHost(string root)
    {
        using var active = FindClient("vmex", root);
        if (active != null) return;
        using var started = StartHost(root);
        WaitForHealthyHost(root, started);
    }

    private static void WaitForHealthyHost(string root, Process process)
    {
        WithCore(root, module =>
        {
            var query = Export<QueryCore>(module, "VmexManagerQuery");
            var state = new int[6];
            var deadline = Stopwatch.StartNew();
            do
            {
                if (process.HasExited) throw new IOException("UPDATE_HOST_EXITED:" + process.ExitCode);
                if (query(state, state.Length) == 0 && state[0] != 0 && state[1] != 0 &&
                    state[2] != 0 && state[5] == process.Id) return;
                Thread.Sleep(250);
            } while (deadline.Elapsed < TimeSpan.FromSeconds(35));
            throw new IOException("UPDATE_HOST_NOT_READY");
        });
        Log("Updated host and TAP are ready pid=" + process.Id);
    }

    private static void StopCandidate(string root, Process? candidate)
    {
        using var discovered = candidate == null ? FindClient("vmex", root) : null;
        Process? active = candidate ?? discovered;
        try { RunStop(root); }
        catch (Exception error)
        {
            Log("Graceful stop of update candidate failed", error, FileLog.Level.Warn);
            if (active != null && !active.HasExited)
            {
                active.Kill();
                if (!active.WaitForExit(10000)) throw new IOException("UPDATE_CANDIDATE_RUNNING");
            }
            try { RunStop(root); }
            catch (Exception cleanup) { Log("Candidate injection cleanup failed", cleanup, FileLog.Level.Warn); }
        }
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll")]
    private static extern bool FreeLibrary(IntPtr module);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private delegate int InitializeCore(StringBuilder error, int capacity);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private delegate int ExecuteCore(int action, int value, StringBuilder error, int capacity);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void ShutdownCore();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int QueryCore([Out, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] int[] values, int count);

    private static T Export<T>(IntPtr module, string name) where T : Delegate
    {
        IntPtr address = GetProcAddress(module, name);
        if (address == IntPtr.Zero) throw new IOException("UPDATE_CORE_EXPORT:" + name);
        return Marshal.GetDelegateForFunctionPointer<T>(address);
    }

    private static void RunStop(string root) => StopCore(root, forUpdate: false);

    private static void StopCore(string root, bool forUpdate)
    {
        WithCore(root, module =>
        {
            var execute = Export<ExecuteCore>(module, "VmexManagerExecute");
            var error = new StringBuilder(4096);
            if (execute(forUpdate ? 12 : 2, 0, error, error.Capacity) != 0)
                throw new IOException("UPDATE_STOP_FAILED:" + error);
        });
    }

    private static void WithCore(string root, Action<IntPtr> operation)
    {
        IntPtr module = LoadLibraryEx(Path.Combine(root, "vmex_core.dll"), IntPtr.Zero, 8);
        if (module == IntPtr.Zero) throw new IOException("UPDATE_CORE_LOAD:" + Marshal.GetLastWin32Error());
        try
        {
            var initialize = Export<InitializeCore>(module, "VmexManagerInitialize");
            var shutdown = Export<ShutdownCore>(module, "VmexManagerShutdown");
            try
            {
                var error = new StringBuilder(4096);
                if (initialize(error, error.Capacity) != 0) throw new IOException("UPDATE_CORE_INITIALIZE:" + error);
                operation(module);
            }
            finally { shutdown(); }
        }
        finally { FreeLibrary(module); }
    }
}
