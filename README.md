# Encaje Carve — VST3 para Studio One

Plugin de inserto que analiza el espectro de cada pista en tiempo real y,
cuando dos o más pistas compiten fuerte por la misma zona de frecuencia,
recorta automáticamente esa zona en las que no son la protagonista. Se pone
uno por pista (o por canal/bus) y todas las instancias se "hablan" entre sí
mientras estén abiertas en la misma sesión de Studio One.

No es magia ni reemplaza tus oídos: es una asistencia en tiempo real para
dejar de pelear a mano por el mismo hueco de frecuencia entre kick y bajo,
voz y guitarra, etc. Siempre puedes bajarle intensidad con la perilla
**Carve** o apagarlo con **Carving activo**.

## 1. Subir este proyecto a GitHub

Necesitas una cuenta de GitHub (gratis alcanza).

1. Entra a github.com → **New repository** → dale un nombre, por ejemplo
   `encaje-carve` → **Create repository** (no marques ningún archivo inicial).
2. En tu computadora, dentro de esta carpeta del proyecto, corre:

   ```
   git init
   git add .
   git commit -m "Encaje Carve v1"
   git branch -M main
   git remote add origin https://github.com/TU-USUARIO/encaje-carve.git
   git push -u origin main
   ```

   (Reemplaza `TU-USUARIO` y el nombre del repo por los tuyos.)

## 2. Dejar que GitHub Actions compile

En cuanto hagas el `push`, el flujo en `.github/workflows/build.yml` arranca
solo. Si no arranca automáticamente:

1. Ve a la pestaña **Actions** de tu repositorio.
2. Entra al flujo **Build VST3 (Windows)**.
3. Botón **Run workflow** → **Run workflow**.

La primera vez tarda bastante (10–20 min) porque descarga y compila JUCE
entero desde cero. Los siguientes builds son más rápidos.

## 3. Descargar el plugin compilado

1. Cuando el run termine en verde, entra a él.
2. Al final de la página hay una sección **Artifacts** →
   `EncajeCarve-VST3-Windows` → descárgalo (es un .zip).
3. Descomprímelo. Vas a encontrar una carpeta llamada
   `Encaje Carve.vst3`.

## 4. Instalarlo

1. Copia la carpeta **`Encaje Carve.vst3`** completa a:

   ```
   C:\Program Files\Common Files\VST3\
   ```

2. Abre Studio One → menú **Studio One** → **Opciones** → pestaña
   **Ubicaciones** → **Plug-ins VST** → confirma que esa carpeta esté en la
   lista → botón **Reiniciar y volver a analizar**.
3. Busca **Encaje Carve** en tu lista de plugins (categoría EQ/Fx).

## 5. Usarlo

- Insértalo en cada pista que quieras que "negocie" espacio con las demás
  (por ejemplo: kick, bajo, voz, guitarra).
- Todas las que tengan el mismo **Grupo** (A–H) se comparan entre sí. Deja
  todas en **A** si quieres que compitan todas contra todas; usa grupos
  distintos para aislar, por ejemplo, la batería del resto de la mezcla.
- **Prioridad** (-12 a +12 dB): esto es lo que define **quién manda**. En cada
  banda gana la pista con más energía *multiplicada por su prioridad*. Sube la
  prioridad de la voz y esa se queda intacta en los medios aunque la guitarra
  suene más fuerte ahí; las demás se apartan. Déjalo en 0 si quieres que
  decida solo por energía.
- **Recorte max** (0 a 12 dB): el tope de cuánto puede bajar una pista. 6 dB
  es un punto de partida conservador.
- **Carve**: escala global de todo lo anterior (0 = no hace nada, 100 = aplica
  el recorte completo calculado).
- **Carving activo**: apágalo para dejar pasar la señal sin tocarla (pero la
  pista sigue "avisando" su espectro a las demás, para que no pierdan el
  contexto).
- Las 6 barras muestran el nivel de esa pista en cada banda:
  **verde** = esta banda es tuya, mandas tú aquí y no se te toca;
  **roja** = te estás apartando para dejarle espacio a otra pista (abajo ves
  cuántos dB); **gris** = no hay disputa en esa banda.

## La base de esto (y sus límites honestos)

**No existe una tabla científica que diga "el piano debe estar a X dB del
kick".** Eso depende del género, la densidad del arreglo y el gusto — es una
decisión artística, no una ley física. Cualquier herramienta que te prometa
"el nivel perfecto" para un instrumento específico está inventando precisión
donde no la hay.

Lo que sí es ciencia real y documentada es el **enmascaramiento por
frecuencia**: dos sonidos que ocupan la misma zona del espectro compiten por
ser escuchados por el oído humano. Esto está estudiado a fondo en
psicoacústica (bandas críticas, curvas de enmascaramiento — el trabajo de
referencia es Zwicker & Fastl, *Psychoacoustics: Facts and Models*). De ahí
sale una práctica bien documentada en la literatura de mezcla —por ejemplo
Mike Senior en *Mixing Secrets for the Small Studio*, o Bobby Owsinski en
*The Mixing Engineer's Handbook*—: mantener una **separación de nivel**
saludable entre el protagonista de una zona de frecuencia y todo lo demás
que compite ahí, típicamente entre 6 y 10 dB como punto de partida, ayuda al
oído a distinguirlos con claridad.

Eso es exactamente lo que hace el plugin, y es ajustable porque no es una
ley fija:

- En cada banda disputada se elige un **dueño** (según qué tipo de sonido es
  cada pista — bombo, bajo, voz, etc. — una convención de mezcla también
  documentada, no descubierta por el plugin).
- A cada pista que no es la dueña se le mide su separación de nivel REAL
  respecto al dueño, ahora mismo, y se le empuja hacia la
  **Separación objetivo** que pongas:
  - si ya está más separada de lo necesario (enterrada en la mezcla), la
    **sube**.
  - si está invadiendo el espacio del dueño (poca separación real), la
    **baja**.
- El ajuste nunca sube más de 6 dB de golpe, a propósito: un sonido casi
  silencioso en una banda daría matemáticamente un salto enorme, y sin ese
  tope se podría disparar el ruido de fondo a un volumen absurdo.
- Solo se toca una banda si el tipo de sonido detectado tiene sentido ahí
  (por ejemplo, no se sube el contenido de graves de unos hats solo porque
  esté "muy bajo" — ahí no debería sonar nada en primer lugar).

Confía en tus oídos por encima de estos números. 8 dB de separación es un
punto de partida razonable para casi cualquier género, pero una balada con
tres instrumentos necesita menos que un tema electrónico con quince capas.
Por eso es una perilla y no un valor fijo en el código.

## Notas técnicas

- Analiza con FFT de 8192 muestras (~5,4 Hz de resolución a 44.1 kHz) y
  ventanas solapadas al 75% (avanza cada 2048 muestras), así que sigue
  actualizando cada ~46 ms pero con mucha más resolución real, sobre todo en
  graves — antes esa zona apenas tenía un par de bins de FFT para trabajar.
- En cada banda se elige un **dueño** (mayor energía x prioridad) que nunca
  se recorta. Los demás se apartan en proporción a qué tan cerca le están
  pisando los talones: si ocupas casi tanto como el dueño, le estorbas de
  verdad y te apartas a fondo; si apenas asomas (menos del 12% de la banda),
  no se te toca nada.
- La energía de cada banda se mide como **promedio por bin** (densidad
  espectral), no como suma total. Si no fuera así, una banda ancha como
  Agudos (10.000 Hz de ancho) siempre le "ganaría" a Sub (40 Hz de ancho)
  solo por tener muchísimos más bins sumando, sin que eso refleje qué tan
  fuerte suena realmente cada una.
- El "acuerdo" entre pistas vive en memoria compartida dentro del mismo
  proceso de Studio One — no hay archivos ni red de por medio, y desaparece
  al cerrar la sesión.
- Solo compila formato **VST3** de 64 bits para Windows. Si más adelante
  quieres AU (Mac) o AAX, se puede agregar al `CMakeLists.txt`, pero AAX
  requiere el SDK con licencia de Avid.
