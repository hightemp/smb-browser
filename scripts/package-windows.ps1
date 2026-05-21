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

function Add-CMakePrefix {
    param(
        [string]$Prefix
    )

    if ([string]::IsNullOrWhiteSpace($Prefix) -or !(Test-Path $Prefix)) {
        return
    }

    $ResolvedPrefix = (Resolve-Path -LiteralPath $Prefix).Path
    $PrefixBin = Join-Path $ResolvedPrefix "bin"
    $PrefixLib = Join-Path $ResolvedPrefix "lib"

    $env:CMAKE_PREFIX_PATH = if ([string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
        $ResolvedPrefix
    } else {
        "$ResolvedPrefix;$env:CMAKE_PREFIX_PATH"
    }
    if (Test-Path $PrefixBin) {
        $env:PATH = "$PrefixBin;$env:PATH"
    }
    if (Test-Path $PrefixLib) {
        $env:PATH = "$PrefixLib;$env:PATH"
    }
}

function Resolve-FirstExistingPath {
    param(
        [string[]]$Paths
    )

    foreach ($Path in $Paths) {
        if (![string]::IsNullOrWhiteSpace($Path) -and (Test-Path $Path)) {
            return (Resolve-Path -LiteralPath $Path).Path
        }
    }
    return ""
}

$MsysUcrtPrefix = if (![string]::IsNullOrWhiteSpace($env:MSYSTEM_PREFIX)) {
    $env:MSYSTEM_PREFIX
} else {
    "C:\msys64\ucrt64"
}
Add-CMakePrefix $MsysUcrtPrefix
Add-CMakePrefix $env:QTKEYCHAIN_PREFIX

$MsysUcrtBin = Join-Path $MsysUcrtPrefix "bin"
if (Test-Path $MsysUcrtBin) {
    $UcrtCc = Resolve-FirstExistingPath @(
        (Join-Path $MsysUcrtBin "cc.exe"),
        (Join-Path $MsysUcrtBin "gcc.exe"),
        (Join-Path $MsysUcrtBin "x86_64-w64-mingw32-gcc.exe")
    )
    $UcrtCxx = Resolve-FirstExistingPath @(
        (Join-Path $MsysUcrtBin "c++.exe"),
        (Join-Path $MsysUcrtBin "g++.exe"),
        (Join-Path $MsysUcrtBin "x86_64-w64-mingw32-g++.exe")
    )
    if (![string]::IsNullOrWhiteSpace($UcrtCc)) {
        $env:CC = $UcrtCc
    } elseif (![string]::IsNullOrWhiteSpace($env:CC) -and !(Test-Path $env:CC)) {
        Remove-Item Env:CC -ErrorAction SilentlyContinue
    }
    if (![string]::IsNullOrWhiteSpace($UcrtCxx)) {
        $env:CXX = $UcrtCxx
    } elseif (![string]::IsNullOrWhiteSpace($env:CXX) -and !(Test-Path $env:CXX)) {
        Remove-Item Env:CXX -ErrorAction SilentlyContinue
    }
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

$CMakeArgs = @(
    "-S", $RootDir,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DSMB_BROWSER_WITH_LIBSMB2=OFF",
    "-DSMB_BROWSER_WITH_NATIVE_SMB=ON",
    "-DSMB_BROWSER_ENABLE_DOCKER_SAMBA_TESTS=OFF"
)
if ($SkipTests) {
    $CMakeArgs += "-DBUILD_TESTING=OFF"
}
if (![string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
    $CMakeArgs += "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
}
if (![string]::IsNullOrWhiteSpace($env:CXX) -and (Test-Path $env:CXX)) {
    $CMakeArgs += "-DCMAKE_CXX_COMPILER=$env:CXX"
}

Invoke-CheckedCommand "cmake" $CMakeArgs

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
    if (![string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
        foreach ($PrefixEntry in ($env:CMAKE_PREFIX_PATH -split ';')) {
            if (![string]::IsNullOrWhiteSpace($PrefixEntry)) {
                $PathEntries += (Join-Path $PrefixEntry "bin")
                $PathEntries += (Join-Path $PrefixEntry "lib")
            }
        }
    }
    foreach ($Pattern in $Patterns) {
        foreach ($Entry in $PathEntries) {
            if ([string]::IsNullOrWhiteSpace($Entry) -or !(Test-Path $Entry)) {
                continue
            }
            $Match = Get-ChildItem -Path $Entry -File -Filter $Pattern -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($Match) {
                Copy-Item -LiteralPath $Match.FullName -Destination $Destination -Force
                return $true
            }
        }
    }
    return $false
}

function Get-RuntimeSearchRoots {
    $Roots = New-Object System.Collections.Generic.List[string]
    foreach ($Entry in ($env:PATH -split [System.IO.Path]::PathSeparator)) {
        if (![string]::IsNullOrWhiteSpace($Entry) -and (Test-Path $Entry)) {
            $Roots.Add((Resolve-Path -LiteralPath $Entry).Path)
        }
    }
    if (![string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
        foreach ($PrefixEntry in ($env:CMAKE_PREFIX_PATH -split ';')) {
            if ([string]::IsNullOrWhiteSpace($PrefixEntry) -or !(Test-Path $PrefixEntry)) {
                continue
            }
            $ResolvedPrefix = (Resolve-Path -LiteralPath $PrefixEntry).Path
            foreach ($Subdir in @("bin", "lib", "plugins", "share\qt5\plugins", "lib\qt5\plugins")) {
                $Candidate = Join-Path $ResolvedPrefix $Subdir
                if (Test-Path $Candidate) {
                    $Roots.Add((Resolve-Path -LiteralPath $Candidate).Path)
                }
            }
        }
    }
    return $Roots | Select-Object -Unique
}

function Find-RuntimeFile {
    param(
        [string]$Pattern,
        [string[]]$Roots
    )

    foreach ($Root in $Roots) {
        $DirectMatch = Get-ChildItem -Path $Root -File -Filter $Pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($DirectMatch) {
            return $DirectMatch.FullName
        }
    }
    foreach ($Root in $Roots) {
        if ($Root -notmatch '(?i)(\\|/)plugins$|qt5(\\|/)plugins') {
            continue
        }
        $RecursiveMatch = Get-ChildItem -Path $Root -Recurse -File -Filter $Pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($RecursiveMatch) {
            return $RecursiveMatch.FullName
        }
    }
    return ""
}

function Copy-RequiredRuntimeFile {
    param(
        [string]$Pattern,
        [string]$Destination,
        [string[]]$Roots,
        [string]$Description
    )

    $Match = Find-RuntimeFile -Pattern $Pattern -Roots $Roots
    if ([string]::IsNullOrWhiteSpace($Match)) {
        throw "$Description not found ($Pattern)"
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -LiteralPath $Match -Destination $Destination -Force
}

function Copy-OptionalRuntimeFile {
    param(
        [string]$Pattern,
        [string]$Destination,
        [string[]]$Roots
    )

    $Match = Find-RuntimeFile -Pattern $Pattern -Roots $Roots
    if (![string]::IsNullOrWhiteSpace($Match)) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        Copy-Item -LiteralPath $Match -Destination $Destination -Force
        return $true
    }
    return $false
}

function Get-ImportedDllNames {
    param(
        [string]$Binary
    )

    $Objdump = Get-Command objdump.exe -ErrorAction SilentlyContinue
    if (!$Objdump) {
        $Objdump = Get-Command llvm-objdump.exe -ErrorAction SilentlyContinue
    }
    if (!$Objdump) {
        return @()
    }

    $Output = & $Objdump.Source -p $Binary 2>$null
    return $Output |
        Select-String -Pattern 'DLL Name: (.+)$' |
        ForEach-Object { $_.Matches[0].Groups[1].Value.Trim() } |
        Select-Object -Unique
}

function Test-SystemDll {
    param(
        [string]$Name
    )

    $LowerName = $Name.ToLowerInvariant()
    if ($LowerName.StartsWith("api-ms-win-") -or $LowerName.StartsWith("ext-ms-")) {
        return $true
    }

    $SystemDlls = @(
        "advapi32.dll",
        "bcrypt.dll",
        "comctl32.dll",
        "comdlg32.dll",
        "crypt32.dll",
        "dnsapi.dll",
        "dwmapi.dll",
        "gdi32.dll",
        "imm32.dll",
        "iphlpapi.dll",
        "kernel32.dll",
        "mpr.dll",
        "msvcp_win.dll",
        "msvcrt.dll",
        "ncrypt.dll",
        "netapi32.dll",
        "ntdll.dll",
        "ole32.dll",
        "oleaut32.dll",
        "rpcrt4.dll",
        "secur32.dll",
        "setupapi.dll",
        "shell32.dll",
        "shlwapi.dll",
        "ucrtbase.dll",
        "user32.dll",
        "uuid.dll",
        "uxtheme.dll",
        "version.dll",
        "winmm.dll",
        "ws2_32.dll",
        "wtsapi32.dll"
    )
    return $SystemDlls -contains $LowerName
}

function Copy-RecursiveRuntimeDependencies {
    param(
        [string]$InitialBinary,
        [string]$Destination,
        [string[]]$Roots,
        [hashtable]$SeenDlls,
        [hashtable]$SeenBinaries
    )

    $Queue = New-Object System.Collections.Queue
    $Queue.Enqueue($InitialBinary)

    while ($Queue.Count -gt 0) {
        $Binary = [string]$Queue.Dequeue()
        if (!(Test-Path $Binary)) {
            continue
        }
        $BinaryKey = (Resolve-Path -LiteralPath $Binary).Path.ToLowerInvariant()
        if ($SeenBinaries.ContainsKey($BinaryKey)) {
            continue
        }
        $SeenBinaries[$BinaryKey] = $true

        foreach ($DllName in Get-ImportedDllNames -Binary $Binary) {
            if (Test-SystemDll $DllName) {
                continue
            }
            $Key = $DllName.ToLowerInvariant()
            if ($SeenDlls.ContainsKey($Key)) {
                continue
            }
            $SeenDlls[$Key] = $true

            $Existing = Join-Path $Destination $DllName
            if (!(Test-Path $Existing)) {
                $Source = Find-RuntimeFile -Pattern $DllName -Roots $Roots
                if ([string]::IsNullOrWhiteSpace($Source)) {
                    Write-Warning "Runtime dependency not found: $DllName"
                    continue
                }
                Copy-Item -LiteralPath $Source -Destination $Destination -Force
            }
            if (Test-Path $Existing) {
                $Queue.Enqueue($Existing)
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
    & $WinDeployQt.Source @(
        "--compiler-runtime",
        "--no-opengl-sw",
        "--no-translations",
        $StageExe
    )
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "$($WinDeployQt.Source) failed with exit code $LASTEXITCODE; continuing with manual runtime staging."
    }
} else {
    Write-Warning "windeployqt.exe not found or staged app exe missing; continuing with manual runtime staging."
}

$RuntimeRoots = @(Get-RuntimeSearchRoots)
Copy-RequiredRuntimeFile -Pattern "qwindows.dll" `
    -Destination (Join-Path $StageDir "platforms") `
    -Roots $RuntimeRoots `
    -Description "Qt Windows platform plugin"
Copy-RequiredRuntimeFile -Pattern "qsqlite.dll" `
    -Destination (Join-Path $StageDir "sqldrivers") `
    -Roots $RuntimeRoots `
    -Description "Qt SQLite driver plugin"
Copy-OptionalRuntimeFile -Pattern "qsvgicon.dll" `
    -Destination (Join-Path $StageDir "iconengines") `
    -Roots $RuntimeRoots | Out-Null
Copy-OptionalRuntimeFile -Pattern "qsvg.dll" `
    -Destination (Join-Path $StageDir "imageformats") `
    -Roots $RuntimeRoots | Out-Null

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
    -Destination $StageDir | Out-Null
$RuntimeDependencySeenDlls = @{}
$RuntimeDependencySeenBinaries = @{}
foreach ($RuntimeBinary in Get-ChildItem -Path $StageDir -Recurse -File -Include "*.exe", "*.dll") {
    Copy-RecursiveRuntimeDependencies -InitialBinary $RuntimeBinary.FullName `
        -Destination $StageDir `
        -Roots $RuntimeRoots `
        -SeenDlls $RuntimeDependencySeenDlls `
        -SeenBinaries $RuntimeDependencySeenBinaries
}

$PackagesDir = Join-Path $BuildDir "packages"
New-Item -ItemType Directory -Force -Path $PackagesDir | Out-Null
$BuiltPackage = Join-Path $PackagesDir "smb-browser-$Version-windows-x86_64-portable.zip"
Remove-Item -Force $BuiltPackage -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $BuiltPackage

if (!$SkipSmoke) {
    & (Join-Path $PSScriptRoot "package-smoke-windows.ps1") `
        -PackagePath $BuiltPackage
}
