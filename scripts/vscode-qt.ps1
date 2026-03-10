param(
    [ValidateSet("Diagnose", "Build", "Run")]
    [string]$Action = "Diagnose",
    [string]$BuildDir = "build-vscode"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$snapshotPath = Join-Path $workspace ".vscode\\toolchain.snapshot.json"
$projectFile = Join-Path $workspace "newudp.pro"
$buildPath = Join-Path $workspace $BuildDir
$exePath = [System.IO.Path]::Combine($buildPath, "debug", "newudp.exe")

function Resolve-FirstExistingPath {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Resolve-FirstByPattern {
    param([string[]]$Patterns)

    foreach ($pattern in $Patterns) {
        if ([string]::IsNullOrWhiteSpace($pattern)) {
            continue
        }

        $items = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
        if ($items) {
            return $items[0].FullName
        }
    }

    return $null
}

function Resolve-Toolchain {
    $qmake = Resolve-FirstExistingPath @(
        (Join-Path $workspace ".local\\toolchain\\Qt\\5.15.2\\mingw81_64\\bin\\qmake.exe"),
        $env:QT_QMAKE,
        "C:\\Qt\\5.15.2\\mingw81_64\\bin\\qmake.exe",
        "C:\\Qt\\6.5.3\\mingw_64\\bin\\qmake.exe",
        "D:\\Qt\\5.15.2\\mingw81_64\\bin\\qmake.exe",
        "D:\\Qt\\6.5.3\\mingw_64\\bin\\qmake.exe"
    )

    $mingwMake = $null
    $gdb = $null

    if ($qmake) {
        $qtBinDir = Split-Path -Parent $qmake
        $qtArchDir = Split-Path -Parent $qtBinDir
        $qtVersionDir = Split-Path -Parent $qtArchDir
        $qtRoot = Split-Path -Parent $qtVersionDir
        $qtToolsDir = Join-Path $qtRoot "Tools"

        $mingwMake = Resolve-FirstByPattern @(
            (Join-Path $qtToolsDir "mingw*\\bin\\mingw32-make.exe")
        )
        $gdb = Resolve-FirstByPattern @(
            (Join-Path $qtToolsDir "mingw*\\bin\\gdb.exe")
        )
    }

    if (-not $mingwMake) {
        $mingwMake = Resolve-FirstExistingPath @(
            (Join-Path $workspace ".local\\toolchain\\Qt\\Tools\\mingw810_64\\bin\\mingw32-make.exe"),
            $env:MINGW_MAKE,
            "C:\\msys64\\mingw64\\bin\\mingw32-make.exe",
            "D:\\msys64\\mingw64\\bin\\mingw32-make.exe"
        )
    }

    if (-not $gdb) {
        $gdb = Resolve-FirstExistingPath @(
            (Join-Path $workspace ".local\\toolchain\\Qt\\Tools\\mingw810_64\\bin\\gdb.exe"),
            $env:GDB_PATH,
            "C:\\msys64\\mingw64\\bin\\gdb.exe",
            "D:\\msys64\\mingw64\\bin\\gdb.exe"
        )
    }

    $opencvRoot = Resolve-FirstExistingPath @(
        (Join-Path $workspace ".local\\toolchain\\OpenCV-3.4.8-x64"),
        $env:OPENCV_ROOT,
        "D:\\OpenCV-MinGW-1",
        "C:\\OpenCV-MinGW-1"
    )

    $toolchain = [ordered]@{
        Workspace = $workspace
        QMake = $qmake
        MingwMake = $mingwMake
        Gdb = $gdb
        OpenCVRoot = $opencvRoot
        BuildDir = $buildPath
        ExePath = $exePath
    }

    Set-Content -Path $snapshotPath -Encoding UTF8 -Value ($toolchain | ConvertTo-Json)
    return $toolchain
}

function Assert-RequiredTools {
    param($Toolchain)

    $missing = New-Object System.Collections.Generic.List[string]

    if (-not $Toolchain.QMake) {
        $missing.Add("qmake")
    }
    if (-not $Toolchain.MingwMake) {
        $missing.Add("mingw32-make")
    }
    if (-not $Toolchain.OpenCVRoot) {
        $missing.Add("OpenCV root (set OPENCV_ROOT or install to D:\\OpenCV-MinGW-1)")
    }

    if ($missing.Count -gt 0) {
        throw "Missing local toolchain components: $($missing -join ', '). Snapshot: $snapshotPath"
    }
}

function Invoke-Diagnose {
    $toolchain = Resolve-Toolchain
    Write-Host "Workspace   : $($toolchain.Workspace)"
    Write-Host "QMake       : $($toolchain.QMake)"
    Write-Host "MingwMake   : $($toolchain.MingwMake)"
    Write-Host "Gdb         : $($toolchain.Gdb)"
    Write-Host "OpenCVRoot  : $($toolchain.OpenCVRoot)"
    Write-Host "BuildDir    : $($toolchain.BuildDir)"
    Write-Host "ExePath     : $($toolchain.ExePath)"
    Write-Host "Snapshot    : $snapshotPath"
}

function Invoke-Build {
    $toolchain = Resolve-Toolchain
    Assert-RequiredTools $toolchain

    if (Test-Path $toolchain.BuildDir) {
        Remove-Item -Recurse -Force $toolchain.BuildDir
    }
    New-Item -ItemType Directory -Force -Path $toolchain.BuildDir | Out-Null

    $qtBin = Split-Path -Parent $toolchain.QMake
    $mingwBin = Split-Path -Parent $toolchain.MingwMake
    $opencvBin = Join-Path $toolchain.OpenCVRoot "x64\\mingw\\bin"
    $env:PATH = "$mingwBin;$qtBin;$opencvBin;$env:PATH"
    $env:OPENCV_ROOT = $toolchain.OpenCVRoot

    Push-Location $toolchain.BuildDir
    try {
        & $toolchain.QMake $projectFile "CONFIG+=debug" "OPENCV_ROOT=$($toolchain.OpenCVRoot)"
        if ($LASTEXITCODE -ne 0) {
            throw "qmake failed with exit code $LASTEXITCODE"
        }

        & $toolchain.MingwMake
        if ($LASTEXITCODE -ne 0) {
            throw "mingw32-make failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-Run {
    $toolchain = Resolve-Toolchain

    if (-not (Test-Path $exePath)) {
        Invoke-Build
    }

    $qtBin = Split-Path -Parent $toolchain.QMake
    $mingwBin = Split-Path -Parent $toolchain.MingwMake
    $opencvBin = Join-Path $toolchain.OpenCVRoot "x64\\mingw\\bin"
    $env:PATH = "$mingwBin;$qtBin;$opencvBin;$env:PATH"

    & $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "Application exited with code $LASTEXITCODE"
    }
}

switch ($Action) {
    "Diagnose" { Invoke-Diagnose }
    "Build" { Invoke-Build }
    "Run" { Invoke-Run }
}
