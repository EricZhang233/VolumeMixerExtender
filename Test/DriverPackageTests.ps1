param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
foreach ($name in @('vmex_recording.sys', 'vmex_recording.inf', 'vmex_recording.cat', 'vmex_recording.cer')) {
    $file = Get-Item -LiteralPath (Join-Path $Path $name)
    if ($file.Length -eq 0) { throw "Empty driver package member: $name" }
}
$driver = Get-Item -LiteralPath (Join-Path $Path 'vmex_recording.sys')
if ($driver.VersionInfo.ProductVersion -cne $Version) { throw 'Driver version differs from Version.h.' }
$image = [IO.File]::ReadAllBytes($driver.FullName)
if ($image[0] -ne 0x4D -or $image[1] -ne 0x5A) { throw 'Invalid driver DOS header.' }
$pe = [BitConverter]::ToInt32($image, 0x3C)
if ([BitConverter]::ToUInt32($image, $pe) -ne 0x4550 -or [BitConverter]::ToUInt16($image, $pe + 4) -ne 0x8664) { throw 'Driver must be an x64 PE image.' }
if ([BitConverter]::ToUInt16($image, $pe + 24 + 68) -ne 1) { throw 'Driver must use the native subsystem.' }
if ([BitConverter]::ToUInt32($image, $pe + 24 + 112 + 14 * 8) -ne 0) { throw 'Driver must not depend on a managed runtime.' }
$inf = Get-Content -LiteralPath (Join-Path $Path 'vmex_recording.inf') -Raw
$inf = $inf.Replace("`r`n", "`n")
if ([regex]::Matches($inf, '(?im)^%DeviceName%=Install,ROOT\\VMEX_RECORDING$').Count -ne 1) { throw 'Driver hardware identity mismatch.' }
if ($inf -match '(?im)^.*(?:FX\\|EffectClsid|AddComponent|APO).*$') { throw 'Recording endpoint must not register audio processing effects.' }
if ($inf -notmatch '(?im)^HKR,EP\\0,%DisableEffects%,0x00010001,1$') { throw 'Recording endpoint must disable system effects by default.' }
Write-Output 'Driver image and package checks passed.'
