param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.GetNameInfo('SimpleName', $false) -cne 'Microsoft Corporation') {
    throw 'XAML diagnostics must carry a valid Microsoft signature.'
}
$stream = [IO.File]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
$reader = [IO.BinaryReader]::new($stream)
try {
    if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'XAML diagnostics is not a PE image.' }
    $stream.Position = 0x3C
    $stream.Position = $reader.ReadUInt32()
    if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
        throw 'XAML diagnostics must be an x64 PE image.'
    }
} finally { $reader.Dispose() }
Write-Output 'Microsoft XAML diagnostics signature and x64 architecture verified.'
