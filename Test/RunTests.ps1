$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$reports = Join-Path $repo "Test/artifacts/$runId"
$buildDirectory = Join-Path $repo 'obj/local-tests'
$records = [Collections.Generic.List[object]]::new()
$failed = $false
$failure = $null
$lockHeld = $false
$mutex = $null
New-Item -ItemType Directory -Path $reports -Force | Out-Null

function Save-Report {
    $summary = [ordered]@{
        status = $(if ($failed) { 'failed' } elseif (@($records | Where-Object { $_.status -ne 'passed' }).Count -eq 0) { 'passed' } else { 'running' })
        error = $failure
        phases = @($records.ToArray())
    }
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reports 'summary.json') -Encoding utf8
    $lines = @('# VolumeMixerExtender tests', '', "Status: $($summary.status)", '', '| Phase | Status | Log |', '| --- | --- | --- |')
    $lines += $records | ForEach-Object { "| $($_.name) | $($_.status) | [$($_.log)]($($_.log)) |" }
    if ($failure) { $lines += @('', "Error: $failure") }
    $lines | Set-Content -LiteralPath (Join-Path $reports 'summary.md') -Encoding utf8
}

function Invoke-Step([string]$Executable, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command $Executable -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source)
    $start.WorkingDirectory = $repo
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    [IO.File]::AppendAllText($script:activeLog, "COMMAND: $Executable $($Arguments -join ' ')`n")
    $child = [Diagnostics.Process]::Start($start)
    try {
        $child.StandardInput.Close()
        $stdout = $child.StandardOutput.ReadToEndAsync()
        $stderr = $child.StandardError.ReadToEndAsync()
        $timedOut = -not $child.WaitForExit(1200000)
        if ($timedOut) { $child.Kill($true); $child.WaitForExit(10000) | Out-Null }
        $output = $stdout.WaitAsync([TimeSpan]::FromSeconds(10)).GetAwaiter().GetResult()
        $errorOutput = $stderr.WaitAsync([TimeSpan]::FromSeconds(10)).GetAwaiter().GetResult()
        [IO.File]::AppendAllText($script:activeLog, $output + "`nSTDERR:`n" + $errorOutput)
        if ($output) { Write-Host $output }
        if ($errorOutput) { Write-Host $errorOutput }
        if ($timedOut) { throw 'Test phase exceeded twenty minutes; its process tree was stopped.' }
        if ($child.ExitCode -ne 0) { throw "$Executable exited with code $($child.ExitCode). See $script:activeLog" }
    } finally {
        if (-not $child.HasExited) { $child.Kill($true); $child.WaitForExit(10000) | Out-Null }
        $child.Dispose()
    }
}

$definitions = @(
    @{ Name = 'Preflight'; Action = {
        if (-not $IsWindows) { throw 'Run tests on Windows.' }
        foreach ($tool in @('cmake', 'ctest', 'node', 'dotnet', 'pwsh.exe')) { Get-Command $tool -CommandType Application -ErrorAction Stop | Out-Null }
        $key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($repo.ToLowerInvariant())))
        $script:mutex = [Threading.Mutex]::new($false, "Local\VolumeMixerExtender.Tests.$key")
        try { $script:lockHeld = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $script:lockHeld = $true }
        if (-not $lockHeld) { throw 'Another test run is using this checkout.' }
    } },
    @{ Name = 'Release workflow contracts'; Action = { Invoke-Step 'node' @('Test/ReleaseWorkflowTests.mjs') } },
    @{ Name = 'Configure native tests'; Action = { Invoke-Step 'cmake' @('--preset', 'vs18-x64', '-B', $buildDirectory, '-DBUILD_TESTING=ON') } },
    @{ Name = 'Build Debug test fixtures'; Action = { Invoke-Step 'cmake' @('--build', $buildDirectory, '--config', 'Debug', '--target', 'vmex_tests', 'vmex_manager_ui_tests', 'vmex') } },
    @{ Name = 'All CTest suites'; Action = { Invoke-Step 'ctest' @('--test-dir', $buildDirectory, '-C', 'Debug', '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $reports 'test-results.xml')) } }
)
foreach ($definition in $definitions) { $records.Add([pscustomobject]@{ name = $definition.Name; status = 'not_run'; log = ('{0:D2}.log' -f $records.Count) }) }
try {
    Save-Report
    for ($index = 0; $index -lt $definitions.Count; $index++) {
        $record = $records[$index]
        $record.status = 'running'
        $script:activeLog = Join-Path $reports $record.log
        Write-Host "RUN [$($index + 1)/$($definitions.Count)] $($record.name)"
        Save-Report
        try {
            & $definitions[$index].Action
            $record.status = 'passed'
        } catch {
            $record.status = 'failed'
            throw
        } finally { Save-Report }
    }
} catch {
    $failed = $true
    $failure = $_.Exception.Message
    Write-Host "FAILED: $failure"
} finally {
    Save-Report
    if ($lockHeld) { $mutex.ReleaseMutex() }
    if ($mutex) { $mutex.Dispose() }
}
Write-Host "Report: $reports"
if ($failed) { exit 1 }
Write-Host 'TESTS PASSED'
