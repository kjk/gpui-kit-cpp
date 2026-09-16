param(
    [string]$SdkRoot = "",
    [switch]$Check
)

$ErrorActionPreference = "Stop"
$ndkVersion = "30.0.16248370"
$archiveRevision = "r30"
$archiveSha1 = "9bf167a1985fa7d4a036186b78f702eab9179408"
$archiveUrl = "https://dl.google.com/android/repository/android-ndk-r30-windows.zip"

if (-not $SdkRoot) {
    if ($env:ANDROID_HOME) {
        $SdkRoot = $env:ANDROID_HOME
    } elseif ($env:ANDROID_SDK_ROOT) {
        $SdkRoot = $env:ANDROID_SDK_ROOT
    } else {
        $SdkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk"
    }
}

$sdkFull = [IO.Path]::GetFullPath($SdkRoot)
$target = Join-Path $sdkFull "ndk\$ndkVersion"
$properties = Join-Path $target "source.properties"
if ((Test-Path -LiteralPath $properties) -and
    ((Get-Content -LiteralPath $properties -Raw) -match "Pkg\.Revision\s*=\s*$([regex]::Escape($ndkVersion))")) {
    Write-Host "Android NDK $ndkVersion is installed at $target"
    exit 0
}

if ($Check) {
    throw "Android NDK $ndkVersion is missing. Run this script without -Check."
}

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("gpui-android-ndk-" + [guid]::NewGuid().ToString("N"))
$archive = Join-Path $tempRoot "android-ndk-$archiveRevision-windows.zip"
$unpack = Join-Path $tempRoot "unpack"
New-Item -ItemType Directory -Path $unpack -Force | Out-Null

try {
    Write-Host "Downloading Android NDK $ndkVersion (r30)..."
    & curl.exe --fail --location --progress-bar --output $archive $archiveUrl
    if ($LASTEXITCODE -ne 0) {
        throw "curl failed to download Android NDK $ndkVersion"
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash.ToLowerInvariant()
    if ($actual -ne $archiveSha1) {
        throw "Android NDK archive checksum mismatch: expected $archiveSha1, got $actual"
    }
    & tar.exe -xf $archive -C $unpack
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed to extract Android NDK $ndkVersion"
    }
    $extracted = Join-Path $unpack "android-ndk-$archiveRevision"
    if (-not (Test-Path -LiteralPath (Join-Path $extracted "source.properties"))) {
        throw "Downloaded archive did not contain android-ndk-$archiveRevision"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    if (Test-Path -LiteralPath $target) {
        throw "Refusing to replace unexpected existing directory: $target"
    }
    Move-Item -LiteralPath $extracted -Destination $target
    Write-Host "Installed Android NDK $ndkVersion at $target"
} finally {
    $tempFull = [IO.Path]::GetFullPath($tempRoot)
    $systemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($tempFull.StartsWith($systemTemp, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $tempFull)) {
        Remove-Item -LiteralPath $tempFull -Recurse -Force
    }
}
