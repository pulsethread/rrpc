$ErrorActionPreference = 'Stop'
$sourceRoot = $PSScriptRoot
$buildRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot '../Build-PC'))
& cmake -S $sourceRoot -B $buildRoot -DPSX_DEBUG_TOOLS=ON
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
& cmake --build $buildRoot --config Release --target rage-racer generate-data --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Write-Output 'Executables: Build-PC/bin/Release'
