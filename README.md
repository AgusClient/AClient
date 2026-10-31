# AClient

[![Release](https://img.shields.io/github/v/release/AgusClient/AClient?color=orange&logo=github)](https://github.com/AgusClient/AClient/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue?logo=windows)](https://github.com/AgusClient/AClient/releases/latest)
[![Base DDNet](https://img.shields.io/badge/Base-DDNet-red)](https://ddnet.org)
[![Base TClient](https://img.shields.io/badge/Base-TClient-purple?logo=github)](https://github.com/TaterClient/TClient)
[![License](https://img.shields.io/badge/License-Zlib-green)](license.txt)

**AClient** es un cliente competitivo y de practica avanzado para **DDraceNetwork (DDNet)** y **Teeworlds**, desarrollado sobre la base de **TClient** y **DDNet**. Incorpora herramientas de analisis en tiempo real, perfiles KoG y DDNet, HUD modular de pulsaciones, gameskins compartidos entre jugadores, multiples algoritmos de respuesta rapida de entrada (Fast Input), editor de texturas integrado y actualizador automatico.

---

## Caracteristicas Principales

### Gameskins Compartidos entre Jugadores
* **Visualizacion de armas y gancho por jugador**: Visualiza el gameskin personalizado que lleva cada usuario de AClient en sus armas (martillo, pistola, escopeta, lanzagranadas, laser, ninja) y gancho (cabeza y cadena).
* **Modo local estricto por defecto**: Si no posees el archivo en tu disco local, no se efectuan descargas y el jugador se muestra de forma transparente con tu gameskin activo.
* **Descarga automatica opcional (`cl_download_gameskins`)**: Opcion para descargar gameskins remotos en caliente desde el repositorio de assets.
* **Zero Polling**: Anuncio y consulta unica al conectar o refresco manual bajo demanda en **Configuracion -> Tee** (`reload_remote_gameskins`).
* **Texture Pooling**: Memoria de video optimizada compartiendo instancias de texturas entre jugadores que usen el mismo gameskin.

### Perfiles de Jugador y Modo Equipo (DDNet & KoG)
* **Doble fuente de datos (`ac_profile_source`)**: Consulta de perfiles oficiales de DDNet y King of Gores (KoG.tw) directamente dentro del juego (`profile <nombre>`).
* **Busqueda inteligente insensible a mayusculas**: Deteccion automatica y prueba transparente de variantes en mayusculas/minusculas con renovacion de tokens CSRF.
* **Modo Equipo (`ac_profile_view_mode 1`)**: Analiza al equipo completo y muestra mapas incompletos comunes clasificados por dificultad y puntos.

### HUD Modular, Keystrokes & Velocimetro
* **Overlay de Teclas Pulsadas (`ac_hud_keystrokes`)**: Muestra en tiempo real las teclas de movimiento (WASD/flechas), salto, hook y disparo con colores reactivos.
* **Velocimetro Dinamico (`ac_hud_speedometer`)**: Indicador de velocidad en unidades/segundo.
* **Personalizacion completa**: Ajuste de posicion X/Y, escala y opacidad desde la pestana de configuracion.

### Race Stats & Mini Statboard en Tiempo Real
* **Deteccion precisa de Saves**: Registro y conteo confiable de hammer saves y hook saves.
* **Ordenamiento Dinamico en Vivo (`ac_mini_statboard_sort`)**: Ordena por saves, muertes, freezes, eficiencia, net, tiempo, hooks o lasts.
* **Animacion estilo F1 (`ac_mini_statboard_anim`)**: Transiciones suaves al cambiar de posicion en el ranking con destellos de adelantamiento.

### Fast Input Multimodelo y Anti-Lag
* **Modos de respuesta rapida (`ac_fast_input_mode`)**:
  * `0: Vanilla` - Comportamiento clasico.
  * `1: Fast` - Baja latencia estandar.
  * `2: Extreme` - Respuesta agresiva.
  * `3: Best` - Algoritmo adaptado de BestClient.
  * `4: Saiko` - Motor estilo Saiko.
  * `7: AClient` - Ring-buffer de 200 ticks con respuesta 1:1 seca y directa.
  * `8: aX30` - Interpolacion Hermite / Catmull-Rom spline con compensacion adaptativa de jitter en tiempo real.
* **Margen de prediccion integrado (`ac_prediction_margin`)**: Ajuste en milisegundos.

### Actualizador Integrado en el Juego
* Notificacion automatica de nuevas releases desde GitHub.
* Descarga, respaldo seguro del ejecutable anterior y actualizacion en un solo clic desde el menu.

### Editor Integrado de Game Assets
* Combina 2 assets distintos (`Entities`, `Game`, `HUD`, `Particles`, `Emoticons`, `Extras`) directamente en el cliente.
* Reescalado bilineal automatico entre resoluciones mixtas y previsualizacion inmediata.
* Colorizacion con barras HSLA y 4 modos de fusion (`TeeLike`, `Screen`, `Multiply`, `Overlay`).
* Exportacion directa a PNG en la carpeta de assets del juego.

---

## Descarga e Instalacion

1. Dirigete a la seccion de **[Ultimos Releases](https://github.com/AgusClient/AClient/releases/latest)**.
2. Descarga el ejecutable **`DDNet.exe`** (o el paquete portable comprimido).
3. Coloca el archivo en tu carpeta de instalacion de DDNet / AClient.
4. Ejecuta **`DDNet.exe`**.

---

## Comandos y Variables Clave (`ac_`)

| Comando / Variable | Por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_show_remote_gameskins` | `1` | Mostrar gameskins de armas y gancho de otros jugadores de AClient |
| `cl_download_gameskins` | `0` | Descargar gameskins automaticamente si no se tienen en disco |
| `reload_remote_gameskins` | - | Consultar y refrescar los gameskins remotos del servidor actual |
| `ac_profile_source` | `0` | Fuente de datos para perfiles (0: DDNet, 1: KoG) |
| `ac_profile_view_mode` | `0` | Modo de visualizacion de perfiles (0: Individual, 1: Equipo) |
| `profile <nombre>` | - | Abrir el panel de perfil del jugador indicado |
| `ac_hud_keystrokes` | `1` | Activa el HUD de teclas pulsadas (WASD, Jump, Hook, Fire) |
| `ac_hud_speedometer` | `0` | Activa el velocimetro dinamico en pantalla |
| `ac_mini_statboard` | `1` | Activa o desactiva el overlay de Race Stats en pantalla |
| `ac_mini_statboard_sort` | `0` | Criterio de ordenamiento (Saves, Muertes, Freezes, Eficiencia, etc.) |
| `ac_mini_statboard_anim` | `1` | Activa la animacion suave estilo F1 |
| `ac_fast_input_mode` | `1` | Selector de motor Fast Input (0=Vanilla, 1=Fast, 7=AClient, 8=aX30) |
| `ac_prediction_margin` | `10` | Margen de prediccion en milisegundos |
| `ac_fast_practice_toggle` | - | Alternar modo de practica rapida |

---

## Creditos y Reconocimientos

* **[DDraceNetwork (DDNet)](https://ddnet.org):** Desarrollado por Dennis Felsing (`def-`) y la comunidad de DDNet.
* **[TClient](https://github.com/TaterClient/TClient):** Desarrollado por Taters (`sjrc6`).
* **[Teeworlds](https://www.teeworlds.com):** Creado originalmente por Magnus Auvinen (`matricks`).
* **AClient:** Modificaciones, perfiles KoG/DDNet, gameskins compartidos, Fast Input aX30/AClient, HUD modular y mantenimiento por el equipo de **[AgusClient](https://github.com/AgusClient/AClient)**.
