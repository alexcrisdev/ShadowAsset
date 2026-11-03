# Arquitectura

## Flujo general

```text
main.c  →  leer opciones  →  obtener listas  →  analizar  →  informar  →  código de salida
                              │                   │            │
                              ├ csv_cargar        ├ analizar   ├ consola
                              └ descubrir_vecinos └ alertas    ├ CSV
                                                               └ JSON
```

1. **Obtener listas.** Cada fuente produce una `ListaDispositivos` validada, normalizada y **ordenada por MAC**.
2. **Analizar.** Dos listas ordenadas se recorren a la vez, como en el paso de mezcla de *merge sort*. Cada MAC cae en una de cuatro categorías en tiempo O(n + m).
3. **Alertas.** Se generan sobre el resultado: conflictos de IP, IP que cambia de MAC, MAC aleatoria y posibles equipos repetidos.
4. **Informar.** Por consola siempre; a archivo con `--report`.

## Decisiones técnicas

### La MAC como clave
La IP cambia con DHCP; la MAC es más estable. Así, un equipo cuya IP cambió aparece como `CAMBIO` y no como "uno nuevo y otro desaparecido". Como la MAC no es infalible (aleatorización, clonación), hay alertas específicas para esos casos.

### Comparación por fusión en vez de búsqueda anidada
Comparar cada elemento con todos sería O(n·m). Ordenar (O(n log n)) y fusionar (O(n + m)) escala a los 100 000 dispositivos permitidos.

### Índice por IP con búsqueda binaria
Para las alertas basadas en IP se construye un arreglo de punteros ordenado por el valor numérico de la IP (no por el texto: `"10.0.0.9" > "10.0.0.10"` como texto). La búsqueda es O(log n).

### Lectura completa del archivo en memoria
`csv_cargar` lee el archivo entero (máximo 16 MB), calcula el SHA-256 y analiza **ese mismo búfer**. Así la huella del informe corresponde exactamente a lo analizado.

### Analizador CSV propio
Sigue RFC 4180 en una sola línea: comillas, `""` escapadas y el separador `,` o `;` detectado en la cabecera. Se escribió a mano porque en C estándar no hay uno y porque permite controlar los límites. Lo que no entiende lo rechaza con el número de línea; nunca adivina.

### Sin estado global
Cada módulo recibe y devuelve estructuras explícitas. Eso facilita las pruebas: `csv_parsear_texto` funciona sobre memoria, sin archivos.

### Dos puntos de entrada, un solo núcleo
`main.c` (consola) y `gui.c` (ventana Win32) solo se encargan de la presentación. Toda la lógica está en los módulos compartidos, de modo que una mejora en el análisis llega a las dos interfaces a la vez.

Los módulos no escriben directamente en `stderr`: llaman a `registro_mensaje()`. La consola deja el comportamiento por defecto (escribir en `stderr`); la interfaz gráfica instala un manejador que acumula los mensajes y los muestra en un cuadro de diálogo.

La interfaz usa las funciones Unicode (`W`) de Windows y convierte las rutas a la página de códigos ANSI que usa `fopen`. Si una ruta tiene caracteres que no caben en esa página, la rechaza con un aviso en lugar de abrir un archivo equivocado.

### Portabilidad
Todo es C17 estándar salvo `descubrimiento.c` y `historial.c`, que aíslan el código de Windows (`iphlpapi`, `FindFirstFile`) tras `#ifdef _WIN32` con alternativas POSIX o con un error claro.

## Estructuras principales

```c
struct Dispositivo { char ip[16]; char mac[18]; char hostname[64]; };

struct Hallazgo {               /* uno por MAC presente en alguna lista */
    enum Categoria categoria;   /* SOLO_OBSERVADO, SOLO_REFERENCIA, CAMBIO, IGUAL */
    const struct Dispositivo *referencia, *observado;
};

struct Alerta {
    enum TipoAlerta tipo; enum Severidad severidad;
    char ip[16]; char mac[18]; char mensaje[256];
};
```

`compare` y `audit` usan el mismo motor: solo cambian los nombres que se muestran (`NUEVO` ↔ `NO_INVENTARIADO`, etc.).

## Códigos de salida

| Código | Significado | Uso típico |
|---|---|---|
| 0 | Sin novedades | La tarea programada termina en silencio. |
| 1 | Error (uso, datos, E/S) | Revisar la configuración. |
| 2 | Hay hallazgos o alertas MEDIA/ALTA | Disparar un correo o un ticket. |
