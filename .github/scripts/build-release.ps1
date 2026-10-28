param(
    [Parameter(Mandatory)][string]$Tag,
    [ValidateSet('vs17-x64', 'vs18-x64')][string]$Preset = 'vs17-x64',
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
if ($Tag -cnotmatch '^v\d+\.\d+\.\d+(\.\d+)?$') { throw 'Invalid release tag.' }
$versionHeader = Get-Content "$PSScriptRoot/../../Version.h" -Raw
if ($versionHeader -notmatch 'VMEX_VERSION_STRING\s+"([0-9.]+)"') { throw 'Missing version source.' }
$version = $Matches[1]
if ($Tag -cne "v$version") { throw 'Tag does not match Version.h.' }
if (-not $PackageDirectory) {
    $PackageDirectory = Join-Path ([IO.Path]::GetTempPath()) ('VMEX.Release.' + [guid]::NewGuid().ToString('N'))
}
$packageRoot = [IO.Path]::GetFullPath($PackageDirectory)
if ((Test-Path -LiteralPath $packageRoot) -and (Get-ChildItem -LiteralPath $packageRoot -Force | Select-Object -First 1)) {
    throw 'Release package directory must be empty; existing archives cannot be reused.'
}
cmake --preset $Preset -DBUILD_TESTING=OFF -DVMEX_BUILD_RECORDING_DRIVER=ON "-DVMEX_PACKAGE_DIR=$packageRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
cmake --build "obj/$Preset" --config Release --target package
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
$packageName = "VolumeMixerExtender_$version.zip"
$packagePath = (Resolve-Path (Join-Path $packageRoot $packageName)).Path
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($packagePath)
try {
    $expected = @('vmex.exe', 'vmex_cli.exe', 'vmex_core.dll', 'vmex_manager.exe',
        'vmex_tap.dll', 'vmex_launcher.dll', 'xamldiagnostics.dll')
    $cableFiles = Get-ChildItem -LiteralPath 'bin/Release/Driver/VBCABLE' -File
    if ($cableFiles.Count -lt 5) { throw 'Original VB-CABLE package is missing.' }
    $expected += @($cableFiles | ForEach-Object { 'Driver/VBCABLE/' + $_.Name })
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName -cin @('Driver/', 'Driver/VBCABLE/')) { continue }
        if ($entry.FullName -cnotin $expected) {
            throw "Unexpected package member $($entry.FullName)."
        }
        if ($entry.FullName.Contains('..') -or $entry.FullName.StartsWith('/')) { throw 'Unsafe archive path.' }
    }
    foreach ($name in $expected) {
        $entry = @($archive.Entries | Where-Object { $_.FullName -ceq $name })
        if ($entry.Count -ne 1 -or $entry[0].Length -eq 0) { throw "Missing or empty package member $name." }
        $built = Get-Item "bin/Release/$name"
        if (($name.EndsWith('.exe') -or $name.EndsWith('.dll')) -and
            -not $name.StartsWith('Driver/') -and $name -cne 'xamldiagnostics.dll' -and $built.VersionInfo.ProductVersion -cne $version) { throw "Version mismatch in $name." }
        $memberStream = $entry[0].Open()
        $builtStream = $built.OpenRead()
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            $memberHash = [Convert]::ToHexString($algorithm.ComputeHash($memberStream))
            $builtHash = [Convert]::ToHexString($algorithm.ComputeHash($builtStream))
            if ($memberHash -cne $builtHash) { throw "Archive differs from fresh build: $name." }
        } finally {
            $memberStream.Dispose()
            $builtStream.Dispose()
            $algorithm.Dispose()
        }
    }
} finally { $archive.Dispose() }
& "$PSScriptRoot/../../Test/DriverPackageTests.ps1" -Path 'bin/Release/Driver/VBCABLE'
$digest = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
Write-Output "Built $packageName SHA256=$digest"
if ($env:GITHUB_OUTPUT) {
    @("version=$version", "package_path=$packagePath", "artifact_name=vmex-release-$version", "package_name=$packageName") |
        Out-File -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
