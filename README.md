# AClient

[![Release](https://img.shields.io/github/v/release/agusx30/AClient-Dev?color=orange&logo=github)](https://github.com/agusx30/AClient-Dev/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue?logo=windows)](https://github.com/agusx30/AClient-Dev/releases/latest)
[![Authenticode](https://img.shields.io/badge/Authenticode-Signed-brightgreen?logo=windows)](https://github.com/agusx30/AClient-Dev/releases/latest)
[![Base DDNet](https://img.shields.io/badge/Base-DDNet%2018.x-red)](https://ddnet.org)
[![License](https://img.shields.io/badge/License-Zlib%20%2F%20MIT-green)](license.txt)

**AClient** es un cliente competitivo y de alto rendimiento para **DDraceNetwork (DDNet)** y **Teeworlds**, diseñado sobre la base oficial de DDNet con foco en fluidez de control, optimización de entrada, analítica de carreras en tiempo real, herramientas profesionales de moderación y sincronización de recursos en la nube.

Se distribuye como ejecutable nativo de 64 bits optimizado con librerías modernas de renderizado (Vulkan / OpenGL), aceleración de red, puente IPC de Rust y soporte completo de perfiles web.

---

## Tabla de Contenidos

- [Novedades y Arquitectura](#novedades-y-arquitectura)
- [Suite de Funcionalidades Principales](#suite-de-funcionalidades-principales)
  - [1. Motores Fast Input y Predicción de Movimiento](#1-motores-fast-input-y-predicción-de-movimiento)
  - [2. RaceStats y Mini Statboard HUD en Vivo](#2-racestats-y-mini-statboard-hud-en-vivo)
  - [3. Perfiles de Jugador KoG y DDNet](#3-perfiles-de-jugador-kog-y-ddnet)
  - [4. Gameskins Compartidos en la Nube y Editor de Assets](#4-gameskins-compartidos-en-la-nube-y-editor-de-assets)
  - [5. Práctica Rápida (Fast Practice / Modo Fantasma)](#5-práctica-rápida-fast-practice--modo-fantasma)
  - [6. Suite de Moderación y Auto-Preservación de Demos](#6-suite-de-moderación-y-auto-preservación-de-demos)
  - [7. HUD Modular, Keystrokes y Frametime](#7-hud-modular-keystrokes-y-frametime)
  - [8. Automatizaciones, Chat QoL y Notificaciones AFK](#8-automatizaciones-chat-qol-y-notificaciones-afk)
  - [9. Traducción Automática y Binds Avanzados](#9-traducción-automática-y-binds-avanzados)
  - [10. FPS Booster y Optimizador Gráfico](#10-fps-booster-y-optimizador-gráfico)
  - [11. Relación de Aspecto Personalizada y Black Bars](#11-relación-de-aspecto-personalizada-y-black-bars)
- [Descarga e Instalación](#descarga-e-instalación)
- [Guía Rápida de Comandos y Variables (cvars)](#guía-rápida-de-comandos-y-variables-cvars)
- [Compilación desde el Código Fuente](#compilación-desde-el-código-fuente)
- [Créditos y Licencia](#créditos-y-licencia)

---

## Novedades y Arquitectura

* **Pipeline Híbrido C++ / Rust:** Integra rutinas en Rust para tareas auxiliares críticas, gestión de memoria segura e IPC sin penalizar el framerate.
* **Reducción de Latencia Perceptual:** Algoritmos dedicados que desacoplan la cadencia de entrada del jugador del tickrate estándar del servidor.
* **Consola F1 Limpia:** Supresión completa del spam informativo de base de datos, consultas HTTP y alertas de sincronización interna.
* **Persistencia Aislada:** Las configuraciones específicas de AClient se guardan de forma independiente en `settings_aclient.cfg` para no interferir con instalaciones vanilla de DDNet.

---

## Suite de Funcionalidades Principales

### 1. Motores Fast Input y Predicción de Movimiento
AClient cuenta con **8 motores seleccionables** de compensación y predicción de entrada (`ac_inputs`):
* `0: Off` - Comportamiento vanilla estándar.
* `1: Fast` - Predicción por milisegundos (`ac_fast_input_amount`) con soporte de repredicción de gancho ante cambios bruscos de ángulo (`ac_fast_input_repredict_hook`).
* `2: Best` - Algoritmo con curva de suavizado dinámico (`ac_best_input_smoothing`), compensación de latencia adaptativa (`ac_best_input_latency_comp`) y perfiles de interpolación (lineal, cúbica y suave).
* `3: Saiko` - Motor estilo Saiko con ajuste granular por centésimas de tick (`ac_saiko_input_amount`).
* `4: Delta` - Interpolación basada en derivadas delta para movimientos continuos.
* `5: F` - Motor micro-tick de alta precisión (`ac_f_input_amount`).
* `6: Cloud` - Ajuste de amortiguación de fluctuaciones.
* `8: aX30 (Insignia)` - Motor insignia con estabilización adaptativa de jitter (`ac_ax30_adaptive_jitter`) y filtro de seguridad perimetral de colisión (`ac_ax30_collision_clamp`).

> **Predicción para Terceros:** Permite aplicar predicción visual individual sobre otros jugadores en pantalla (`ac_*_others`), con el filtro exclusivo **Only When Frozen** (`ac_fast_input_others_only_frozen`) que predice a los compañeros únicamente cuando se encuentran congelados.

---

### 2. RaceStats y Mini Statboard HUD en Vivo
Seguimiento analítico en tiempo real de tu equipo durante cualquier mapa de DDNet o KoG:
* **Métricas Registradas:** Freezes, Saves (martillo y gancho), Lasts, Muertes, Eficiencia, Tiempo, Hooks y Net score.
* **Animación Dinámica Estilo F1 (`ac_mini_statboard_anim`):** Las posiciones en la tabla efectúan una transición fluida con destellos verdes (adelantamiento) o rojos cuando cambia el orden de rendimiento.
* **Miniaturas de Skins (`ac_mini_statboard_show_tees`):** Visualización del avatar exacto de cada jugador en la lista.
* **Persistencia Inteligente:** Posibilidad de mantener las estadísticas acumuladas tras morir con `/kill` (`ac_statboard_keep_on_death`) y autoguardado en el historial de carreras (`ac_statboard_auto_save`).
* **Optimización de Cálculo (`ac_statboard_background_calc`):** El procesamiento per-tick de métricas (freezes, hammer saves, muertes) se pausa de inmediato cuando el overlay está desactivado para evitar sobrecarga en segundo plano, con opción de mantener el seguimiento activo si se prefiere.
* **Visualización Modular:** Máscara de columnas personalizable por bitmask, escala y opacidad configurables.

---

### 3. Perfiles de Jugador KoG y DDNet
Consulta estadísticas completas dentro del juego sin necesidad de abrir un navegador:
* **Doble Origen de Datos (`profile <nombre>`):** Alterna entre las bases de datos oficiales de **DDNet** y **King of Gores (KoG.tw)** mediante `ac_profile_source`.
* **Búsqueda Case-Insensitive Inteligente:** Resuelve nombres sin importar mayúsculas/minúsculas y gestiona automáticamente los tokens CSRF.
* **Visor de Dificultades KoG:** Clasificación instantánea de mapas (Easy, Main, Hard, Insane, Extreme y Mod).
* **Botón Votar Dif:** Emite un voto para cambiar el mapa o la dificultad correspondiente en un solo clic.
* **Modo Cooperativo de Equipo (`ac_profile_view_mode 1`):** Cruza los mapas completados por todos los integrantes de tu equipo actual y resalta los mapas pendientes que tienen en común.

---

### 4. Gameskins Compartidos en la Nube y Editor de Assets
* **Presencia en Tiempo Real con Backend Cloud:** Visualiza las armas, ganchos y skins personalizadas de todos los usuarios de AClient en el servidor.
* **Descarga Automática en Caliente (`cl_download_gameskins`):** Los assets se sincronizan y descargan directamente desde el repositorio oficial de skins sin necesidad de reiniciar el juego.
* **Pooling de Texturas en GPU:** El motor reutiliza las texturas cargadas en VRAM para tees que compartan apariencias, eliminando micro-tirones (stuttering) o duplicación de memoria.
* **Editor Integrado de Assets:** Herramienta interactiva para previsualizar, recortar y ajustar texturas de juego en vivo.

---

### 5. Práctica Rápida (Fast Practice / Modo Fantasma)
Entrena saltos, partes complejas de mapas o mecánicas de martillo sin abandonar la partida ni perder tu progreso:
* **Activación Inmediata (`ac_fast_practice_toggle`):** Crea una instancia local con control total de tu tee.
* **Fantasma Visual de Referencia (`ac_fast_practice_ghost_alpha`):** Muestra un tee semitransparente con tu posición y trayectoria previa para comparar intentos.
* **Auto-Espectador (`ac_fast_practice_spec_original`):** Tu tee original pasa a modo espectador automáticamente mientras practicas.
* **Comandos Rápidos Integrados:** Teletransporte al cursor (`/tp` y `/tc`), invencibilidad local ante freeze (`/invincible`), armas completas (`/weapons`) y guardado/carga de checkpoints locales (`/save` y `/load`).

---

### 6. Suite de Moderación y Auto-Preservación de Demos
Herramientas avanzadas para administradores y moderadores de servidores:
* **Acciones Contextuales en Scoreboard:** Menú desplegable con clic derecho sobre cualquier jugador con presets configurables de sanción (Mute, Kick y Ban).
* **Auto-Grabación ante Sanciones (`cl_mod_record_on_punish`):** Al expulsar o banear a un jugador, el cliente extrae automáticamente un clip de los últimos 3 minutos desde el búfer circular a `demos/replays/`.
* **Preservación Dual de Evidencias:** Guarda simultáneamente el corte específico y la demo completa de la sesión.
* **Arma de Moderación (`tc_mod_weapon`):** Permite ejecutar comandos de administración apuntando y disparando con la mirilla, mostrando una cuenta regresiva sobre el tee objetivo.
* **Reproductor Mejorado de Demos:** Marcadores de corte en vivo (`cl_demo_slice_begin`/`end`), velocímetro e indicadores visuales de pausa/velocidad.

---

### 7. HUD Modular, Keystrokes y Frametime
* **Overlay de Teclas (`ac_hud_keystrokes`):** Indicador visual en pantalla de las teclas pulsadas (WASD, salto, disparo y gancho) con layouts estándar y compactos (A/D).
* **Velocímetro Digital (`ac_hud_speedometer`):** Monitorización de velocidad vectorial y horizontal en tiempo real.
* **Frametime Preciso (`ac_show_frametime`):** Lectura numérica en ms en la esquina superior derecha, con baja ocupación visual.
* **Indicadores de Freeze de Compañeros:** HUD con cantidad de compañeros congelados y estado del equipo.

---

### 8. Automatizaciones, Chat QoL y Notificaciones AFK
* **Auto Team Lock (`ac_auto_team_lock`):** Envía automáticamente `/lock 1` tras unirse a un equipo con retardo ajustable anti-spam.
* **Auto-Respuesta AFK (`ac_afk_auto_reply`):** Envía un mensaje configurable cuando alguien te menciona en el chat mientras tienes el juego minimizado o tabulado.
* **Sistema de Saludo Rápido (`ac_greet_message`):** Bind configurable para saludar inmediatamente al último usuario que te mencionó o envió un susurro.
* **Chat Silencioso (Silent Typing):** Oculta la animación de la burbuja de puntos sobre tu cabeza (`ac_silent_typing`) o las de otros jugadores (`ac_hide_others_chat_bubbles`).
* **Scroll de Historial en Chat Abierto (`ac_chat_max_lines`):** Al abrir el chat con `Enter` o tus binds (`T`, `Y`), puedes desplazarte hacia mensajes anteriores con la rueda del ratón (`Shift + Rueda` para avance línea por línea), `RePág`/`AvPág` o `Ctrl+Inicio`/`Ctrl+Fin` sin cambiar de arma. Viene en `64` líneas por defecto y es ampliable hasta `512` líneas.
* **Notificaciones de Escritorio en Segundo Plano:** Alertas de Windows (Toast), sonido o parpadeo en la barra de tareas al ser descongelado (`ac_notify_unfreeze`) o al cambiar de casilla (`ac_change_tile_notification`).

---

### 9. Traducción Automática y Binds Avanzados
* **Traducción en Tiempo Real (`translate_auto`):** Traduce automáticamente los mensajes entrantes al idioma elegido (`tc_translate_target`).
* **Rueda de Binds (Bindwheel):** Menú circular visual para invocar comandos o frases rápidamente con el ratón.
* **Bindchat:** Asignación de alias y ejecuciones rápidas mediante triggers de chat.
* **Warlist:** Lista de seguimiento de jugadores y clanes prioritarios o rivales con colores personalizables.

---

### 10. FPS Booster y Optimizador Gráfico
* **Interruptor Maestro (`ac_optimizer` / `ac_optimizer_toggle`):** Activa al instante un perfil de máximo rendimiento y menor latencia para computadoras de gama baja o monitores de altos hercios.
* **Supresión Real de Partículas en CPU y GPU (`ac_optimizer_disable_particles`):** Corta de raíz la creación, simulación física, colisiones y flujos (`m_Flow`) de humo, explosiones, estelas y estrellas de freeze conservando intactos todos los sonidos del juego.
* **Recorte de Quads Decorativos (`ac_optimizer_disable_quads`):** Saltea el dibujado de quads estéticos pesados colocados por los mappers (nubes animadas, fondos con luces y capas visuales no esenciales).
* **Gestión No Destructiva de Detalle (`ac_optimizer_disable_high_detail`):** Omite en tiempo de renderizado las capas y sonidos ambientales de alto detalle sin sobrescribir tu variable `gfx_high_detail` en disco.
* **FPS Fog por Radio Fijo o Porcentaje de Zoom (`ac_optimizer_fps_fog`):** Recorta tiles del mapa (respetando parallax), bordes de entidades, ganchos, tees, barras de freeze, estelas, proyectiles, láseres, pickups y nameplates mediante AABB sin raíces cuadradas. Soporta **Modo 0 (Radio fijo en tiles)** (`ac_optimizer_fps_fog_radius_tiles`) y **Modo 1 (Porcentaje dinámico de Zoom)** (`ac_optimizer_fps_fog_zoom_percent`) que escala automáticamente al alejar o acercar la cámara.
* **Prioridad de Proceso en Windows (`ac_optimizer_high_priority`):** Asigna prioridad Alta al ejecutable en el sistema operativo para mitigar tirones causados por otras aplicaciones en segundo plano.

---

### 11. Relación de Aspecto Personalizada y Black Bars
* **Presets Competitivos y Proporción Libre (`ac_custom_aspect_ratio`):** Permite jugar en `4:3`, `16:10`, `5:4`, `16:9`, `21:9` o cualquier relación `Ancho:Alto` personalizada manteniendo la resolución nativa nítida del monitor.
* **Modo Black Bars o Estirado (`ac_custom_aspect_ratio_black_bars`):** Elige entre bandas negras sólidas laterales/superiores sin deformación de píxeles o imagen estirada a pantalla completa (`Stretched`).
* **Solo en Partida sin Parpadeo (`ac_custom_aspect_ratio_ingame_only`):** Aplica la relación personalizada únicamente dentro de la partida y mantiene los menús (`Escape`) y el editor de mapas en pantalla completa nativa sin recrear el swapchain ni recargar texturas en OpenGL ni Vulkan 1.1.0.

---

## Descarga e Instalación

### Windows x64 (Binario Precompilado)
1. Ve a la sección de **[Últimas Releases](https://github.com/AgusClient/AClient/releases/latest)**.
2. Descarga el paquete recomendado:
   * **`AClient-windows-v2.6.8.zip` (Recomendado):** Incluye `DDNet.exe`, la carpeta [`data/`](data/) completa (incluyendo shaders `.spv` para Vulkan 1.1.0), todas las librerías dinámicas (`SDL2.dll`, `libcurl.dll`, `vulkan-1.dll`, etc.) y archivos de configuración iniciales. Simplemente descomprime y ejecuta `DDNet.exe`.
   * **`DDNet.exe`:** Solo el binario ejecutable para actualizar una instalación existente de DDNet/AClient.

---

## Guía Rápida de Comandos y Variables (cvars)

### Fast Input y Predicción
| Variable / Comando | Defecto | Descripción |
| :--- | :---: | :--- |
| `ac_inputs` | `8` | Modo de entrada: `0` Off, `1` Fast, `2` Best, `3` Saiko, `4` Delta, `5` F, `6` Cloud, `8` aX30 |
| `ac_ax30_input_amount` | `100` | Desfase del motor aX30 (en centésimas de tick) |
| `ac_ax30_adaptive_jitter` | `0` | Compensación adaptativa de fluctuación de latencia |
| `ac_ax30_collision_clamp` | `0` | Filtro de colisión y contorno para evitar desincronización |
| `ac_ax30_others` | `1` | Aplica predicción aX30 a los demás jugadores |
| `ac_fast_input_others_only_frozen` | `0` | Predice a otros jugadores solo si están congelados |
| `ac_fast_input_repredict_hook` | `0` | Repredice la trayectoria del gancho ante giros rápidos |

### RaceStats y HUD
| Variable / Comando | Defecto | Descripción |
| :--- | :---: | :--- |
| `ac_mini_statboard` | `0` | Activa o desactiva el overlay de RaceStats en el HUD |
| `ac_statboard_background_calc` | `0` | Calcula estadísticas en segundo plano aunque el overlay esté oculto |
| `ac_mini_statboard_anim` | `1` | Animación fluida de posiciones estilo F1 |
| `ac_statboard_keep_on_death` | `0` | Mantiene las estadísticas tras morir con `/kill` |
| `ac_hud_keystrokes` | `0` | Muestra el panel en pantalla de teclas pulsadas |
| `ac_hud_speedometer` | `0` | Muestra el velocímetro digital |
| `ac_show_frametime` | `0` | Monitor numérico de frametime en la esquina superior |

### Perfiles y Moderación
| Variable / Comando | Defecto | Descripción |
| :--- | :---: | :--- |
| `profile <nombre>` | - | Abre el panel de perfil del jugador indicado |
| `ac_profile_source` | `0` | Fuente de perfil (`0` DDNet, `1` King of Gores) |
| `ac_profile_view_mode` | `0` | Modo de visualización (`0` Individual, `1` Comparativa de equipo) |
| `cl_mod_record_on_punish` | `1` | Guarda repetición de 3 minutos al sancionar (Kick/Ban) |
| `tc_mod_weapon` | `0` | Dispara acciones de moderación apuntando con el arma |

### FPS Booster y Optimizador Gráfico
| Variable / Comando | Defecto | Descripción |
| :--- | :---: | :--- |
| `ac_optimizer` | `0` | Interruptor principal del optimizador / booster de FPS |
| `ac_optimizer_toggle` | - | Alterna rápidamente el estado del optimizador (bindeable) |
| `ac_optimizer_disable_particles` | `1` | Desactiva renderizado y actualización de partículas |
| `ac_optimizer_disable_quads` | `1` | Saltea el dibujo de quads decorativos de mapas |
| `ac_optimizer_disable_high_detail` | `1` | Fuerza off las capas de alto detalle mientras esté activo |
| `ac_optimizer_fps_fog` | `0` | Limita el renderizado al área enfocada alrededor de la cámara |
| `ac_optimizer_fps_fog_mode` | `0` | Modo de FPS Fog (`0` Radio fijo en tiles, `1` % dinámico del zoom) |
| `ac_optimizer_fps_fog_radius_tiles` | `35` | Radio en tiles cuando `ac_optimizer_fps_fog_mode` es `0` |
| `ac_optimizer_fps_fog_zoom_percent` | `75` | Porcentaje visible del área de zoom cuando el modo es `1` |
| `ac_optimizer_fps_fog_cull_map_tiles` | `1` | Recorta tiles y outlines fuera del área activa de FPS Fog |
| `ac_optimizer_high_priority` | `0` | Asigna prioridad de proceso Alta a DDNet en Windows |

### Relación de Aspecto y Historial de Chat
| Variable / Comando | Defecto | Descripción |
| :--- | :---: | :--- |
| `ac_custom_aspect_ratio` | `0` | Activa la relación de aspecto personalizada |
| `ac_custom_aspect_ratio_mode` | `0` | Preset (`0` 4:3, `1` 16:10, `2` 5:4, `3` 16:9, `4` 21:9, `5` Custom) |
| `ac_custom_aspect_ratio_black_bars` | `1` | `1` Bandas negras sin deformar, `0` Estirado (Stretched) |
| `ac_custom_aspect_ratio_ingame_only` | `1` | Aplica la relación personalizada solo dentro de la partida |
| `ac_chat_max_lines` | `64` | Límite de líneas almacenadas en el historial de chat (`64` a `512`) |

---

## Compilación desde el Código Fuente

### Requisitos Previos
* **CMake** 3.16 o superior.
* Compilador C++ con soporte para **C++17/20** (MSVC 2019/2022 o Clang/GCC en Linux).
* **Cargo / Rust** (para compilar los módulos del puente de Rust).
* **Python** 3.x.

### Pasos de Compilación (Windows - CMake)

```powershell
# 1. Clonar el repositorio
git clone https://github.com/agusx30/AClient-Dev.git
cd AClient-Dev

# 2. Generar el proyecto con CMake
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. Compilar el cliente
cmake --build . --config Release --target DDNet
```

El ejecutable resultante estará en `build/release/DDNet.exe` (o `build/Release/DDNet.exe`).

---

## Créditos y Licencia

* **[DDraceNetwork (DDNet)](https://ddnet.org):** Base de código abierta mantenida por Dennis Felsing (`def-`) y la comunidad de DDNet.
* **[Teeworlds](https://teeworlds.com):** Juego original creado por Magnus Auvinen (`matricks`).
* **[TClient](https://github.com/TaterClient/TClient):** Base de herramientas extendidas.
* **AClient:** Desarrollado, adaptado y optimizado por el equipo de **[AgusClient](https://github.com/agusx30/AClient-Dev)**.

Distribuido bajo la licencia [Zlib](license.txt) junto con las licencias correspondientes de dependencias de terceros en [license.txt](license.txt).
