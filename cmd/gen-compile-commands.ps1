<#
.SYNOPSIS
    Generate compile_commands.json for clangd from the gpui build system.

.DESCRIPTION
    Creates a compile_commands.json that tells clangd to treat all src/** files
    as part of the amalgam (.work/gpui.cpp), not as standalone TUs. This gives
    correct diagnostics and navigation even when a src/** file is open in the editor.

    The amalgam is the only file that actually compiles; src/** are concatenated
    into it with #line directives for mapping diagnostics back.

.EXAMPLE
    .\cmd\gen-compile-commands.ps1
    # Generates .work\compile_commands.json

.NOTES
    Requires: clang-cl on PATH (VS Installer → C++ Clang Compiler for Windows)
#>
param(
    [switch]$Debug,
    [string]$OutFile = ".work\compile_commands.json"
)

$ErrorActionPreference = "Stop"
# The repo root is the parent of cmd/.
$root = Split-Path -Parent $PSScriptRoot
Write-Host "Debug: root=$root"

$amalgamDir = Join-Path $root ".work"
$gpuiCpp = Join-Path $amalgamDir "gpui.cpp"
$gpuiH = Join-Path $amalgamDir "gpui.h"

if (-not (Test-Path $gpuiCpp) -or -not (Test-Path $gpuiH)) {
    Write-Error @"
Amalgam not found. Run a build first:
    bun cmd/build.ts -rel system_monitor

This generates .work/gpui.h and .work/gpui.cpp which compile_commands.json references.
"@
    exit 1
}

# Find clang-cl for the compile commands (clangd uses it to understand flags)
$clangCl = Get-Command clang-cl -ErrorAction SilentlyContinue
if (-not $clangCl) {
    Write-Warning "clang-cl not found on PATH. Using placeholder 'clang-cl.exe'."
    Write-Warning "For full IntelliSense, install 'C++ Clang Compiler for Windows' via VS Installer."
    $clangClPath = "clang-cl.exe"
} else {
    $clangClPath = $clangCl.Source
}

# Base flags matching cflagsFor() in build.ts for Windows + clang-cl (release)
$baseFlags = @(
    "/nologo",
    "/std:c++20",
    # build.ts compiles without exceptions or RTTI.
    "/EHs-c-",
    "/GR-",
    "/D_HAS_EXCEPTIONS=0",
    "/utf-8",
    "/I", $amalgamDir,
    	"/DUNICODE",
    	"/D_UNICODE",
    	# Explicit MSVC architecture + version so the STL headers don't emit
    	# STL1003 "Unexpected compiler" under clangd's clang-cl driver.
    	"/D_M_X64",
	"/D_MSC_VER=1930",
	"/W4",
	# Force C++ mode for all files, including .h headers.
	# Without this, clangd defaults .h to C and C++ syntax (namespaces, templates)
	# produces errors like "unknown type name 'namespace'".
	"/TP",
    "/WX",
    "/wd4996",
    "/Z7",
    "-Wno-missing-field-initializers",
    "-Wno-microsoft-exception-spec",
    "-Wno-delete-non-abstract-non-virtual-dtor",
    "-Wno-unused-command-line-argument",
    "-Wno-unused-function"
)

if ($Debug) {
    $baseFlags += @("/Od", "/MTd", "/DDEBUG")
} else {
    $baseFlags += @("/O2", "/Gy", "/Gw", "/MT", "/DNDEBUG")
}

# The amalgam itself is the main TU
$commands = @()

# Entry for the amalgam - this is what actually compiles
$commands += @{
    directory = $root
    file = $gpuiCpp
    arguments = @($clangClPath) + $baseFlags + @("/TP", $gpuiCpp)
}

# Also register gpui.h as a "header" the amalgam depends on
# (clangd uses this for include resolution)
$commands += @{
    directory = $root
    file = $gpuiH
    arguments = @($clangClPath) + $baseFlags + @("/TP", $gpuiH)
}

# For every src/** file, create an entry that points clangd at the amalgam.
# When clangd sees a src/** file, it will use the amalgam's compile command (flags only),
# which gives it the full context (#defines, includes from -I.work, etc.).
# Do NOT include a main file (/TP <file>) here — that would cause clangd to
# compile both the original main AND the looked-up src file in one TU, leading
# to duplicate includes (e.g., base.h via gpui.cpp and via entity.cpp).
Get-ChildItem -Path (Join-Path $root "src") -Recurse -File -Include "*.cpp","*.h","*.hpp" | ForEach-Object {
    $srcFile = $_.FullName
    $commands += @{
        directory = $root
        file = $srcFile
        arguments = @($clangClPath) + $baseFlags
    }
}

$json = $commands | ConvertTo-Json -Depth 10
$json | Out-File -Encoding utf8 $OutFile

Write-Host "Wrote $OutFile with $($commands.Count) entries"
Write-Host ""
Write-Host "clangd will now:"
Write-Host "  - Index the amalgam (.work/gpui.cpp) as the real TU"
Write-Host "  - Map diagnostics back to src/** via #line directives"
Write-Host "  - Resolve includes correctly for both amalgam and src/** files"
Write-Host ""
Write-Host "Restart your language server (Zed: 'editor: restart language server')"
Write-Host "or reload the window for changes to take effect."
