param(
    [ValidateSet("Diagnose", "Build", "Run", "Package")]
    [string]$Action = "Diagnose",
    [string]$BuildDir = "build-vscode",
    [switch]$Demo
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$snapshotPath = Join-Path $workspace ".vscode\\toolchain.snapshot.json"
$projectFile = Join-Path $workspace "newudp.pro"
$buildPath = Join-Path $workspace $BuildDir
$exePath = [System.IO.Path]::Combine($buildPath, "debug", "newudp.exe")
$distRoot = Join-Path $workspace "dist"

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

        Invoke-DeployRuntime -Toolchain $toolchain
    }
    finally {
        Pop-Location
    }
}

function Copy-IfExists {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (Test-Path $Source) {
        Copy-Item -Force $Source $Destination
    }
}

function Invoke-DeployRuntime {
    param($Toolchain)

    $exeDir = Split-Path -Parent $Toolchain.ExePath
    $qtBin = Split-Path -Parent $Toolchain.QMake
    $mingwBin = Split-Path -Parent $Toolchain.MingwMake
    $opencvBin = Join-Path $Toolchain.OpenCVRoot "x64\\mingw\\bin"
    $windeployqt = Join-Path $qtBin "windeployqt.exe"

    if (-not (Test-Path $Toolchain.ExePath)) {
        throw "Cannot deploy runtime because executable was not built: $($Toolchain.ExePath)"
    }

    if (Test-Path $windeployqt) {
        & $windeployqt --debug --no-compiler-runtime --no-translations $Toolchain.ExePath
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt failed with exit code $LASTEXITCODE"
        }
    }

    $opencvDlls = @(
        "libopencv_core348.dll",
        "libopencv_dnn348.dll",
        "libopencv_imgproc348.dll",
        "libopencv_highgui348.dll",
        "libopencv_imgcodecs348.dll",
        "libopencv_videoio348.dll",
        "opencv_ffmpeg348_64.dll"
    )

    foreach ($dll in $opencvDlls) {
        Copy-IfExists -Source (Join-Path $opencvBin $dll) -Destination $exeDir
    }

    $mingwDlls = @(
        "libgcc_s_seh-1.dll",
        "libgomp-1.dll",
        "libstdc++-6.dll",
        "libwinpthread-1.dll"
    )

    foreach ($dll in $mingwDlls) {
        Copy-IfExists -Source (Join-Path $mingwBin $dll) -Destination $exeDir
    }

    $modelDir = Join-Path $workspace "models"
    if (Test-Path $modelDir) {
        $destModelDir = Join-Path $exeDir "models"
        New-Item -ItemType Directory -Force -Path $destModelDir | Out-Null
        Get-ChildItem -Path $modelDir -Filter *.onnx | ForEach-Object {
            Copy-Item -Force $_.FullName $destModelDir
        }
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

    $runArgs = @()
    if ($Demo) {
        $runArgs += "--demo"
    }

    & $exePath @runArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Application exited with code $LASTEXITCODE"
    }
}

function Invoke-Package {
    $toolchain = Resolve-Toolchain

    if (-not (Test-Path $exePath)) {
        Invoke-Build
        $toolchain = Resolve-Toolchain
    }

    $packageName = "Post-Train-UDP-Vision-Console"
    $packageDir = Join-Path $distRoot $packageName
    $zipPath = Join-Path $distRoot "$packageName.zip"
    $exeDir = Split-Path -Parent $toolchain.ExePath

    if (Test-Path $packageDir) {
        Remove-Item -Recurse -Force $packageDir
    }
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

    Copy-Item -Force (Join-Path $exeDir "newudp.exe") $packageDir
    Get-ChildItem -Path $exeDir -Filter *.dll | ForEach-Object {
        Copy-Item -Force $_.FullName $packageDir
    }

    @("bearer", "iconengines", "imageformats", "platforms", "styles") | ForEach-Object {
        $pluginDir = Join-Path $exeDir $_
        if (Test-Path $pluginDir) {
            Copy-Item -Recurse -Force $pluginDir $packageDir
        }
    }

    $modelDir = Join-Path $exeDir "models"
    if (Test-Path $modelDir) {
        Copy-Item -Recurse -Force $modelDir $packageDir
    }

    $readme = @"
Post-Train UDP Vision Console

Run:
  newudp.exe

Demo mode:
  newudp.exe --demo

This package already includes the required Qt, OpenCV, MinGW runtime DLLs, and ONNX model files.
"@
    Set-Content -Path (Join-Path $packageDir "README.txt") -Value $readme -Encoding ASCII

    if (Test-Path $zipPath) {
        Remove-Item -Force $zipPath
    }

    Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath

    Write-Host "PackageDir  : $packageDir"
    Write-Host "PackageZip  : $zipPath"
}

switch ($Action) {
    "Diagnose" { Invoke-Diagnose }
    "Build" { Invoke-Build }
    "Run" { Invoke-Run }
    "Package" { Invoke-Package }
}
