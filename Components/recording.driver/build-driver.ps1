param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$SysvadSource,
    [string]$Generator = 'Visual Studio 18 2026'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildMutex = [Threading.Mutex]::new($false, 'Local\Vmex.Recording.DriverBuild')
$ownsBuildMutex = $false
try {
try { $ownsBuildMutex = $buildMutex.WaitOne(600000) }
catch [Threading.AbandonedMutexException] { $ownsBuildMutex = $true }
if (-not $ownsBuildMutex) { throw 'Another driver build is still running.' }
$dependencyRoot = Join-Path $projectRoot 'obj/driver-deps'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($dependency in @(
    @{ Name = 'wdk'; Id = 'microsoft.windows.wdk.x64' },
    @{ Name = 'sdk'; Id = 'microsoft.windows.sdk.cpp' }
)) {
    $destination = Join-Path $dependencyRoot $dependency.Name
    if (-not (Test-Path -LiteralPath $destination)) {
        $archivePath = Join-Path $dependencyRoot ($dependency.Name + '.nupkg')
        Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$($dependency.Id)/10.0.28000.1839/$($dependency.Id).10.0.28000.1839.nupkg" -OutFile $archivePath
        [IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $destination)
    }
}
$revision = 'bb68ed5e770bce04dc5395ec9258dafab034184d'
$managedSource = -not $SysvadSource
if (-not $SysvadSource) { $SysvadSource = Join-Path $dependencyRoot 'sysvad' }
if (-not (Test-Path -LiteralPath $SysvadSource)) {
    git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/Windows-driver-samples.git $SysvadSource
    if ($LASTEXITCODE -ne 0) { throw 'SYSVAD clone failed.' }
    git -C $SysvadSource sparse-checkout set audio/sysvad
    if ($LASTEXITCODE -ne 0) { throw 'SYSVAD sparse checkout failed.' }
}
$head = git -C $SysvadSource rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'SYSVAD revision unavailable.' }
if ($head -cne $revision) {
    git -C $SysvadSource fetch --depth 1 origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'SYSVAD fetch failed.' }
    git -C $SysvadSource checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'SYSVAD checkout failed.' }
}
$patchFile = Join-Path $PSScriptRoot 'sysvad.patch'
git -C $SysvadSource apply --reverse --check $patchFile 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $SysvadSource apply --check $patchFile 2>$null
    if ($LASTEXITCODE -ne 0) {
        $expectedCache = [IO.Path]::GetFullPath((Join-Path $dependencyRoot 'sysvad'))
        if (-not $managedSource -or [IO.Path]::GetFullPath($SysvadSource) -cne $expectedCache) {
            throw 'SYSVAD does not match the recording driver patch.'
        }
        git -C $SysvadSource reset --hard $revision
        if ($LASTEXITCODE -ne 0) { throw 'SYSVAD dependency cache refresh failed.' }
        git -C $SysvadSource apply --check $patchFile
        if ($LASTEXITCODE -ne 0) { throw 'SYSVAD recording driver patch is invalid.' }
    }
    git -C $SysvadSource apply $patchFile
    if ($LASTEXITCODE -ne 0) { throw 'SYSVAD patch failed.' }
}
$driverPreset = if ($Generator -match '18') { 'vs18-x64' } else { 'vs17-x64' }
$buildRoot = Join-Path $projectRoot "obj/recording-driver/$driverPreset"
cmake -S $PSScriptRoot -B $buildRoot -G $Generator -A x64 "-DVMEX_SYSVAD_SOURCE=$SysvadSource" "-DVMEX_WDK_ROOT=$dependencyRoot/wdk" "-DVMEX_SDK_ROOT=$dependencyRoot/sdk"
if ($LASTEXITCODE -ne 0) { throw 'Driver configure failed.' }
cmake --build $buildRoot --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Driver build failed.' }
$output = Join-Path $projectRoot "bin/$Configuration/Driver"
New-Item -ItemType Directory -Path $output -Force | Out-Null
Copy-Item -LiteralPath "$buildRoot/$Configuration/vmex_recording.sys" -Destination $output -Force
Copy-Item -LiteralPath "$buildRoot/vmex_recording.inf" -Destination $output -Force
$signTool = Join-Path $dependencyRoot 'sdk/c/bin/10.0.28000.0/x64/signtool.exe'
$certificate = $null
if ($env:VMEX_DRIVER_SIGN_CERT_THUMBPRINT) {
    $certificate = Get-Item -LiteralPath "Cert:/CurrentUser/My/$($env:VMEX_DRIVER_SIGN_CERT_THUMBPRINT)"
} else {
    $certificate = Get-ChildItem Cert:/CurrentUser/My -CodeSigningCert |
        Where-Object { $_.Subject -ceq 'CN=VolumeMixerExtender Development Driver' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(30) } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $certificate) {
        $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=VolumeMixerExtender Development Driver' `
            -CertStoreLocation Cert:/CurrentUser/My -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
            -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(3)
    }
}
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date)) { throw 'A current signing certificate with a private key is required.' }
Export-Certificate -Cert $certificate -FilePath "$output/vmex_recording.cer" -Force | Out-Null
& $signTool sign /sha1 $certificate.Thumbprint /fd SHA256 "$output/vmex_recording.sys"
if ($LASTEXITCODE -ne 0) { throw 'Driver image signing failed.' }
$catalogTool = Join-Path $dependencyRoot 'wdk/c/bin/10.0.28000.0/x86/Inf2Cat.exe'
& $catalogTool "/driver:$output" /os:10_GE_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw 'Driver package validation failed.' }
& $signTool sign /sha1 $certificate.Thumbprint /fd SHA256 "$output/vmex_recording.cat"
if ($LASTEXITCODE -ne 0) { throw 'Driver catalog signing failed.' }
Write-Output "Built and validated driver package in $output."
} finally {
    if ($ownsBuildMutex) { $buildMutex.ReleaseMutex() }
    $buildMutex.Dispose()
}
