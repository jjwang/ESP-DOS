#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build ESP-DOS firmware and run in QEMU (ESP32-S3 emulator).

.DESCRIPTION
    - Builds firmware and ELF apps
    - Generates flash.bin and qemu_efuse.bin
    - Launches QEMU with the proper ESP32-S3 configuration
    - RGB virtual display requires SDL backend (default); -NoGraphics disables
      display updates and only provides serial console

.PARAMETER NoBuild
    Skip build, just run QEMU with existing flash.bin
.PARAMETER NoGraphics
    Run in nographic mode (serial console only, no RGB display updates)
.PARAMETER Gdb
    Wait for GDB connection on port 3333
#>
param(
    [switch]$NoBuild,
    [switch]$NoGraphics,
    [switch]$Gdb
)

$ErrorActionPreference = "Stop"
chcp 65001 | Out-Null
$PROJ = "C:\AAAProjects\OpenCrab\ESP-DOS"
$QEMU = "$PROJ\tools\qemu\bin\qemu-system-xtensa.exe"
$FLASH = "$PROJ\flash.bin"
$EFUSE = "$PROJ\.pio\build\esp32-s3-dev\qemu_efuse.bin"

if (-not $NoBuild) {
    Write-Host "=== Building firmware ===" -ForegroundColor Cyan

    $env:PYTHONUTF8 = "1"
    $env:IDF_COMPONENT_MANAGER = "0"

    Push-Location $PROJ
    python -m platformio run
    if ($LASTEXITCODE -ne 0) { throw "PlatformIO build failed" }
    Pop-Location

    Write-Host "=== Generating flash image ===" -ForegroundColor Cyan
    python "$PROJ\tools\gen_flash_image.py"
    if ($LASTEXITCODE -ne 0) { throw "Flash image generation failed" }
}

if (-not (Test-Path $FLASH)) {
    throw "flash.bin not found. Run without -NoBuild first."
}
if (-not (Test-Path $EFUSE)) {
    Write-Host "Generating eFuse image..." -ForegroundColor Yellow
    python "$PROJ\tools\gen_flash_image.py"
}

Write-Host "=== Starting QEMU ===" -ForegroundColor Cyan
Write-Host "Machine:  esp32s3,graphics=on (32MB RAM)"
Write-Host "Flash:    $FLASH"
Write-Host "eFuse:    $EFUSE"
Write-Host "Display:  $(if ($NoGraphics) { 'none (nographic, RGB disabled)' } else { 'SDL' })"
Write-Host "GDB:      $(if ($Gdb) { 'port 3333' } else { 'disabled' })"
Write-Host ""

$args = @(
    "-M", "esp32s3,graphics=on"
    "-m", "32M"
    "-drive", "file=$FLASH,if=mtd,format=raw"
    "-drive", "file=$EFUSE,if=none,format=raw,id=efuse"
    "-global", "driver=nvram.esp32s3.efuse,property=drive,value=efuse"
    "-global", "driver=timer.esp32s3.timg,property=wdt_disable,value=true"
    "-global", "driver=ssi_psram,property=is_octal,value=true"
)

if ($Gdb) {
    $args += @("-gdb", "tcp::3333", "-S")
}

if ($NoGraphics) {
    $args += @("-nographic")
} else {
    $args += @("-display", "sdl")
}

$args += @("-serial", "mon:stdio")

$env:PATH = "$PROJ\tools\qemu\bin;$env:PATH"
Write-Host "$QEMU $($args -join ' ')" -ForegroundColor DarkGray
& $QEMU $args
