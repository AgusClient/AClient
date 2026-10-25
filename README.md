# ⚡ AClient (Clientx30)

[![Release](https://img.shields.io/github/v/release/agusx30/Clientx30?color=orange&logo=github)](https://github.com/agusx30/Clientx30/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue?logo=windows)](https://github.com/agusx30/Clientx30/releases/latest)
[![Base DDNet](https://img.shields.io/badge/Base-DDNet-red)](https://ddnet.org)
[![Base TClient](https://img.shields.io/badge/Base-TClient-purple?logo=github)](https://github.com/TaterClient/TClient)
[![License](https://img.shields.io/badge/License-Zlib-green)](license.txt)

**AClient** es un cliente competitivo y de práctica avanzado para **DDraceNetwork (DDNet)** y **Teeworlds**, diseñado por **agusx30** sobre la sólida base de **TClient** y **DDNet**. Incorpora herramientas de análisis en tiempo real, múltiples algoritmos de respuesta rápida de entrada (Fast Input), animación dinámica de posiciones, personalización visual profunda y un sistema de configuración propio.

---

## 🌟 Características Destacadas

### 📊 Race Stats & Overlay en Tiempo Real
* **Detección precisa de Saves:** Algoritmo avanzado que detecta y contabiliza de forma confiable tanto *hammer saves* como *hook saves*.
* **Ordenamiento Dinámico en Vivo (`ac_mini_statboard_sort`):** Puedes elegir ordenar el mini statboard por:
  * Default (ID/Orden natural)
  * Saves
  * Muertes
  * Freezes
  * Eficiencia (Ratio Saves/Muertes)
  * Net (Saves − Muertes)
  * Tiempo
  * Hooks
  * Lasts
* **Animación estilo F1 (`ac_mini_statboard_anim`):** 
  * Los jugadores se deslizan suavemente a sus nuevas posiciones en el ranking en tiempo real.
  * Destellos de sobrepaso: destello verde cuando un jugador escala posiciones y naranja suave cuando cae.
  * Velocidad de animación ajustable (`ac_mini_statboard_anim_speed`).
* **Columnas y visualización 100% configurables:** Activa o desactiva columnas individuales desde la pestaña de ajustes de AClient.

### ⚡ Fast Input Multimodelo y Anti-Lag
* **Múltiples motores de Fast Input (`ac_fast_input_mode`):**
  * `0: Vanilla` – Comportamiento original sin predicción extra.
  * `1: Fast` – Modo estándar optimizado para baja latencia.
  * `2: Extreme` – Respuesta agresiva de entrada.
  * `3: Best` – Algoritmo de predicción adoptado de BestClient.
  * `4: Saiko` – Motor de interpolación y respuesta ultra reactiva estilo Saiko.
* **Margen de predicción integrado (`ac_prediction_margin`):** Ajusta los ticks de predicción directamente en el bloque de Entrada.
* **Control independiente para otros jugadores (`ac_fast_input_others`):** Aplica o desactiva la predicción en tees externos según tus necesidades.

### 🛠️ Configuración Propia y Persistente (`ac_`)
* **Archivo de configuración dedicado:** Se almacena y lee automáticamente en `%appdata%\DDNet\aclient.cfg`.
* **Exportación a archivos `.txt`:** Cada modificación de perfiles de AClient sincroniza su respectivo archivo de configuración para respaldo y transporte fácil.
* **Jerarquía de pestañas organizada:** Menú de ajustes ordenado en `TClient`, `Perfiles` y `AClient`.

### 🎮 Pestaña de Comandos en el Juego
* Navega por todos los comandos y variables `ac_` directamente desde el cliente con interfaz de tarjetas explicativas, atajos y barra de desplazamiento integrada.

### 🎨 Editor Integrado de Game Assets
* **Edición y Composición en Vivo:** Combina 2 assets distintos (`Entities`, `Game`, `HUD`, `Particles`, `Emoticons`, `Extras`) directamente desde la interfaz del juego sin necesidad de programas de diseño externos.
* **Lienzo Dual Donante / Resultado:** Explora tus assets instalados y arrastra componentes (armas, tiles, iconos, partículas) de izquierda a derecha con reescalado bilineal automático para resoluciones mixtas (HD y estándar).
* **Paleta de Colorización y Modos de Fusión:** Modifica el color de cualquier sprite individual con barras HSLA, slider de opacidad y 4 modos de mezcla (`TeeLike`, `Screen`, `Multiply`, `Overlay`).
* **Exportación Inmediata a PNG:** Guarda tus creaciones directamente en `%appdata%\DDNet\assets\...` (`ac_asset`), quedando listas para equipar y jugar de inmediato sin reiniciar.

### 🎯 Herramienta de Práctica Rápida (Fast Practice)
* Alterna el modo de práctica instantáneamente con un solo bind (`ac_fast_practice_toggle`).

### 👾 Discord Rich Presence (Clientx30)
* Muestra tu estado de juego, mapa actual y presencia personalizada de **Clientx30 / AClient** en tu perfil de Discord.

---

## 📥 Descarga e Instalación

1. Dirígete a la sección de **[Últimos Releases](https://github.com/agusx30/Clientx30/releases/latest)**.
2. Descarga el paquete portable **`AClient-vX.X.X-win64.zip`**.
3. Descomprime la carpeta en la ubicación que prefieras.
4. Ejecuta **`AClient.exe`** (o `DDNet.exe`) ¡y a jugar!

---

## ⚙️ Comandos y Variables Clave (`ac_`)

| Comando / Variable | Por Defecto | Descripción |
| :--- | :---: | :--- |
| `ac_mini_statboard` | `1` | Activa o desactiva el overlay de Race Stats en pantalla |
| `ac_mini_statboard_sort` | `0` | Criterio de ordenamiento: 0=Default, 1=Saves, 2=Muertes, 3=Freezes, 4=Eficiencia, 5=Net, 6=Tiempo, 7=Hooks, 8=Lasts |
| `ac_mini_statboard_anim` | `1` | Activa la animación suave de cambio de posición estilo F1 |
| `ac_mini_statboard_anim_speed` | `10` | Velocidad de la animación de reordenamiento (3 a 30) |
| `ac_fast_input_mode` | `1` | Selector de motor Fast Input (0=Vanilla, 1=Fast, 2=Extreme, 3=Best, 4=Saiko) |
| `ac_fast_input_amount` | `1` | Nivel de intensidad del Fast Input |
| `ac_fast_input_others` | `1` | Aplicar predicción de Fast Input a otros jugadores |
| `ac_prediction_margin` | `10` | Margen de predicción en milisegundos |
| `ac_discord_rpc` | `1` | Activa el Rich Presence personalizado de Discord para Clientx30 |
| `ac_fast_practice_toggle` | - | Comando para alternar el modo Fast Practice rápidamente |
| `ac_stat_hammer_saves` | `1` | Muestra la columna de Hammer Saves |
| `ac_stat_hook_saves` | `1` | Muestra la columna de Hook Saves |
| `ac_stat_efficiency` | `1` | Muestra la columna de Eficiencia |

---

## 📜 Soporte de Scripts (ChaiScript)

AClient conserva soporte total para scripts en [ChaiScript](https://chaiscript.com/) para automatizaciones, utilidades y atajos avanzados en el cliente.
Coloca tus scripts `.chai` en el directorio de configuración de DDNet y ejecútalos en la consola con:
```sh
chai [nombre_del_script] [argumentos...]
```

---

## ❤️ Créditos y Reconocimientos

* **[DDraceNetwork (DDNet)](https://ddnet.org):** Desarrollado por Dennis Felsing (`def-`) y la inmensa comunidad de DDNet.
* **[TClient](https://github.com/TaterClient/TClient):** Desarrollado por Taters (`sjrc6`).
* **[Teeworlds](https://www.teeworlds.com):** Creado originalmente por Magnus Auvinen (`matricks`).
* **AClient / Clientx30:** Modificaciones, integración de Fast Input multimodelo, F1 Race Stats overlay, configuración `ac_` y mantenimiento por **[agusx30](https://github.com/agusx30)**.
