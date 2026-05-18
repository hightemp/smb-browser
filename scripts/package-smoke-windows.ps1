param(
    [string]$PackagePath = ""
)

$ErrorActionPreference = "Stop"
$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Get-ChildItem -Path (Join-Path $RootDir "tmp") `
        -Recurse -File -Include "smb-browser-*.zip","smb-browser_*.zip" `
        | Sort-Object LastWriteTime -Descending `
        | Select-Object -First 1 -ExpandProperty FullName
}

if ([string]::IsNullOrWhiteSpace($PackagePath) -or !(Test-Path $PackagePath)) {
    throw "Package not found. Build it first with: cmake --build tmp\package-windows --target package"
}

$SmokeDir = Join-Path $RootDir "tmp\package-smoke-windows"
$RootFs = Join-Path $SmokeDir "rootfs"
Remove-Item -Recurse -Force $SmokeDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $RootFs | Out-Null

Expand-Archive -LiteralPath $PackagePath -DestinationPath $RootFs -Force

$Exe = Get-ChildItem -Path $RootFs -Recurse -File -Filter "smb-browser.exe" |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($Exe)) {
    throw "smb-browser.exe not found in package"
}

$Translation = Get-ChildItem -Path $RootFs -Recurse -File -Filter "smb-browser_ru.qm" |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($Translation)) {
    throw "Russian translation file not found in package"
}

$Libsmb2 = Get-ChildItem -Path $RootFs -Recurse -File -Include "libsmb2.dll","smb2.dll" |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($Libsmb2)) {
    Write-Warning "libsmb2 DLL not found in package; verify runtime deployment manually."
}

$Process = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 3
if ($Process.HasExited) {
    if ($Process.ExitCode -ne 0) {
        throw "Application exited early with code $($Process.ExitCode)"
    }
} else {
    Stop-Process -Id $Process.Id -Force
}

Write-Host "Windows package smoke passed for $PackagePath"
