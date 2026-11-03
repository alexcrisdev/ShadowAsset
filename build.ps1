<#
.SYNOPSIS
    Compila ShadowAsset en Windows con GCC (MSYS2 UCRT64) y, opcionalmente, ejecuta las pruebas.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File build.ps1          # compila
    powershell -ExecutionPolicy Bypass -File build.ps1 -Test    # compila y ejecuta las pruebas
    powershell -ExecutionPolicy Bypass -File build.ps1 -Clean   # borra la carpeta build
#>
param(
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

$build = Join-Path $PSScriptRoot 'build'

if ($Clean) {
    if (Test-Path $build) { Remove-Item -Recurse -Force $build }
    Write-Host 'Carpeta build eliminada.'
    exit 0
}

$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc) {
    $gcc = 'C:\msys64\ucrt64\bin\gcc.exe'
    if (-not (Test-Path $gcc)) {
        Write-Error 'No se encontro gcc. Instale MSYS2 (UCRT64) o agreguelo al PATH.'
    }
    # Las DLL de tiempo de ejecucion de GCC viven junto al compilador.
    $env:PATH = (Split-Path $gcc) + ';' + $env:PATH
}

New-Item -ItemType Directory -Force $build | Out-Null

$flags = @('-std=c17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-Wshadow',
           '-Wconversion', '-Wformat=2', '-O2', '-g',
           '-D_FORTIFY_SOURCE=2', '-fstack-protector-strong')
# Modulos compartidos: todo src\ menos los dos puntos de entrada.
$lib = Get-ChildItem src\*.c | Where-Object { $_.Name -notin 'main.c', 'gui.c' } |
       ForEach-Object FullName

Write-Host "Compilando con $gcc"
& $gcc @flags src\main.c @lib -liphlpapi -o (Join-Path $build 'shadowasset.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'OK -> build\shadowasset.exe       (linea de comandos)'

# Interfaz grafica: recursos (manifest + version) con windres.
$windres = Join-Path (Split-Path $gcc) 'windres.exe'
$recursos = Join-Path $build 'gui_res.o'
& $windres res\gui.rc -O coff -o $recursos
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $gcc @flags -municode -mwindows src\gui.c @lib $recursos `
    -liphlpapi -lcomctl32 -lcomdlg32 -lshell32 -lole32 `
    -o (Join-Path $build 'shadowasset-gui.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'OK -> build\shadowasset-gui.exe   (interfaz grafica)'

if ($Test) {
    & $gcc @flags tests\pruebas.c @lib -liphlpapi -o (Join-Path $build 'pruebas.exe')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host 'Ejecutando pruebas (los errores en rojo de algunos casos son esperados)...'
    & (Join-Path $build 'pruebas.exe')
    exit $LASTEXITCODE
}
