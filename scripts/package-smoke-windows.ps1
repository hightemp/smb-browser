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
    throw "Package not found. Build it first with: scripts\package-windows.ps1"
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

function Assert-PackagedFile {
    param(
        [string]$Pattern,
        [string]$Description
    )

    $Match = Get-ChildItem -Path $RootFs -Recurse -File -Filter $Pattern -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (!$Match) {
        throw "$Description not found in package ($Pattern)"
    }
}

function Assert-PackagedAnyFile {
    param(
        [string[]]$Patterns,
        [string]$Description
    )

    foreach ($Pattern in $Patterns) {
        $Match = Get-ChildItem -Path $RootFs -Recurse -File -Filter $Pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($Match) {
            return
        }
    }
    throw "$Description not found in package ($($Patterns -join ', '))"
}

Assert-PackagedFile "Qt5Core.dll" "Qt5 Core runtime"
Assert-PackagedFile "Qt5Gui.dll" "Qt5 Gui runtime"
Assert-PackagedFile "Qt5Widgets.dll" "Qt5 Widgets runtime"
Assert-PackagedFile "Qt5Sql.dll" "Qt5 SQL runtime"
Assert-PackagedFile "Qt5Svg.dll" "Qt5 SVG runtime"
Assert-PackagedFile "qwindows.dll" "Qt Windows platform plugin"
Assert-PackagedFile "qsqlite.dll" "Qt SQLite driver plugin"
Assert-PackagedAnyFile @("Qt5Keychain.dll", "qt5keychain.dll", "libqt5keychain.dll") `
    "QtKeychain runtime"
Assert-PackagedAnyFile @("libsodium*.dll", "sodium*.dll") "libsodium runtime"

$LegacySmb = Get-ChildItem -Path $RootFs -Recurse -File -Include `
    "libsmb2.dll","smb2.dll","smbclient.exe","samba*.dll" |
    Select-Object -ExpandProperty FullName
if ($LegacySmb.Count -gt 0) {
    throw "Unexpected legacy SMB runtime found in package: $($LegacySmb -join ', ')"
}

$DependencyText = ""
$Dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($Dumpbin) {
    $DependencyText = (& $Dumpbin.Source /DEPENDENTS $Exe) -join "`n"
} else {
    $Objdump = Get-Command llvm-objdump.exe -ErrorAction SilentlyContinue
    if ($Objdump) {
        $DependencyText = (& $Objdump.Source -p $Exe) -join "`n"
    }
}
if ($DependencyText -match "(?i)(libsmb2|smbclient|samba)") {
    throw "Executable links a legacy SMB runtime dependency: $DependencyText"
}

$Process = Start-Process -FilePath $Exe -ArgumentList "--smoke-close-ms=1000" -PassThru
Wait-Process -Id $Process.Id -Timeout 10 -ErrorAction SilentlyContinue
if (!$Process.HasExited) {
    Stop-Process -Id $Process.Id -Force
    throw "Application did not close cleanly during smoke test"
}
if ($Process.ExitCode -ne 0) {
    throw "Application smoke close exited with code $($Process.ExitCode)"
}

if (![string]::IsNullOrWhiteSpace($env:SMB_BROWSER_SMOKE_SERVER) -and
    ![string]::IsNullOrWhiteSpace($env:SMB_BROWSER_SMOKE_SHARE)) {
    $ListProcess = Start-Process -FilePath $Exe -ArgumentList "--smoke-smb-list" -PassThru
    Wait-Process -Id $ListProcess.Id -Timeout 30 -ErrorAction SilentlyContinue
    if (!$ListProcess.HasExited) {
        Stop-Process -Id $ListProcess.Id -Force
        throw "Application did not finish SMB list smoke"
    }
    if ($ListProcess.ExitCode -ne 0) {
        throw "Application SMB list smoke exited with code $($ListProcess.ExitCode)"
    }
}

Write-Host "Windows package smoke passed for $PackagePath"
