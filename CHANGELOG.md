# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/); versionado [SemVer](https://semver.org/lang/es/).

## [1.1.0] - 2026-11-02

### Añadido
- Interfaz gráfica nativa de Windows (`shadowasset-gui.exe`, API Win32): abrir, descubrir, comparar, auditar, monitor y exportar sin escribir comandos.
- Tablas coloreadas por gravedad, detalle de alerta con doble clic y gateway autocompletado.
- *Manifest* con estilos visuales, *DPI awareness* y `asInvoker`; información de versión en el `.exe`.
- Módulo `registro.c`: los mensajes de error de los módulos van a la consola o a la ventana.
- La CI publica los ejecutables de Windows y verifica que solo dependen de DLL del sistema.

## [1.0.0] - 2026-11-02

### Añadido
- Alertas de seguridad: `IP_DUPLICADA`, `IP_REASIGNADA` (ALTA en la puerta de enlace), `POSIBLE_MISMO_EQUIPO` y `MAC_ALEATORIA`.
- Comando `monitor`: instantánea con fecha y comparación automática con la anterior.
- Detección automática de la puerta de enlace (`GetIpForwardTable2`) y opción `--gateway`.
- Control de alcance con `--subnet CIDR`.
- Informes JSON con versión, fecha UTC y SHA-256 de cada entrada.
- Opción `--sep ;` para Excel configurado en español.
- Analizador CSV conforme a RFC 4180: comillas, columnas en cualquier orden, columnas extra y separador `;`.
- Códigos de salida 0/1/2 para automatización.
- Pruebas unitarias (134 comprobaciones), `build.ps1`, `Makefile` y CI en GitHub Actions con ASan y UBSan.
- Documentación: arquitectura, modelo de amenazas, uso programado y notas de aprendizaje.

### Cambiado
- Los datos de ejemplo usan el rango de documentación RFC 7042.
- El ejecutable se genera en `build/`.

### Seguridad
- Límites de tamaño de archivo, línea, columnas y dispositivos; se rechazan bytes nulos y UTF-16.
- Compilación con `-D_FORTIFY_SOURCE=2` y `-fstack-protector-strong`.

## [0.6.0]
- `audit` frente al inventario de TI.

## [0.4.0] – [0.5.0]
- `discover`: lectura pasiva de la caché ARP de Windows. Instantáneas que no se sobrescriben.

## [0.2.0] – [0.3.0]
- `compare` entre dos CSV. Informe CSV protegido contra inyección de fórmulas.

## [0.1.0]
- Lectura y validación de un CSV `ip,mac,hostname`.
