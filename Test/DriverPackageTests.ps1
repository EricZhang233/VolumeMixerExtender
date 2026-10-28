param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
$expected = @(
    'VBCABLE_ControlPanel.exe',
    'VBCABLE_Setup_x64.exe',
    'pin_in.ico',
    'pin_out.ico',
    'readme.txt',
    'vbaudio_cable64_win10.cat',
    'vbaudio_cable64_win10.sys',
    'vbaudio_cable64arm_win10.sys',
    'vbMmeCable64_win10.inf')
$actual = @(Get-ChildItem -LiteralPath $Path -File | ForEach-Object { $_.Name } | Sort-Object)
$missing = @($expected | Where-Object { $actual -cnotcontains $_ })
$unexpected = @($actual | Where-Object { $expected -cnotcontains $_ })
if ($missing.Count -gt 0) { throw "Missing VB-CABLE package member: $($missing -join ', ')" }
if ($unexpected.Count -gt 0) { throw "Unexpected VB-CABLE package member: $($unexpected -join ', ')" }
foreach ($name in $expected) {
    if ((Get-Item -LiteralPath (Join-Path $Path $name)).Length -eq 0) { throw "Empty VB-CABLE package member: $name" }
}
$catalog = Join-Path $Path 'vbaudio_cable64_win10.cat'
$signature = Get-AuthenticodeSignature -LiteralPath $catalog
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.GetNameInfo('SimpleName', $false) -cne 'Microsoft Windows Hardware Compatibility Publisher') {
    throw 'VB-CABLE catalog must carry a valid Microsoft hardware signature.'
}
$signTool = Get-ChildItem -LiteralPath "${env:ProgramFiles(x86)}/Windows Kits/10/bin" -Filter signtool.exe -Recurse |
    Where-Object { $_.Directory.Name -eq 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signTool) { throw 'Windows SDK signtool is required.' }
foreach ($name in @('vbaudio_cable64_win10.sys', 'vbaudio_cable64arm_win10.sys', 'vbMmeCable64_win10.inf')) {
    & $signTool.FullName verify /kp /c $catalog (Join-Path $Path $name)
    if ($LASTEXITCODE -ne 0) { throw "VB-CABLE kernel catalog verification failed: $name" }
}
if ((Get-FileHash -LiteralPath (Join-Path $Path 'VBCABLE_Setup_x64.exe') -Algorithm SHA256).Hash -cne '734C35DFA6D98F48782A451633CEB471166EC70D60482FD89A1123D0EE3C4F41') {
    throw 'VB-CABLE installer differs from the pinned official package.'
}
if ((Get-FileHash -LiteralPath (Join-Path $Path 'VBCABLE_ControlPanel.exe') -Algorithm SHA256).Hash -cne 'F5B44706FE7BA2EED0516DEE791F826DC7A9891E997F6CE9704E2899300B14FF') {
    throw 'VB-CABLE control panel differs from the pinned official package.'
}
Write-Output 'Windows 11 x64 VB-CABLE package and Microsoft kernel signature verified.'
