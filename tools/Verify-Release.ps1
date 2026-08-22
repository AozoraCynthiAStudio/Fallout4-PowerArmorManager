param(
    [string]$Version = "2.2.7"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$release = Join-Path $repo ("releases\v" + $Version)

Add-Type -AssemblyName System.IO.Compression.FileSystem

$required = @(
    "PowerArmorMgr.esp",
    "F4SE/Plugins/PowerArmorMgr.dll",
    "Scripts/PowerArmorMgr.pex",
    "PrismaUI_F4/views/PowerArmorMgr/menu.html",
    "PrismaUI_F4/views/PowerArmorMgr/result.html",
    "MCM/Config/PowerArmorMgr/config.json",
    "Scripts/Source/User/PowerArmorMgr.psc"
)

$zips = @(Get-ChildItem -LiteralPath $release -Filter "PowerArmorManager_$Version*.zip" -File | Sort-Object Name)
if ($zips.Count -ne 2) {
    throw "Expected exactly two language ZIP files in $release, found $($zips.Count)."
}

foreach ($zipFile in $zips) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipFile.FullName)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.TrimStart('./') })
        $missing = @($required | Where-Object { $_ -notin $names })
        if ($missing.Count -gt 0) {
            throw "$($zipFile.Name) is missing: $($missing -join ', ')"
        }
        Write-Host "[OK] $($zipFile.Name) contains the required release files."
    }
    finally {
        $zip.Dispose()
    }

    $hash = (Get-FileHash -LiteralPath $zipFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "$hash  $($zipFile.Name)"
}
