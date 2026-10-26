<#
.SYNOPSIS
Checks the JIT's AArch64 assembler (core/jit/a64_assembler.h) against clang's assembler.

.DESCRIPTION
Runs the host build's a64_encoding_test, which writes every instruction form the assembler emits
as assembly text and as the assembler's own encoding; assembles the text with clang for AArch64;
and compares the two, instruction by instruction. Fails on the first difference.

.EXAMPLE
tools\check-a64-encoding.ps1 -Build .build\host -Clang F:\tools\llvm-mingw-20261006-ucrt-x86_64\bin
#>
param(
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$Clang
)
$ErrorActionPreference = 'Stop'
$out = Join-Path $Build 'a64-encoding'
New-Item -ItemType Directory -Force $out | Out-Null
& (Join-Path $Build 'a64_encoding_test.exe') $out
if ($LASTEXITCODE -ne 0) { throw 'a64_encoding_test failed' }
$object = Join-Path $out 'expected.o'
& (Join-Path $Clang 'clang.exe') --target=aarch64-linux-gnu -c (Join-Path $out 'expected.s') -o $object
if ($LASTEXITCODE -ne 0) { throw 'clang could not assemble expected.s' }
$text = Join-Path $out 'expected.bin'
& (Join-Path $Clang 'llvm-objcopy.exe') -O binary --only-section=.text $object $text
if ($LASTEXITCODE -ne 0) { throw 'llvm-objcopy failed' }
$expected = [IO.File]::ReadAllBytes($text)
$generated = [IO.File]::ReadAllBytes((Join-Path $out 'generated.bin'))
$lines = @(Get-Content (Join-Path $out 'expected.s') | Select-Object -Skip 1)
# The object's text section may be padded past the instructions.
if ($expected.Length -lt $generated.Length) { throw "clang made $($expected.Length) bytes, the assembler $($generated.Length)" }
for ($i = 0; $i -lt $generated.Length; $i += 4) {
    $e = [BitConverter]::ToUInt32($expected, $i)
    $g = [BitConverter]::ToUInt32($generated, $i)
    if ($e -ne $g) { throw ("{0}: clang {1:X8}, the assembler {2:X8}" -f $lines[$i / 4].Trim(), $e, $g) }
}
"all $($generated.Length / 4) instructions encode as clang encodes them"
