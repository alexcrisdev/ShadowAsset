<#
.SYNOPSIS
    Ejecuta "shadowasset monitor" y, si hay novedades, deja un aviso en el Visor de eventos.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\monitor.ps1 -Subred 192.168.1.0/24
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$Subred,
    [string]$Gateway = ''
)

$raiz      = Split-Path -Parent $PSScriptRoot
$exe       = Join-Path $raiz 'build\shadowasset.exe'
$historial = Join-Path $raiz 'data\historial'
$informes  = Join-Path $raiz 'reports'
$fecha     = Get-Date -Format 'yyyyMMdd_HHmmss'
$informe   = Join-Path $informes "monitor_$fecha.json"

if (-not (Test-Path $exe)) {
    Write-Error "No existe $exe. Compile primero con build.ps1."
    exit 1
}
New-Item -ItemType Directory -Force $historial, $informes | Out-Null

$argumentos = @('monitor', $historial, '--subnet', $Subred, '--report', $informe)
if ($Gateway) { $argumentos += @('--gateway', $Gateway) }

& $exe @argumentos
$codigo = $LASTEXITCODE

if ($codigo -eq 2) {
    $mensaje = "ShadowAsset detecto cambios o alertas en $Subred. Informe: $informe"
    try {
        Write-EventLog -LogName Application -Source 'ShadowAsset' -EventId 2 `
            -EntryType Warning -Message $mensaje -ErrorAction Stop
    } catch {
        # El origen de eventos no esta registrado: basta con la salida por consola.
    }
    Write-Warning $mensaje
}
exit $codigo
