# Script to package fnirsi_doom_complete.bin from compiled src/fnirsi_doom.bin and prebuilt bootloader

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoDir = Split-Path -Parent $scriptDir

$doomPath = Join-Path $repoDir "src\fnirsi_doom.bin"
$outPath = Join-Path $repoDir "prebuilt\fnirsi_doom_complete.bin"
$bootloaderPath = Join-Path $repoDir "prebuilt\fnirsi_sd_card_bootloader.bin"

# If bootloader doesn't exist in prebuilt, extract first 32KB from existing complete bin or prompt
if (-not (Test-Path $bootloaderPath)) {
    if (Test-Path $outPath) {
        $existing = [System.IO.File]::ReadAllBytes($outPath)
        $bl = New-Object byte[] 0x8000
        [Array]::Copy($existing, 0, $bl, 0, 0x8000)
        [System.IO.File]::WriteAllBytes($bootloaderPath, $bl)
    } else {
        Write-Error "Bootloader binary not found!"
        exit 1
    }
}

if (-not (Test-Path $doomPath)) {
    Write-Error "File $doomPath not found! Please compile the project first in src/ directory using make."
    exit 1
}

$bootloader = [System.IO.File]::ReadAllBytes($bootloaderPath)
$doom = [System.IO.File]::ReadAllBytes($doomPath)

$padToSector = 512 - ($doom.Length % 512)
if ($padToSector -eq 512) { $padToSector = 0 }
$totalSize = 0x8000 + $doom.Length + $padToSector

$package = New-Object byte[] $totalSize
[Array]::Copy($bootloader, 0, $package, 0, $bootloader.Length)
[Array]::Copy($doom, 0, $package, 0x8000, $doom.Length)

[System.IO.File]::WriteAllBytes($outPath, $package)

Write-Host "Successfully packaged: $outPath" -ForegroundColor Green
Write-Host "Package size: $($package.Length) bytes ($([math]::Round($package.Length / 1024, 2)) KB)" -ForegroundColor Cyan
