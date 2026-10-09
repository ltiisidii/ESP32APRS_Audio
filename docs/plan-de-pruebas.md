# Plan de pruebas: ¿llega al nivel de un KPC-3+?

Objetivo: demostrar con mediciones, no con código, que una estación ESP32APRS_Audio puede quedar
sola en un cerro sin mantenimiento, con una confiabilidad comparable a un TNC dedicado
probado durante años (Kantronics KPC-3+ o similar).

Se evalúan cuatro cosas:

1. **Decodificación**: ¿escucha tantos paquetes como un KPC-3+?
2. **Larga duración**: ¿funciona semanas sin degradarse ni colgarse?
3. **Estrés**: ¿se recupera solo de cortes de luz, WiFi, Internet y transmisiones?
4. **Entorno físico**: ¿aguanta RF propia, alimentación y temperatura del sitio?

Completar las planillas de cada sección. Si una prueba falla, anotar fecha, hora y el log
correspondiente antes de repetirla.

---

## 0. Preparación

### 0.1 Firmware de prueba (con logs)

El firmware normal se compila con `CORE_DEBUG_LEVEL=0`, que **elimina todos los mensajes de log**,
incluidos la línea `HEALTH` y el motivo de reinicio del supervisor. Para las pruebas se usa un
firmware con nivel 3 (info, warning y error):

```powershell
$env:PLATFORMIO_BUILD_FLAGS = "-DCORE_DEBUG_LEVEL=3"
pio run -e TTGO-TWR -t upload        # cambiar el entorno según el hardware
Remove-Item Env:PLATFORMIO_BUILD_FLAGS
```

Al terminar las pruebas, volver a grabar el firmware normal (sin la variable) para el cerro.

### 0.2 Captura del log

Dejar una PC conectada por USB durante toda la prueba, guardando el log con fecha y hora:

```powershell
pio device monitor -e TTGO-TWR --filter time --filter log2file --filter esp32_exception_decoder
```

El archivo `platformio-device-monitor-*.log` queda en la carpeta del proyecto. Si el equipo se
reinicia por un crash, el decodificador muestra la traza: guardarla.

### 0.3 Qué mirar en el log

| Mensaje | Significado |
|---|---|
| `HEALTH up ... heap ... min ... maxblk ... adc ... drop ...` | Cada 10 min: uptime (s), heap libre, mínimo histórico, bloque libre más grande, contador del ADC, muestras de audio descartadas |
| `stack free <tarea> N bytes` | Margen de stack de cada tarea (mínimo histórico) |
| `SUPERVISOR: last restart was forced: ...` | Al arrancar: el reinicio anterior lo forzó el supervisor (y por qué) |
| `SUPERVISOR: ... restarting` | El supervisor detectó una tarea colgada o el ADC detenido |
| `PTT time-out` | El TOT cortó una transmisión que no terminaba |
| `APRS-IS no data for 120 s, reconnecting` | Conexión APRS-IS zombie detectada |
| `WiFi: reconnected after N attempts` | Recuperación de WiFi |
| `Guru Meditation` / `abort()` / `Backtrace` | Crash: **guardar la traza** |

### 0.4 Referencia

Lo ideal es tener un **KPC-3+ (u otro TNC de confianza) prestado** y medirlo con el mismo montaje.
Si no hay, comparar con resultados publicados (ver 1.4).

---

## 1. Decodificación (WA8LMF TNC Test CD)

Es la prueba estándar con la que los radioaficionados comparan TNCs: grabaciones reales de
tráfico APRS en 1200 baudios, siempre las mismas, así que los resultados son comparables.

### 1.1 Material

- **WA8LMF TNC Test CD** (pistas en WAV): <http://wa8lmf.net/TNCtest/>
  - Pista 1: 40 min de tráfico real de Los Ángeles, audio plano (flat).
  - Pista 2: el mismo tráfico con audio de-enfatizado (como sale del parlante de una radio).
- Una PC con placa de sonido para reproducirlas.

### 1.2 Cómo inyectar el audio

- **Placas con radio externa** (entrada de audio al ADC): de la salida de la placa de sonido a la
  entrada de audio del ESP32, con un atenuador/potenciómetro para ajustar el nivel.
- **TTGO-TWR** (radio SA868 integrada, sin entrada de audio): transmitir la pista con **otra radio**
  en la frecuencia de recepción, a baja potencia, con carga fantasma o antena lejos, y que el
  TTGO la reciba por RF. Ajustar para que la señal llegue limpia y fuerte.

### 1.3 Procedimiento

1. Anotar el contador **RADIO RX** del dashboard web (sección STATISTICS) antes de empezar.
2. Reproducir la pista completa sin tocar nada.
3. Anotar el contador al final. Paquetes decodificados = final − inicial.
4. Repetir con tres niveles de audio (−6 dB, nivel normal, +6 dB) para ver qué tan sensible es
   al ajuste de nivel. Un buen TNC decodifica bien en un rango amplio.
5. Repetir el nivel normal **con la página web abierta y recargándola** durante toda la pista.
   El resultado tiene que ser prácticamente igual al de sin web (valida el PR7). Al final, mirar
   en `HEALTH` que `drop` sea 0.

### 1.4 Planilla

| Equipo | Pista | Nivel | Paquetes decodificados | Observaciones |
|---|---|---|---|---|
| ESP32 | 1 | −6 dB | | |
| ESP32 | 1 | normal | | |
| ESP32 | 1 | +6 dB | | |
| ESP32 | 1 | normal + web | | |
| ESP32 | 2 | normal | | |
| KPC-3+ (referencia) | 1 | normal | | |
| KPC-3+ (referencia) | 2 | normal | | |

Resultados publicados para comparar (incluye otros TNCs y demoduladores por software): documento
"A Better APRS Packet Demodulator, Part 1, 1200 baud" de Dire Wolf (WB2OSZ), en
<https://github.com/wb2osz/direwolf/tree/master/doc>.

**Criterio**: igual o mejor que el KPC-3+ medido con el mismo montaje, y que el resultado con la
web cargada no baje más de un 1–2 % respecto al de sin web.

---

## 2. Larga duración

### 2.1 Montaje

El equipo **como va a quedar en el cerro**: misma radio, antena (o carga), fuente, configuración
(iGate, digi, balizas) y en un canal con tráfico real. Mínimo **72 horas**; ideal **14 días**.

### 2.2 Qué registrar (una vez por día)

| Día | Uptime (s) | heap | min | maxblk | drop | Stack libre mínimo (tarea) | Reinicios | Notas |
|---|---|---|---|---|---|---|---|---|
| 1 | | | | | | | | |
| 2 | | | | | | | | |
| 3 | | | | | | | | |

### 2.3 Cómo leerlo

- **Uptime** sube siempre. Si vuelve a valores chicos hubo un reinicio: buscar la causa en el log.
- **heap** y **min** deben estabilizarse en las primeras horas. Si bajan día tras día, hay una
  pérdida de memoria (el equipo terminaría reiniciándose por memoria baja).
- **maxblk** (bloque libre más grande) estable. Si cae mucho mientras `heap` se mantiene, la
  memoria se está fragmentando.
- **drop** en 0 o casi: si sube, el demodulador no da abasto y se pierden paquetes.
- **stack free** de cada tarea: ninguna por debajo de ~500 bytes.

**Criterio**: cero reinicios no planificados en 72 h (y en 14 días si se hace la versión larga),
heap estable, `drop` ≈ 0. Si se usa el reinicio diario (`reset_timeout = 1440`), esos reinicios
son planificados y no cuentan.

---

## 3. Estrés y recuperación

Cada prueba se repite varias veces. Lo que importa es que **el equipo se recupere solo**, sin
que nadie lo toque.

| # | Prueba | Cómo | Criterio de éxito | Veces | OK |
|---|---|---|---|---|---|
| 3.1 | Corte de luz al guardar | Guardar la configuración desde la web y cortar la alimentación justo después. | Arranca con la configuración previa o la nueva, **nunca** con NOCALL ni valores de fábrica | 20 | |
| 3.2 | Corte de luz al azar | Cortar la alimentación en momentos cualesquiera (RX, TX, web abierta). | Arranca y opera normalmente | 20 | |
| 3.3 | Router caído | Apagar el router 6 min y volver a prenderlo. | Mientras está apagado, el AP `ESP32APRS_Audio` sigue visible en `192.168.4.1`; al volver se reconecta solo | 5 | |
| 3.4 | Router de respaldo | Con 2 redes configuradas, apagar la principal. | Pasa a la secundaria en ~2 min | 3 | |
| 3.5 | Router sin ping | Bloquear ICMP en el router (con el iGate activo). | No se desconecta; aparece `Ping WiFi Fail, but APRS-IS is receiving: ignored` | 1 h | |
| 3.6 | Internet caído | Cortar la salida a Internet del router sin apagar el WiFi. | Se reconecta a APRS-IS en < 3 min cuando vuelve | 5 | |
| 3.7 | TX seguidas | Forzar muchas transmisiones (balizas cortas, digi en canal ocupado, MANUAL TX). | Ninguna queda trabada; no aparece `PTT time-out` | 50 TX | |
| 3.8 | Web durante TX | Abrir Storage o guardar la configuración de Radio mientras transmite. | El PTT se suelta; la TX termina o la corta el TOT | 10 | |
| 3.9 | Web pesada | Recargar páginas de la web sin parar durante 1 h con tráfico. | Sin reinicios; `drop` ≈ 0 | 1 h | |
| 3.10 | OTA y rollback | Subir firmware por la web y esperar 2 min. | Aparece `new firmware confirmed`. Repetir subiendo un firmware que se reinicie antes de 2 min: vuelve solo al anterior | 2 | |
| 3.11 | Paquetes largos | Inyectar por KISS/APRS-IS paquetes de 300–500 caracteres y paths de 8 saltos. | Sin reinicios | — | **Pendiente PR5** |

---

## 4. Entorno físico

Lo que un KPC-3+ resuelve con su hardware y su caja metálica.

| # | Prueba | Cómo | Criterio | OK |
|---|---|---|---|---|
| 4.1 | RF propia | Transmitir 100 veces a **potencia máxima** con la antena real (a la distancia que va a tener en el sitio). | Cero reinicios y cero cuelgues durante o después de la TX | |
| 4.2 | Alimentación | Variar la tensión de la fuente en todo el rango esperado del sitio (p. ej. 11–14,5 V en un sistema de 12 V con batería). | Opera normal en todo el rango, sin reinicios | |
| 4.3 | Caídas de tensión | Provocar caídas breves (encendido de otro equipo en la misma fuente, TX de la radio). | Sin reinicios por brownout | |
| 4.4 | Temperatura | 24 h dentro de la caja definitiva, al sol o con el calor esperado del sitio. | Sin reinicios; decodificación igual a la prueba 1 | |
| 4.5 | Rayos / estática | Verificar descargadores, puesta a tierra y filtros en antena y alimentación. | Instalación revisada | |

---

## 5. Lado a lado en campo (opcional, la más convincente)

Si se consigue un KPC-3+, dejar **los dos equipos una semana** en el mismo sitio, con antenas
equivalentes y la misma configuración de digi.

- Comparar cuántas estaciones y paquetes escucha cada uno (contador RADIO RX y "Last Heard").
- Comparar digipeats en aprs.fi (pestaña *raw packets* de cada indicativo).

**Criterio**: el ESP32 escucha y digipetea al menos lo mismo que el KPC-3+.

---

## 6. Resultado final

| Área | Resultado | Aprobado |
|---|---|---|
| 1. Decodificación | | |
| 2. Larga duración | | |
| 3. Estrés | | |
| 4. Entorno físico | | |
| 5. Lado a lado | | |

Si todo aprueba: grabar el **firmware normal** (sin logs), activar el reinicio diario
(`reset_timeout = 1440`) como red de seguridad adicional y recién entonces subir al cerro.
