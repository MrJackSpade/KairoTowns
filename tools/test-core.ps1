<#
.SYNOPSIS
Builds the FM Towns core's host test and boots a disc with it, memory-limited.

.DESCRIPTION
Builds core/ for the host with llvm-mingw into .build\host, then runs test\boot_test through
shared\tools\run-limited.ps1. ROM images are read from bios\ and results go to .build\boot\.

.EXAMPLE
tools\test-core.ps1 "roms\afterburner\Afterburner (1987)(Sega)(Jp-En).cue" -Seconds 40
#>
param(
    [Parameter(Mandatory = $true)][string]$Disc,
    [int]$Seconds = 20
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$toolchain = 'F:/tools/llvm-mingw-20261006-ucrt-x86_64/bin'
$cmake = 'F:\android-sdk\cmake\3.22.1\bin\cmake.exe'
$build = Join-Path $root '.build\host'
$env:PATH = "$($toolchain.Replace('/', '\'));$env:PATH"

if (-not (Test-Path (Join-Path $build 'build.ninja'))) {
    & $cmake -S (Join-Path $root 'core') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_MAKE_PROGRAM=F:/android-sdk/cmake/3.22.1/bin/ninja.exe `
        "-DCMAKE_C_COMPILER=$toolchain/clang.exe" "-DCMAKE_CXX_COMPILER=$toolchain/clang++.exe"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
}
& $cmake --build $build --target boot_test -j 16
if ($LASTEXITCODE -ne 0) { throw 'Host build failed' }

$work = Join-Path $root '.build\boot'
New-Item -ItemType Directory -Force $work | Out-Null
& (Join-Path $root 'shared\tools\run-limited.ps1') (Join-Path $build 'boot_test.exe') `
    (Join-Path $root 'bios') $work (Resolve-Path (Join-Path $root $Disc)).Path "$Seconds"
if ($LASTEXITCODE -ne 0) { throw "boot_test failed (exit $LASTEXITCODE)" }
