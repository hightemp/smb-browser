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
$WinDeployQt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if ($WinDeployQt -and (Test-Path $Exe)) {
    & $WinDeployQt.Source --no-translations $Exe
} else {
    Write-Warning "windeployqt.exe not found or app exe missing; ensure Qt runtime files are staged before publishing."
}

cmake --build $BuildDir --target package --config $BuildType

if (!$SkipSmoke) {
    & (Join-Path $PSScriptRoot "package-smoke-windows.ps1")
}
