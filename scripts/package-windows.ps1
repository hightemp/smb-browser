param(
    [string]$BuildDir = "tmp\package-windows",
    [string]$Generator = "Ninja",
    [string]$BuildType = "Release",
    [switch]$SkipTests,
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Stop"
$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (![System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $RootDir $BuildDir
}
$Version = (Get-Content -LiteralPath (Join-Path $RootDir "VERSION") -Raw).Trim()
if ($Version -notmatch '^[0-9]+(\.[0-9]+){2,3}$') {
    throw "Invalid VERSION '$Version'. Expected numeric MAJOR.MINOR.PATCH."
}

function Invoke-CheckedCommand {
    param(
        [string]$Command,
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

Invoke-CheckedCommand "cmake" @(
    "-S", $RootDir,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DSMB_BROWSER_WITH_LIBSMB2=OFF",
    "-DSMB_BROWSER_WITH_NATIVE_SMB=ON",
    "-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF"
)

Invoke-CheckedCommand "cmake" @("--build", $BuildDir, "--config", $BuildType)

if (!$SkipTests) {
    Invoke-CheckedCommand "ctest" @(
        "--test-dir", $BuildDir,
        "--output-on-failure",
        "-C", $BuildType
    )
}

$Exe = Join-Path $BuildDir "src\app\smb-browser.exe"
if (!(Test-Path $Exe)) {
    throw "Built executable not found: $Exe"
}

function Copy-FirstPathMatch {
    param(
        [string[]]$Patterns,
        [string]$Destination
    )

    $PathEntries = $env:PATH -split [System.IO.Path]::PathSeparator
    foreach ($Pattern in $Patterns) {
        foreach ($Entry in $PathEntries) {
            if ([string]::IsNullOrWhiteSpace($Entry) -or !(Test-Path $Entry)) {
                continue
            }
            $Match = Get-ChildItem -Path $Entry -File -Filter $Pattern -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($Match) {
                Copy-Item -LiteralPath $Match.FullName -Destination $Destination -Force
                break
            }
        }
    }
}

$StageDir = Join-Path $BuildDir "stage\smb-browser"
Remove-Item -Recurse -Force $StageDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
Copy-Item -LiteralPath $Exe -Destination $StageDir -Force

$StageExe = Join-Path $StageDir "smb-browser.exe"
$WinDeployQt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (!$WinDeployQt) {
    $WinDeployQt = Get-Command windeployqt-qt5.exe -ErrorAction SilentlyContinue
}
if ($WinDeployQt -and (Test-Path $StageExe)) {
    Invoke-CheckedCommand $WinDeployQt.Source @(
        "--compiler-runtime",
        "--no-translations",
        $StageExe
    )
} else {
    throw "windeployqt.exe not found or staged app exe missing; Qt runtime files cannot be staged."
}

$I18nDir = Join-Path $StageDir "i18n"
New-Item -ItemType Directory -Force -Path $I18nDir | Out-Null
$Translation = Join-Path $BuildDir "i18n\smb-browser_ru.qm"
if (!(Test-Path $Translation)) {
    throw "Russian translation file was not built: $Translation"
}
Copy-Item -LiteralPath $Translation -Destination $I18nDir -Force
Copy-Item -LiteralPath (Join-Path $RootDir "LICENSE") -Destination $StageDir -Force
Copy-Item -LiteralPath (Join-Path $RootDir "NOTICE") -Destination $StageDir -Force
Copy-FirstPathMatch `
    -Patterns @("Qt5Keychain.dll", "qt5keychain.dll", "libqt5keychain.dll", "libsodium*.dll", "sodium*.dll") `
    -Destination $StageDir

$PackagesDir = Join-Path $BuildDir "packages"
New-Item -ItemType Directory -Force -Path $PackagesDir | Out-Null
$BuiltPackage = Join-Path $PackagesDir "smb-browser-$Version-windows-x86_64-portable.zip"
Remove-Item -Force $BuiltPackage -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $BuiltPackage

if (!$SkipSmoke) {
    & (Join-Path $PSScriptRoot "package-smoke-windows.ps1") `
        -PackagePath $BuiltPackage
}
