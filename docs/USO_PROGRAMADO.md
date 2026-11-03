# Monitorización programada en Windows

`monitor` está pensado para ejecutarse de forma periódica. En cada ejecución:

1. Detecta la puerta de enlace y la vigila (si no se indica `--gateway`).
2. Lee la caché ARP dentro del alcance (`--subnet`).
3. Guarda `snapshot_AAAAMMDD_HHMMSS.csv` en la carpeta indicada.
4. La compara con la instantánea anterior y devuelve `2` si hay cambios o alertas.

El script [scripts/monitor.ps1](../scripts/monitor.ps1) lo hace y, si hay novedades, deja un aviso en el Visor de eventos de Windows.

## 1. Probarlo a mano

```powershell
powershell -ExecutionPolicy Bypass -File scripts\monitor.ps1 -Subred 192.168.1.0/24
```

La primera ejecución crea la línea base; a partir de la segunda, compara.

## 2. (Opcional) Registrar el origen de eventos

Una sola vez, en una consola **como administrador**:

```powershell
New-EventLog -LogName Application -Source 'ShadowAsset'
```

Si no lo hace, el script sigue funcionando: simplemente no escribe en el Visor de eventos.

## 3. Programarlo cada hora

```powershell
$script = (Resolve-Path .\scripts\monitor.ps1).Path
schtasks /Create /TN "ShadowAsset monitor" /SC HOURLY /F `
  /TR "powershell -NoProfile -ExecutionPolicy Bypass -File `"$script`" -Subred 192.168.1.0/24"
```

Consultar o eliminar la tarea:

```powershell
schtasks /Query /TN "ShadowAsset monitor"
schtasks /Delete /TN "ShadowAsset monitor" /F
```

## Recomendaciones

- La caché ARP se refresca con el uso normal de la red. Un intervalo de 15–60 minutos es razonable.
- Las instantáneas y los informes contienen información sensible de la red y están en `.gitignore`.
- Purgue periódicamente las instantáneas antiguas según su política de retención.
