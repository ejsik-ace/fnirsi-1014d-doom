#=============================================================================================
# FNIRSI 1014D - DOOM SD Card Flasher (Windows PowerShell)
# Requires Run as Administrator
#=============================================================================================

# Ensure script is running with administrative privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Please run this script as Administrator!"
    Write-Host "Right-click PowerShell -> 'Run as administrator', then re-run this script." -ForegroundColor Yellow
    pause
    exit 1
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$binPath = Join-Path $scriptDir "fnirsi_doom_complete.bin"
$scopeBin = Join-Path $scriptDir "fnirsi_1014d_scope.bin"
$wadPath = Join-Path $scriptDir "DOOM1.WAD"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "       FNIRSI 1014D - BARE-METAL DOOM FLASHER" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

if (-not (Test-Path $binPath)) {
    Write-Error "File not found: $binPath!"
    pause
    exit 1
}

# Auto-download DOOM1.WAD if missing
if (-not (Test-Path $wadPath)) {
    Write-Host "`nDOOM1.WAD not found in folder. Downloading official Shareware WAD..." -ForegroundColor Yellow
    $wadUrl = "https://distro.ibiblio.org/pub/linux/distributions/slitaz/sources/packages/d/doom1.wad"
    try {
        Invoke-WebRequest -Uri $wadUrl -OutFile $wadPath -UseBasicParsing
        Write-Host "Downloaded DOOM1.WAD successfully!" -ForegroundColor Green
    } catch {
        Write-Warning "Failed to automatically download DOOM1.WAD. Please download it manually and place in the 'prebuilt' folder."
    }
}

# Detect removable drives
$removableDisks = Get-Disk | Where-Object { $_.Bustype -eq "USB" -or $_.Bustype -eq "SD" -or $_.MediaType -eq "Removable" }

if (-not $removableDisks) {
    Write-Warning "No removable SD card or USB drive detected!"
    Write-Host "Insert your MicroSD card into your computer and try again." -ForegroundColor Yellow
    pause
    exit 1
}

Write-Host "`nDetected removable disks:" -ForegroundColor Yellow
$removableDisks | Format-Table Number, FriendlyName, @{Label="Size (GB)"; Expression={[math]::Round($_.Size / 1GB, 2)}}

$selectedDiskNumber = $null
if ($removableDisks.Count -eq 1) {
    $selectedDiskNumber = $removableDisks[0].Number
    Write-Host "Automatically selected only detected disk: Disk $selectedDiskNumber ($($removableDisks[0].FriendlyName))" -ForegroundColor Green
} else {
    $userInput = Read-Host "Enter the disk number of your SD card (from Number column)"
    $selectedDiskNumber = [int]$userInput
}

# Find drive letter for this disk
$partition = Get-Partition -DiskNumber $selectedDiskNumber | Where-Object { $_.DriveLetter } | Select-Object -First 1
$driveLetter = $partition.DriveLetter

if ($driveLetter) {
    Write-Host "`nDetected FAT32 partition: Drive $driveLetter`: " -ForegroundColor Cyan
    Write-Host "Copying DOOM1.WAD and fnirsi_1014d_scope.bin to SD card..." -ForegroundColor Yellow
    
    if (Test-Path $wadPath) {
        Copy-Item -Path $wadPath -Destination "$($driveLetter):\" -Force
        Write-Host "  -> Copied DOOM1.WAD to drive $driveLetter`:" -ForegroundColor Green
    }
    if (Test-Path $scopeBin) {
        Copy-Item -Path $scopeBin -Destination "$($driveLetter):\" -Force
        Write-Host "  -> Copied fnirsi_1014d_scope.bin to drive $driveLetter`:" -ForegroundColor Green
    }
} else {
    Write-Warning "Could not determine drive letter for FAT32 partition. Please copy DOOM1.WAD to your SD card manually!"
}

# Raw disk writing implementation via kernel32 CreateFile / WriteFile
$csharp = @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public class RawDiskWriterDoom
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern SafeFileHandle CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool SetFilePointerEx(
        SafeFileHandle hFile,
        long liDistanceToMove,
        out long lpNewFilePointer,
        uint dwMoveMethod);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool WriteFile(
        SafeFileHandle hFile,
        byte[] lpBuffer,
        uint nNumberOfBytesToWrite,
        out uint lpNumberOfBytesWritten,
        IntPtr lpOverlapped);

    const uint GENERIC_READ = 0x80000000;
    const uint GENERIC_WRITE = 0x40000000;
    const uint FILE_SHARE_READ = 0x00000001;
    const uint FILE_SHARE_WRITE = 0x00000002;
    const uint OPEN_EXISTING = 3;

    public static void WriteToDisk(int diskNumber, long offset, byte[] data)
    {
        string devicePath = string.Format(@"\\.\PhysicalDrive{0}", diskNumber);
        SafeFileHandle handle = CreateFile(
            devicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            IntPtr.Zero,
            OPEN_EXISTING,
            0,
            IntPtr.Zero);

        if (handle.IsInvalid)
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Unable to open physical drive " + devicePath + ". Ensure PowerShell is running as Administrator!");
        }

        long newPtr;
        if (!SetFilePointerEx(handle, offset, out newPtr, 0))
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Failed to seek to offset " + offset);
        }

        uint written;
        if (!WriteFile(handle, data, (uint)data.Length, out written, IntPtr.Zero))
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error(), "Error writing to physical disk");
        }

        handle.Close();
    }
}
"@

if (-not ([System.Management.Automation.PSTypeName]'RawDiskWriterDoom').Type) {
    Add-Type -TypeDefinition $csharp
}

$data = [System.IO.File]::ReadAllBytes($binPath)
Write-Host "`nWriting bootloader and DOOM ($($data.Length) bytes) to sector 16 (offset 8192) on Disk $selectedDiskNumber..." -ForegroundColor Yellow

try {
    [RawDiskWriterDoom]::WriteToDisk($selectedDiskNumber, 8192, $data)
    Write-Host "`n==========================================================" -ForegroundColor Green
    Write-Host "   SUCCESS! DOOM WAS SUCCESSFULLY FLASHED TO SD CARD!" -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Green
    Write-Host "`nNext steps:" -ForegroundColor Cyan
    Write-Host "1. Safely eject the MicroSD card from your computer."
    Write-Host "2. Insert the card into your FNIRSI 1014D oscilloscope."
    Write-Host "3. Power on the oscilloscope and press F1 on the boot screen."
    Write-Host "4. Enjoy playing DOOM at locked 35 FPS!`n"
} catch {
    Write-Error "Write error: $_"
}

Write-Host "Press any key to exit..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
