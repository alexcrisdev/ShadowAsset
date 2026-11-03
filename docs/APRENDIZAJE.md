# Notas de aprendizaje

Conceptos trabajados en el proyecto, con las preguntas originales de la primera lección.

## C

- **¿Qué diferencia hay entre `FILE *archivo` y `*archivo`?**
  `FILE *archivo` declara un puntero a una estructura `FILE`. `*archivo` desreferencia ese puntero, es decir, accede a la estructura. Con archivos nunca se desreferencia directamente: el puntero se pasa a `fread`, `fclose`, etc.
- **¿Qué significa `&dispositivo.ip`?**
  `ip` es un arreglo (`char[16]`). En casi cualquier expresión, un arreglo se convierte en un puntero a su primer elemento, así que `dispositivo.ip` ya es un `char *`. `&dispositivo.ip` es un puntero a *todo el arreglo* (`char (*)[16]`): misma dirección, distinto tipo. Con un escalar (`int activo`) sí hace falta `&` para que `sscanf` pueda escribir en él.
- **¿Por qué `fgets` necesita el tamaño del búfer?**
  Para no escribir más allá de su final, lo que sería un desbordamiento de búfer. Es la diferencia con la función insegura `gets`, que fue eliminada del estándar.
- **¿Por qué comprobamos el valor devuelto por `sscanf`?**
  Porque indica cuántos campos se leyeron. Si es menor de lo esperado, las variables restantes quedan sin inicializar y usarlas es un error.

Otros conceptos del código:
- Memoria dinámica con `realloc` y crecimiento geométrico ([src/dispositivo.c](../src/dispositivo.c)).
- `qsort` y búsqueda binaria con funciones de comparación ([src/analisis.c](../src/analisis.c)).
- Operaciones de bits: máscaras de subred y el bit "administrada localmente" de la MAC.
- Compilación separada: archivos `.h`/`.c` y guardas de inclusión.
- Compilación condicional `#ifdef _WIN32` para la portabilidad.

## Redes

- **ARP** traduce IP → MAC dentro de una red local. Cada sistema guarda las respuestas en una caché, que es lo que lee `discover`.
- **Suplantación ARP**: un atacante responde "la IP del router soy yo" para interceptar el tráfico (*man-in-the-middle*). El síntoma visible es que la MAC asociada a la IP del router cambia.
- **MAC administrada localmente**: si el segundo bit menos significativo del primer byte vale 1 (`x2`, `x6`, `xA`, `xE`), la MAC no fue asignada por el fabricante. Los móviles modernos usan MAC aleatorias por privacidad.
- **CIDR** (`192.168.1.0/24`): los primeros 24 bits identifican la red y los 8 restantes el equipo.
- **Rango de documentación**: `00:00:5E:00:53:00`–`FF` (RFC 7042) está reservado para ejemplos, como `192.0.2.0/24` en IPv4.

## Seguridad

- **Validar con listas blancas** (qué se permite) en vez de listas negras (qué se prohíbe).
- **CSV injection**: Excel ejecuta las celdas que empiezan por `=`, `+`, `-` o `@`.
- **TOCTOU** (*time of check to time of use*): si verificas un archivo y luego lo vuelves a abrir para usarlo, puede haber cambiado entretanto.
- **Hash ≠ firma**: SHA-256 detecta cambios, pero no prueba quién creó el archivo.
