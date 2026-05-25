# TensorRT for RTX 依存関係セットアップ
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OrtVersion = "1.24.4",
    [string]$TrtRtxRoot = ""
)

$ErrorActionPreference = "Stop"
$External = Join-Path $ProjectRoot "external"
$OrtDir = Join-Path $External "onnxruntime"
$TrtRtxDir = Join-Path $External "tensorrt-rtx"
$EpSrcDir = Join-Path $External "trt-rtx-ep-src"
$EpOutDir = Join-Path $External "trt-rtx-ep"

function Install-Ort {
    if (Test-Path (Join-Path $OrtDir "lib\onnxruntime.dll")) {
        Write-Host "ONNX Runtime already installed at $OrtDir"
        return
    }
    $zipName = "onnxruntime-win-x64-$OrtVersion.zip"
    $zipPath = Join-Path $env:TEMP $zipName
    $url = "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/$zipName"

    if (-not (Test-Path $zipPath)) {
        Write-Host "Downloading ONNX Runtime $OrtVersion..."
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    }

    $extractRoot = Join-Path $env:TEMP "ort-$OrtVersion"
    if (Test-Path $extractRoot) { Remove-Item $extractRoot -Recurse -Force }
    Expand-Archive -Path $zipPath -DestinationPath $extractRoot -Force

    $src = Join-Path $extractRoot "onnxruntime-win-x64-$OrtVersion"
    New-Item -ItemType Directory -Force -Path $OrtDir, (Join-Path $OrtDir "include"), (Join-Path $OrtDir "lib") | Out-Null
    Copy-Item "$src\include\*" (Join-Path $OrtDir "include") -Recurse -Force
    Copy-Item "$src\lib\*" (Join-Path $OrtDir "lib") -Force
    Write-Host "ONNX Runtime $OrtVersion installed to $OrtDir"
}

function Install-EpSource {
    if (-not (Test-Path (Join-Path $EpSrcDir ".git"))) {
        Write-Host "Cloning TensorRT-RTX-EP-ABI..."
        git clone --depth 1 https://github.com/NVIDIA/TensorRT-RTX-EP-ABI.git $EpSrcDir
    }
}

function Install-EpPlugin {
    $releaseZip = Join-Path $env:TEMP "TensorRT-RTX-EP-ABI-v0.1.zip"
    $releaseUrl = "https://github.com/NVIDIA/TensorRT-RTX-EP-ABI/releases/download/v0.1.0/TensorRT-RTX-EP-ABI-v0.1.zip"
    if (-not (Test-Path $releaseZip)) {
        Write-Host "Downloading TensorRT RTX EP ABI v0.1..."
        Invoke-WebRequest -Uri $releaseUrl -OutFile $releaseZip -UseBasicParsing
    }
    $extract = Join-Path $env:TEMP "trt-rtx-ep-v0.1"
    if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
    Expand-Archive -Path $releaseZip -DestinationPath $extract -Force
    $pluginDir = Get-ChildItem $extract -Recurse -Filter "onnxruntime_providers_nv_tensorrt_rtx.dll" | Select-Object -First 1
    if (-not $pluginDir) { throw "EP plugin DLL not found in release package" }
    New-Item -ItemType Directory -Force -Path $EpOutDir | Out-Null
    Copy-Item (Join-Path $pluginDir.DirectoryName "*.dll") $EpOutDir -Force
    Write-Host "EP plugin installed to $EpOutDir"
}

function Build-EpPlugin {
    if (-not $TrtRtxRoot) {
        if (Test-Path $TrtRtxDir) { $TrtRtxRoot = $TrtRtxDir }
    }
    if (-not $TrtRtxRoot -or -not (Test-Path $TrtRtxRoot)) {
        Write-Warning "TensorRT RTX SDK not found. Place SDK in $TrtRtxDir (include/ lib/ bin/) and rerun with -TrtRtxRoot."
        return
    }

    $buildDir = Join-Path $EpSrcDir "build"
    cmake -B $buildDir -G "Visual Studio 17 2022" -A x64 `
        -DONNXRUNTIME_ROOT="$OrtDir" `
        -DTRT_RTX_ROOT="$TrtRtxRoot"
    cmake --build $buildDir --config Release

    New-Item -ItemType Directory -Force -Path $EpOutDir | Out-Null
    Copy-Item (Join-Path $buildDir "Release\onnxruntime_providers_nv_tensorrt_rtx.dll") $EpOutDir -Force
    Write-Host "EP plugin installed to $EpOutDir"
}

Install-Ort
Install-EpSource
if (Test-Path (Join-Path $TrtRtxDir "include\NvInfer.h")) {
    Install-EpPlugin
} else {
    Write-Warning "TensorRT RTX SDK not found at $TrtRtxDir. Skipping EP plugin install."
}

Write-Host "Done."
