using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Vmex.Core;

internal static class Program
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static MemoryStream Package(IEnumerable<string>? names = null)
    {
        var stream = new MemoryStream();
        using (var archive = new ZipArchive(stream, ZipArchiveMode.Create, true))
            foreach (string name in names ?? UpdateTransaction.RequiredFiles)
                using (var writer = new StreamWriter(archive.CreateEntry(name).Open())) writer.Write("new:" + name);
        stream.Position = 0;
        return stream;
    }

    private static string Digest(MemoryStream stream)
    {
        using var hash = SHA256.Create();
        return "sha256:" + BitConverter.ToString(hash.ComputeHash(stream.ToArray())).Replace("-", "").ToLowerInvariant();
    }

    private static void Rejected(Action action)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        catch (IOException) { return; }
        throw new Exception("An unsafe update was accepted");
    }

    private static void CheckLogging(string root)
    {
        string directory = Path.Combine(root, "logs");
        Directory.CreateDirectory(directory);
        string recent = Path.Combine(directory, "app-recent.log"), expired = Path.Combine(directory, "app-expired.log"),
            oversized = Path.Combine(directory, "app-over-budget.log"), unrelated = Path.Combine(directory, "unrelated.log");
        File.WriteAllText(recent, "recent"); File.SetLastWriteTimeUtc(recent, DateTime.UtcNow.AddHours(-1));
        File.WriteAllText(expired, "expired"); File.SetLastWriteTimeUtc(expired, DateTime.UtcNow.AddDays(-31));
        File.WriteAllText(oversized, new string('x', 2048)); File.SetLastWriteTimeUtc(oversized, DateTime.UtcNow.AddHours(-2));
        File.WriteAllText(unrelated, "keep"); File.SetLastWriteTimeUtc(unrelated, DateTime.UtcNow.AddDays(-31));
        Require(FileLog.Redact("GITHUBTOKEN='plain_secret' ghp_abcdef github_pat_abcdef") ==
            "GITHUBTOKEN='[redacted]' [redacted] [redacted]", "Updater credentials must be redacted without i18n");
        using (var log = new FileLog(directory, "updater", capacity: 1, retainedBytes: 1024))
        {
            log.Write("Stress", "rotation-probe|" + new string('r', 6 * 1024 * 1024) + "|end", level: FileLog.Level.Warn);
            Require(SpinWait.SpinUntil(() => Directory.GetFiles(directory, "updater-*-1.log").Length > 0, TimeSpan.FromSeconds(15)),
                "Updater primary logs must rotate at five megabytes");
            Parallel.For(0, 12000, new ParallelOptions { MaxDegreeOfParallelism = 8 }, i =>
                log.Write("Stress", $"critical-{i:D5}|" + new string('x', 1024) + "|end", level: FileLog.Level.Warn));
            Parallel.For(0, 20000, new ParallelOptions { MaxDegreeOfParallelism = 8 }, _ => log.Write("Stress", "low-priority"));
            log.Write("Stress", "githubToken=plain_secret ghp_abcdef", new IOException("github_pat_exception"), level: FileLog.Level.Error);
            using (log.StartOperation("Stress", "FixtureOperation")) { }
            log.Write("Stress", "flush-tail-marker", level: FileLog.Level.Error);
        }
        using (var log = new FileLog(directory, "updater"))
        {
            using (log.StartOperation("Stress", "CallerOperation")) { }
        }
        string[] files = Directory.GetFiles(directory, "updater-*.log");
        string contents = string.Join("\n", files.Select(File.ReadAllText));
        var numbers = Regex.Matches(contents, @"critical-(\d{5})\|").Cast<Match>().Select(match => match.Groups[1].Value).Distinct().Count();
        Require(numbers == 12000 && files.Any(file => file.EndsWith("-critical.log")), "Updater warnings must survive bounded-queue overflow");
        Require(contents.Split('\n').Where(line => line.Contains("critical-")).All(line => line.TrimEnd('\r').EndsWith("|end")),
            "Updater fallback writes must preserve complete lines");
        Require(contents.Contains("flush-tail-marker") && contents.Contains("Dropped low-priority log entries count="),
            "Updater shutdown must drain the final error and report low-priority losses");
        Require(contents.Contains("Program.cs:") && contents.Contains("duration=") && contents.Contains(" op=") &&
            contents.Contains("Exception=System.IO.IOException") && !contents.Contains("plain_secret") &&
            !contents.Contains("ghp_abcdef") && !contents.Contains("github_pat_exception"),
            "Updater logs must retain caller, timing and exception context with all secrets redacted");
        Require(!File.Exists(expired) && !File.Exists(oversized) && File.Exists(recent) && File.Exists(unrelated),
            "Updater retention must enforce age and size while preserving unrelated and recent logs");
        Require(files.All(file => !File.ReadAllBytes(file).Take(3).SequenceEqual(new byte[] { 0xEF, 0xBB, 0xBF })),
            "Updater logs must use UTF-8 without a BOM");
    }

    private static int Main(string[] args)
    {
        string root = Path.Combine(Path.GetTempPath(), "vmex-update-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var calls = new List<string>();
            Action stop = () => calls.Add("stop");
            Action restart = () => calls.Add("restart");
            Action<string, Version> fixture = (_, _) => { };
            var version = new Version(2, 0, 0, 0);
            Require(args.Length == 1, "The packaged binary directory must be provided by CTest");
            var binary = FileVersionInfo.GetVersionInfo(Path.Combine(args[0], "vmex_core.dll"));
            var actual = new Version(binary.FileMajorPart, binary.FileMinorPart, binary.FileBuildPart, binary.FilePrivatePart);
            foreach (string name in UpdateTransaction.RequiredFiles)
                UpdateTransaction.ValidateBinary(Path.Combine(args[0], name), actual);
            foreach (string name in UpdateTransaction.RequiredFiles) File.WriteAllText(Path.Combine(root, name), "old:" + name);
            File.WriteAllText(Path.Combine(root, "vmex.ini"), "keep configuration");
            Require(UpdateRuntime.OpenManager(0, root) == null, "Automatic updates must not target a manager window");
            Rejected(() => { using var unrelated = UpdateRuntime.OpenManager(Process.GetCurrentProcess().Id, root); });
            Require(UpdateRuntime.Run(new[] { Path.Combine(root, "vmex.ini") }) == 1 &&
                File.ReadAllText(Path.Combine(root, "vmex.ini")) == "keep configuration",
                "The updater must reject arbitrary request files without deleting them");
            using (var package = Package())
            {
                Require(UpdateTransaction.Verify(package, Digest(package)), "A valid GitHub SHA256 digest was rejected");
                package.Position = 0;
                Require(!UpdateTransaction.Verify(package, "sha256:" + new string('0', 64)), "Corruption must fail verification");
                Rejected(() => UpdateTransaction.InstallVerified(package, "sha256:" + new string('0', 64), root, version,
                    stop, restart, validate: fixture));
                Require(calls.Count == 0, "Verification failure must leave the host and injection running");
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart));
                Require(calls.Count == 0, "An invalid executable must be rejected before shutting down");
            }
            foreach (string unsafePath in new[] { "../vmex.exe", "/vmex.exe", "C:/vmex.exe", "Driver/VBCABLE/../../vmex.exe",
                "Driver/VBCABLE/file:stream", "Driver/VBCABLE/NUL", "Driver/VBCABLE/file.", ".vmex-update-backup/journal.json", "vmex.ini" })
            {
                using var package = Package(UpdateTransaction.RequiredFiles.Concat(new[] { unsafePath }));
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version,
                    stop, restart, validate: fixture));
                Require(calls.Count == 0, "Archive paths must be validated before stopping any process");
            }
            using (var package = Package(UpdateTransaction.RequiredFiles.Take(UpdateTransaction.RequiredFiles.Length - 1)))
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart, validate: fixture));
            using (var package = Package(UpdateTransaction.RequiredFiles.Concat(new[] { "VMEX.EXE" })))
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart, validate: fixture));
            Require(calls.Count == 0, "Incomplete or ambiguous packages must leave the host untouched");
            File.WriteAllText(Path.Combine(root, "vmex_cli.exe"), "retired executable");
            using (var package = Package())
            {
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart,
                    step => { if (step == "install:vmex_core.dll") throw new IOException("simulated disk failure"); }, fixture));
                Require(calls.SequenceEqual(new[] { "stop", "restart" }), "A failed replacement must restart the old host after rollback");
                foreach (string name in UpdateTransaction.RequiredFiles)
                    Require(File.ReadAllText(Path.Combine(root, name)) == "old:" + name, "Rollback did not restore every original binary");
                Require(File.ReadAllText(Path.Combine(root, "vmex_cli.exe")) == "retired executable",
                    "Rollback must restore retired files until replacement commits");
            }
            calls.Clear();
            using (var package = Package())
            {
                bool firstStart = true;
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, () =>
                {
                    calls.Add("restart");
                    if (!firstStart) return;
                    firstStart = false;
                    Require(UpdateTransaction.NeedsRecovery(root), "The old files must remain available during health validation");
                    throw new IOException("simulated injection readiness failure");
                }, validate: fixture, stopCandidate: () => calls.Add("stop-candidate")));
                Require(calls.SequenceEqual(new[] { "stop", "restart", "stop-candidate", "restart" }) &&
                    File.ReadAllText(Path.Combine(root, "vmex_core.dll")) == "old:vmex_core.dll",
                    "A candidate failing health validation must be stopped and the previous host restored");
            }
            var interrupted = Path.Combine(root, ".vmex-update-backup");
            Directory.CreateDirectory(Path.Combine(interrupted, "files"));
            File.Move(Path.Combine(root, "vmex_core.dll"), Path.Combine(interrupted, "files", "vmex_core.dll"));
            File.WriteAllText(Path.Combine(interrupted, "journal.json"), new JavaScriptSerializer().Serialize(
                new UpdateTransaction.Journal { BackedUp = new List<string> { "vmex_core.dll" } }));
            using (var lease = UpdateTransaction.AcquireLease(root))
                Require(UpdateTransaction.RecoverPending(root, null), "Interrupted installation must recover before reading the installed version");
            Require(File.ReadAllText(Path.Combine(UpdateRuntime.ResolveRoot(root), "vmex_core.dll")) == "old:vmex_core.dll",
                "The restored core must be accessible through its resolved volume path");
            calls.Clear();
            using (var package = Package(UpdateTransaction.RequiredFiles.Concat(new[] { "Driver/VBCABLE/old.inf" })))
                UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart, validate: fixture);
            Require(calls.SequenceEqual(new[] { "stop", "restart" }), "Successful installation must stop once and restart once");
            Require(!File.Exists(Path.Combine(root, "vmex_cli.exe")), "Successful upgrades must remove the retired command-line executable");
            calls.Clear();
            using (var package = Package())
                UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart, validate: fixture);
            Require(!File.Exists(Path.Combine(root, "Driver", "VBCABLE", "old.inf")), "Obsolete owned files must be removed");
            Require(File.ReadAllText(Path.Combine(root, "vmex.ini")) == "keep configuration", "Updates must preserve user configuration");
            foreach (string name in UpdateTransaction.RequiredFiles)
                Require(File.ReadAllText(Path.Combine(root, name)) == "new:" + name, "A required file was not replaced");
            using (var lease = UpdateTransaction.AcquireLease(root))
            using (var package = Package())
            {
                calls.Clear();
                Rejected(() => UpdateTransaction.InstallVerified(package, Digest(package), root, version, stop, restart, validate: fixture));
                Require(calls.Count == 0, "A concurrent updater must not shut down the host");
            }
            Require(!Directory.Exists(Path.Combine(root, ".vmex-update-backup")), "Completed backups must be cleaned up");
            CheckLogging(root);
            Console.WriteLine("Updater verification, archive validation, transaction, rollback, configuration and concurrency checks passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        finally { Directory.Delete(root, true); }
    }
}
