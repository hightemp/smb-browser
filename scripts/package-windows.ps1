param(
    [string]$BuildDir = "tmp\package-windows",
    [string]$Generator = "Ninja",
    [string]$BuildType = "Release",
    [switch]$SkipTests,
    [switch]$SkipSmoke
)

$ErrorActionPreference = "Stop"
$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
if (![System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $RootDir $BuildDir
}

cmake -S $RootDir -B $BuildDir -G $Generator `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DSMB_BROWSER_WITH_LIBSMB2=OFF `
    -DSMB_BROWSER_WITH_NATIVE_SMB=ON `
    -DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF

cmake --build $BuildDir --config $BuildType

if (!$SkipTests) {
    ctest --test-dir $BuildDir --output-on-failure -C $BuildType
}

$Exe = Join-Path $BuildDir "src\app\smb-browser.exe"
if (!(Test-Path $Exe)) {
    throw "Built executable not found: $Exe"
}

$WinDeployQt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (!$WinDeployQt) {
    $WinDeployQt = Get-Command windeployqt-qt5.exe -ErrorAction SilentlyContinue
}
if ($WinDeployQt -and (Test-Path $Exe)) {
    & $WinDeployQt.Source --compiler-runtime --no-translations $Exe
} else {
    Write-Warning "windeployqt.exe not found or app exe missing; ensure Qt runtime files are staged before publishing."
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

$AppDir = Split-Path -Parent $Exe
$I18nDir = Join-Path $AppDir "i18n"
New-Item -ItemType Directory -Force -Path $I18nDir | Out-Null
$Translation = Join-Path $BuildDir "i18n\smb-browser_ru.qm"
if (!(Test-Path $Translation)) {
    throw "Russian translation file was not built: $Translation"
}
Copy-Item -LiteralPath $Translation -Destination $I18nDir -Force
Copy-Item -LiteralPath (Join-Path $RootDir "LICENSE") -Destination $AppDir -Force
Copy-Item -LiteralPath (Join-Path $RootDir "NOTICE") -Destination $AppDir -Force
Copy-FirstPathMatch `
    -Patterns @("Qt5Keychain.dll", "qt5keychain.dll", "libqt5keychain.dll", "libsodium*.dll", "sodium*.dll") `
    -Destination $AppDir

$PackagesDir = Join-Path $BuildDir "packages"
New-Item -ItemType Directory -Force -Path $PackagesDir | Out-Null
$BuiltPackage = Join-Path $PackagesDir "smb-browser-$BuildType-windows-portable.zip"
Remove-Item -Force $BuiltPackage -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $AppDir "*") -DestinationPath $BuiltPackage

if (!$SkipSmoke) {
    & (Join-Path $PSScriptRoot "package-smoke-windows.ps1") `
        -PackagePath $BuiltPackage
}
