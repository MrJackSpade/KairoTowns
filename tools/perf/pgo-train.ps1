<#
.SYNOPSIS
Trains KairoTowns' profile-guided build on the device (KairoTowns#9): builds game_profile to record a
profile, runs it over the training discs and writes the merged profile to core/pgo.

.DESCRIPTION
Builds .build\perf\android-pgogen from the committed tree with the NDK (KAIROTOWNS_TOOLS=ON,
KAIROTOWNS_PGO_GENERATE=ON, KAIRO_PGO=OFF), pushes it, the firmware in bios\ and each training
disc's folder to /data/local/tmp/kairotowns, and runs each disc flat out for -Seconds of guest time,
each run writing its own .profraw. The runs are pulled to .build\perf\pgo\raw and merged with equal
weight by the NDK's llvm-profdata, which shared/tools/pgo_freeze.py then writes into core/pgo as the
profile the device build uses.

The discs below are the training set. 3x3 Eyes is kept out of it, so an evaluation can include a
game the profile has never seen.

.EXAMPLE
tools\perf\pgo-train.ps1 -Device 192.168.1.118:5555
#>
param(
    [Parameter(Mandatory)][string]$Device,
    [int]$Seconds = 90,
    [string]$Sdk = 'F:\android-sdk',
    [string]$Ndk = 'F:\android-sdk\ndk\28.2.13676358'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$discs = [ordered]@{
    afterburner = 'roms\afterburner\Afterburner (1987)(Sega)(Jp-En).cue'
    ssf2 = 'roms\ssf2\Super Street Fighter II (1993)(Capcom)(Jp-En).cue'
    mahou = 'roms\mahou\Mahou Daisakusen (aka Sorcerer Striker) (1993)(Electronic Arts)(Jp).cue'
}

$cmake = Join-Path $Sdk 'cmake\3.22.1\bin\cmake.exe'
$ninja = (Join-Path $Sdk 'cmake\3.22.1\bin\ninja.exe') -replace '\\', '/'
$dir = Join-Path $root '.build\perf\android-pgogen'
New-Item -ItemType Directory -Force (Split-Path $dir) | Out-Null
& $cmake -S (Join-Path $root 'core') -B $dir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$Ndk\build\cmake\android.toolchain.cmake" -DANDROID_ABI=arm64-v8a `
    -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DKAIROTOWNS_TOOLS=ON `
    -DKAIRO_PGO=OFF -DKAIROTOWNS_PGO_GENERATE=ON *> "$dir-configure.log"
if ($LASTEXITCODE -ne 0) { throw "configuring failed; see $dir-configure.log" }
& $cmake --build $dir --target game_profile *> "$dir-build.log"
if ($LASTEXITCODE -ne 0) { throw "building failed; see $dir-build.log" }

# The executable by its path: a bare "adb" inside the function named Adb would call the function.
$adb = (Get-Command adb -CommandType Application | Select-Object -First 1).Source
function Adb { $out = & $adb -s $Device @args 2>&1; if ($LASTEXITCODE -ne 0) { throw "adb $args failed: $out" }; $out }
. (Join-Path $PSScriptRoot 'awake.ps1')

$remote = '/data/local/tmp/kairotowns'
$raw = Join-Path $root '.build\perf\pgo\raw'
Remove-Item -Recurse -Force $raw -ErrorAction Ignore
New-Item -ItemType Directory -Force $raw | Out-Null
Adb shell "mkdir -p $remote/bios $remote/disc $remote/work $remote/pgo; rm -f $remote/pgo/*.profraw" | Out-Null
Adb push (Join-Path $dir 'game_profile') "$remote/game_profile-pgogen" | Out-Null
Adb shell "chmod 755 $remote/game_profile-pgogen" | Out-Null
foreach ($rom in Get-ChildItem (Join-Path $root 'bios') -File) { Adb push $rom.FullName "$remote/bios/$($rom.Name)" | Out-Null }
Invoke-Awake { foreach ($name in $discs.Keys) {
    $disc = Get-Item (Join-Path $root $discs[$name])
    foreach ($file in Get-ChildItem $disc.DirectoryName -File) {
        $target = "$remote/disc/$($file.Name)"
        $size = (Adb shell "stat -c %s '$target' 2>/dev/null || echo -1" | Select-Object -Last 1).Trim()
        if ($size -ne [string]$file.Length) { Adb push $file.FullName $target | Out-Null }
    }
    Adb shell "rm -f $remote/work/cmos.bin" | Out-Null
    $text = Adb shell "cd $remote && LLVM_PROFILE_FILE=$remote/pgo/$name.profraw ./game_profile-pgogen bios work 'disc/$($disc.Name)' $Seconds skip"
    "${name}: " + ($text | Select-String '^all').Line
    Adb pull "$remote/pgo/$name.profraw" (Join-Path $raw "$name.profraw") | Out-Null
} }

$profdata = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-profdata.exe'
$merged = Join-Path $root '.build\perf\pgo\kairotowns.profdata'
& $profdata merge -o $merged @(Get-ChildItem $raw -Filter '*.profraw' | ForEach-Object FullName)
if ($LASTEXITCODE -ne 0) { throw 'llvm-profdata merge failed' }
"merged $merged"
$note = 'tools/perf/pgo-train.ps1: ' + ($discs.Keys -join ', ') + ", $Seconds s of guest time each, flat out; 3x3 Eyes held out"
New-Item -ItemType Directory -Force (Join-Path $root 'core\pgo') | Out-Null
& python (Join-Path $root 'shared\tools\pgo_freeze.py') --profdata $merged --llvm-profdata $profdata --root $root `
    --text (Join-Path $root 'core\pgo\arm64.proftext') --manifest (Join-Path $root 'core\pgo\profile.json') `
    --ndk (Split-Path $Ndk -Leaf) --sparse --note $note @(Get-ChildItem $raw -Filter '*.profraw' | ForEach-Object FullName)
if ($LASTEXITCODE -ne 0) { throw 'pgo_freeze.py failed' }
