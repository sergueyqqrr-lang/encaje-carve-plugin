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
- **Carve**: qué tan fuerte recorta cuando detecta un choque (0 = no hace
  nada, 100 = recorte completo calculado).
- **Carving activo**: apágalo para dejar pasar la señal sin tocarla (pero la
  pista sigue "avisando" su espectro a las demás, para que no pierdan el
  contexto).
- Las 6 barras muestran el nivel de esa pista en cada banda; se ponen
  **rojas** cuando hay un choque real con otra pista y muestran cuántos dB
  se están recortando ahí en ese instante.

## Notas técnicas

- Analiza en ventanas de 2048 muestras (FFT + ventana Hann), actualiza cada
  ~46 ms a 44.1 kHz.
- El "acuerdo" entre pistas vive en memoria compartida dentro del mismo
  proceso de Studio One — no hay archivos ni red de por medio, y desaparece
  al cerrar la sesión.
- Solo compila formato **VST3** de 64 bits para Windows. Si más adelante
  quieres AU (Mac) o AAX, se puede agregar al `CMakeLists.txt`, pero AAX
  requiere el SDK con licencia de Avid.
