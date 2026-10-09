# Pruebas en PC (sin hardware)

Compilan el código **real** del firmware para PC y lo prueban con AddressSanitizer y
UndefinedBehaviorSanitizer, que detectan en el acto desbordes de buffer, uso de memoria liberada
y comportamiento indefinido.

## Qué se prueba

| Archivo | Código del firmware | Qué verifica |
|---|---|---|
| `test_modem.cpp` | `AX25.cpp`, `modem.cpp`, `fx25.cpp` | Ida y vuelta completa: texto TNC2 → trama AX.25 → **modulador real** → audio → **demodulador real** → trama. 1200, 300 y 9600 baudios, FX.25, 3 tramas en una TX, info de 250 caracteres, path de 8 saltos, fin de TX y time-out, abortar TX, TX a través del desborde de `millis()`, paquetes demasiado largos. |
| `test_noise.cpp` | modem | Tasa de decodificación con ruido blanco (curva de referencia) y el camino WAV a 44,1 kHz. |
| `test_config.cpp` | `config.cpp` | Guardado atómico: corte de luz en cualquier punto de la escritura y entre los renombres, archivo corrupto, `.bak`, JSON sin claves. Usa un sistema de archivos en memoria (`shim/FS.h`). |
| `test_digi.cpp` | `digirepeater.cpp` | WIDE1-1, WIDE2-2, TRACEn-N, path ya usado, sin path, paquetes de Internet (`qA`/`TCPIP`), NOCALL, path lleno. |

`shim/` contiene reemplazos mínimos de Arduino/ESP-IDF (tiempo simulado, logs, LittleFS en memoria).
`host_stubs.cpp` reemplaza las partes de `AFSK.cpp` que tocan hardware (DAC, ADC, PTT, LED).

## Cómo correrlas

Necesita Docker Desktop iniciado (usa la imagen `gcc:13`, no instala nada en Windows):

```powershell
powershell -File test\host\run.ps1            # todas
powershell -File test\host\run.ps1 config     # solo las que contienen "config"
```

En Linux o macOS con gcc: `make -C test/host` (opcional `T=filtro`).

La salida termina con `N tests, N checks, 0 failures`. Si un sanitizer detecta algo, el
programa se detiene mostrando el archivo y la línea del firmware.

## Decodificar grabaciones (WA8LMF TNC Test CD)

Herramienta `wav_decode`: pasa un WAV de 16 bits por el demodulador del firmware y cuenta tramas.

```powershell
docker run --rm -v "${PWD}:/src" -w /src/test/host gcc:13 make wav WAV=/src/track1.wav ARGS="1200"
# ARGS: 1200 | 300 | 9600, "flat" para audio sin de-énfasis, "-v" para listar las tramas
```

El WAV tiene que estar dentro de la carpeta del proyecto (se monta como `/src`).

**Límite:** el WAV entra directo al demodulador. El front-end del ESP32 (ADC real, quitar DC,
AGC, decimación) no está incluido, así que el resultado mide el demodulador; el equipo real
puede rendir algo distinto por el ruido y la alinealidad del ADC. Ver `docs/plan-de-pruebas.md`.

## Agregar una prueba

Crear `test_algo.cpp` en esta carpeta (el Makefile toma todos los `test_*.cpp`):

```cpp
#include "test.h"
TEST(mi_prueba)
{
    CHECK(1 + 1 == 2);
    CHECK_EQ_INT(valor, 3);
    CHECK_EQ_STR(texto, "esperado");
}
```

Si el código nuevo del firmware necesita algo de Arduino/ESP-IDF que falta, agregarlo en `shim/`.
