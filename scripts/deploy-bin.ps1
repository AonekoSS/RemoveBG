# bin/ へ実行時 DLL をコピー
param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$BinDir = Join-Path $ProjectRoot "bin"
$OrtLib = Join-Path $ProjectRoot "external\onnxruntime\lib"
$EpDir = Join-Path $ProjectRoot "external\trt-rtx-ep"
$TrtRtxBin = Join-Path $ProjectRoot "external\tensorrt-rtx\bin"

New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

Copy-Item (Join-Path $OrtLib "onnxruntime.dll") $BinDir -Force
Copy-Item (Join-Path $OrtLib "onnxruntime_providers_shared.dll") $BinDir -Force

foreach ($obsolete in @("onnxruntime_providers_cuda.dll", "onnxruntime_providers_tensorrt.dll")) {
    $path = Join-Path $BinDir $obsolete
    if (Test-Path $path) { Remove-Item $path -Force }
}

$epDll = Join-Path $EpDir "onnxruntime_providers_nv_tensorrt_rtx.dll"
if (Test-Path $epDll) {
    Copy-Item $epDll $BinDir -Force
}

foreach ($extra in @("tensorrt_plugins.dll")) {
    $path = Join-Path $EpDir $extra
    if (Test-Path $path) { Copy-Item $path $BinDir -Force }
}

if (Test-Path $TrtRtxBin) {
    Copy-Item (Join-Path $TrtRtxBin "*.dll") $BinDir -Force
}

Write-Host "Deployed runtime DLLs to $BinDir"
