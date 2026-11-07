param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
$members = @(
    'vbMmeCable64_win10.inf',
    'vbaudio_cable64_win10.cat',
    'vbaudio_cable64_win10.sys',
    'vbaudio_cable64arm_win10.sys')
Add-Type -Namespace Vmex -Name ResourceReader -MemberDefinition @'
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(string file, IntPtr unused, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")]
    public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")]
    public static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")]
    public static extern int SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
'@
$module = [Vmex.ResourceReader]::LoadLibraryEx((Resolve-Path -LiteralPath $Path).Path, [IntPtr]::Zero, 0x22)
if ($module -eq [IntPtr]::Zero) { throw "Could not open resources in $Path" }
$scratch = $null
try {
    $scratch = [IO.Directory]::CreateTempSubdirectory('Vmex.DriverPackage.')
    for ($index = 0; $index -lt $members.Count; $index++) {
        $name = $members[$index]
        $resource = [Vmex.ResourceReader]::FindResource($module, [IntPtr](201 + $index), [IntPtr]10)
        if ($resource -eq [IntPtr]::Zero) { throw "Missing embedded VB-CABLE member: $name" }
        $size = [Vmex.ResourceReader]::SizeofResource($module, $resource)
        $loaded = [Vmex.ResourceReader]::LoadResource($module, $resource)
        $data = [Vmex.ResourceReader]::LockResource($loaded)
        if ($size -le 0 -or $data -eq [IntPtr]::Zero) { throw "Empty embedded VB-CABLE member: $name" }
        $bytes = [byte[]]::new($size)
        [Runtime.InteropServices.Marshal]::Copy($data, $bytes, 0, $size)
        [IO.File]::WriteAllBytes((Join-Path $scratch.FullName $name), $bytes)
    }
    $catalog = Join-Path $scratch.FullName 'vbaudio_cable64_win10.cat'
    $signature = Get-AuthenticodeSignature -LiteralPath $catalog
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.GetNameInfo('SimpleName', $false) -cne 'Microsoft Windows Hardware Compatibility Publisher') {
        throw 'VB-CABLE catalog must carry a valid Microsoft hardware signature.'
    }
    $signTool = Get-ChildItem -LiteralPath "${env:ProgramFiles(x86)}/Windows Kits/10/bin" -Filter signtool.exe -Recurse |
        Where-Object { $_.Directory.Name -eq 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signTool) { throw 'Windows SDK signtool is required.' }
    foreach ($name in $members | Where-Object { $_ -notlike '*.cat' }) {
        & $signTool.FullName verify /kp /c $catalog (Join-Path $scratch.FullName $name)
        if ($LASTEXITCODE -ne 0) { throw "VB-CABLE kernel catalog verification failed: $name" }
    }
} finally {
    [void][Vmex.ResourceReader]::FreeLibrary($module)
    if ($scratch) {
        foreach ($name in $members) { [IO.File]::Delete((Join-Path $scratch.FullName $name)) }
        $scratch.Delete()
    }
}
Write-Output 'Embedded VB-CABLE package and Microsoft kernel signature verified.'
