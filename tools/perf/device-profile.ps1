<#
.SYNOPSIS
Runs core/test/game_profile on an Android device through adb, to measure the core on the device
itself with the same tool the host uses (KairoTowns#31).

.DESCRIPTION
Builds game_profile for arm64 with the NDK (KAIROTOWNS_TOOLS=ON, release) into
.build\perf\android-<Name>, pushes it, the firmware in bios\ and the files of the disc image's
folder to /data/local/tmp/kairotowns on the device (a disc already there with the same size is
not pushed again), runs it with the given game_profile options and prints its report.

.EXAMPLE
tools\perf\device-profile.ps1 -Device 192.168.1.118:5555 -Disc 'roms\mahou\Mahou Daisakusen (aka Sorcerer Striker) (1993)(Electronic Arts)(Jp).cue' -Seconds 90 -Options check,exact

-Name and -CMakeArgs build a variant beside the plain build, for comparing compiler settings.
#>
param(
    [Parameter(Mandatory)][string]$Device,
    [Parameter(Mandatory)][string]$Disc,
    [Parameter(Mandatory)][int]$Seconds,
    [string[]]$Options = @('skip'),
    [string]$Name = 'plain',
    [string[]]$CMakeArgs = @(),
    [string]$Sdk = 'F:\android-sdk',
    [string]$Ndk = 'F:\android-sdk\ndk\28.2.13676358'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmake = Join-Path $Sdk 'cmake\3.22.1\bin\cmake.exe'
$ninja = (Join-Path $Sdk 'cmake\3.22.1\bin\ninja.exe') -replace '\\', '/'
$dir = Join-Path $root ".build\perf\android-$Name"
New-Item -ItemType Directory -Force (Split-Path $dir) | Out-Null
& $cmake -S (Join-Path $root 'core') -B $dir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$Ndk\build\cmake\android.toolchain.cmake" -DANDROID_ABI=arm64-v8a `
    -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DKAIROTOWNS_TOOLS=ON `
    @CMakeArgs *> "$dir-configure.log"
if ($LASTEXITCODE -ne 0) { throw "configuring failed; see $dir-configure.log" }
& $cmake --build $dir --target game_profile *> "$dir-build.log"
if ($LASTEXITCODE -ne 0) { throw "building failed; see $dir-build.log" }

$adb = (Get-Command adb -CommandType Application | Select-Object -First 1).Source
function Adb { $out = & $adb -s $Device @args 2>&1; if ($LASTEXITCODE -ne 0) { throw "adb $args failed: $out" }; $out }
. (Join-Path $PSScriptRoot 'awake.ps1')

$remote = '/data/local/tmp/kairotowns'
Adb shell "mkdir -p $remote/bios $remote/disc $remote/work" | Out-Null
Adb push (Join-Path $dir 'game_profile') "$remote/game_profile-$Name" | Out-Null
Adb shell "chmod 755 $remote/game_profile-$Name" | Out-Null
foreach ($rom in Get-ChildItem (Join-Path $root 'bios') -File) { Adb push $rom.FullName "$remote/bios/$($rom.Name)" | Out-Null }
$discItem = Get-Item (Join-Path $root $Disc)
foreach ($file in Get-ChildItem $discItem.DirectoryName -File) {
    $target = "$remote/disc/$($file.Name)"
    $size = (Adb shell "stat -c %s '$target' 2>/dev/null || echo -1" | Select-Object -Last 1).Trim()
    if ($size -ne [string]$file.Length) { Adb push $file.FullName $target | Out-Null }
}
Adb shell "rm -f $remote/work/cmos.bin" | Out-Null
$command = "cd $remote && ./game_profile-$Name bios work 'disc/$($discItem.Name)' $Seconds $($Options -join ' ')"
Invoke-Awake {
    & $adb -s $Device shell $command 2>&1 | Where-Object { $_ -match '^(frames|all|  hash|FAIL|CS:EIP|  [0-9A-F]{4}:)' }
    if ($LASTEXITCODE -ne 0) { throw "game_profile failed on the device" }
}
