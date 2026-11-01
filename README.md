# AClient

[![Release](https://img.shields.io/github/v/release/AgusClient/AClient?color=orange&logo=github)](https://github.com/AgusClient/AClient/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue?logo=windows)](https://github.com/AgusClient/AClient/releases/latest)
[![Signature](https://img.shields.io/badge/Authenticode-Signed-brightgreen?logo=windows)](https://github.com/AgusClient/AClient/releases/latest)
[![Base DDNet](https://img.shields.io/badge/Base-DDNet-red)](https://ddnet.org)
[![Base TClient](https://img.shields.io/badge/Base-TClient-purple?logo=github)](https://github.com/TaterClient/TClient)
[![License](https://img.shields.io/badge/License-Zlib-green)](license.txt)

**AClient** es el cliente avanzado y competitivo para **DDraceNetwork (DDNet)** y **Teeworlds**, desarrollado sobre la base de **TClient** y **DDNet**. Incorpora una suite completa de optimizaciones de red, múltiples algoritmos de respuesta rápida de entrada (Fast Input), sincronización de assets en la nube, perfiles completos de King of Gores (KoG) y DDNet, HUD modular en tiempo real, herramientas de práctica rápida, editor de texturas y utilidades diseñadas para competición y juego diario.

El binario oficial para Windows x64 se distribuye compilado en modo Release y firmado digitalmente mediante certificado Authenticode (*AClient Software*).

---

## Caracteristicas Principales

### 1. Motores Fast Input y Prediccion de Entrada
El cliente incluye un selector de 8 motores de respuesta rápida de entrada para adaptarse a cualquier conexión o servidor (`ac_inputs`):
* `0: Off` - Vanilla (comportamiento clásico estándar).
* `1: Fast` - Predicción por milisegundos con soporte configurable para otros jugadores.
* `2: Best` - Algoritmo adaptado de BestClient con suavizado dinámico.
* `3: Saiko` - Motor estilo Saiko.
* `4: Delta` - Interpolación Delta.
* `5: F` - Motor F en micro-ticks.
* `6: Cloud` - Predicción Cloud.
* `7: AClient` - Ring-buffer de 200 ticks con respuesta 1:1 directa sin latencia artificial.
* `8: aX30` - Motor de predicción continua adaptativa exclusivo de AClient, con compensación dinámica de fluctuaciones de ping (jitter), filtro de seguridad contra colisiones (*Anti-Snap*) y soporte opcional para otros jugadores en partida (`ac_ax30_others`).

### 2. Perfiles en Vivo y Sistema de Votacion KoG / DDNet
* **Doble fuente de datos (`ac_profile_source`)**: Alterna entre perfiles oficiales de DDNet y King of Gores (KoG.tw) directamente en el juego con `profile <nombre>`.
* **Resolución Inteligente Insensible a Mayúsculas**: Encuentra jugadores automáticamente sin importar cómo esté escrita la mayúscula o minúscula de su nick.
* **Base de Datos Local de Mapas KoG**: Categorización instantánea de dificultades (Easy, Main, Hard, Insane, Extreme y Mod) y sincronización automática en segundo plano.
* **Botón "Votar Dif"**: Si el mapa elegido corresponde a una dificultad distinta a la que corre el servidor actual, un solo clic emite el voto de cambio de categoría correspondiente.
* **Modo Equipo (`ac_profile_view_mode 1`)**: Analiza la alineación completa del equipo y muestra los mapas pendientes compartidos.

### 3. Race Stats HUD y Mini Statboard en Tiempo Real
* **Métricas en Vivo**: Seguimiento de Freezes, Saves (Hammer y Hook), Lasts, Eficiencia, Muertes, Net, Tiempo y Hooks.
* **Animación suave estilo F1 (`ac_mini_statboard_anim`)**: Transiciones visuales con destellos de color al ganar o perder puestos en carrera.
* **Miniaturas de Skins**: Muestra la skin del tee de cada jugador directamente en la tabla.
* **Persistencia configurable (`ac_statboard_keep_on_death`)**: Opción de no reiniciar las estadísticas al morir (`/kill`) y guardado automático en historial de carreras.

### 4. Gameskins Compartidos en la Nube
* **Sincronización mediante Cloudflare**: Tus compañeros que usen AClient visualizan automáticamente los skins de armas y gancho que tengas equipados.
* **Descarga en caliente opcional (`cl_download_gameskins`)**: Descarga automática opcional desde el repositorio de assets o modo estricto local sin descargas.
* **Texture Pooling**: Memoria de video optimizada reutilizando instancias de texturas para jugadores con skins coincidentes.

### 5. Fast Practice (Practica Rapida)
* Sistema integrado para entrenar partes complejas de mapas sin reiniciar la carrera.
* **Fantasma visual interactivo (`ac_fast_practice_ghost_alpha`)**: Opacidad configurable para comparar tu trayectoria actual con la trazada original.

### 6. Control de Notificaciones de Escritorio
* **Banners emergentes de Windows (Toast notifications)**: Notifica menciones en el chat (`AClient Chat`) o alertas de votación (`AClient Vote`) cuando el cliente está minimizado o en segundo plano.
* **Control total On / Off**:
  * Menú **Ajustes -> General**: Casilla *Mostrar notificaciones de escritorio*.
  * Menú **Ajustes -> AClient**: Casilla *Notificaciones de escritorio (Menciones de chat y Alertas)*.
  * Chat directo / consola: `/notif` o `/notifications [0|1]`, o comando `cl_shownotifications [0|1]`.

### 7. Herramientas de Moderacion Integradas (Mod Tools)
* Panel completo en **Ajustes -> AClient -> Moderacion** para administradores y moderadores de servidores.
* **Auto-grabado de repeticiones (`cl_mod_record_on_punish`)**: Al sancionar a un jugador con kick o ban, se guardan automáticamente los últimos 3 minutos de partida en `demos/replays/` como evidencia.
* **Menú contextual en Scoreboard**: Clic sobre cualquier jugador para aplicar mutes, kicks o bans con motivos y tiempos predefinidos.
* **Arma de Moderación (`tc_mod_weapon`)**: Ejecuta un comando RCON al apuntar y disparar a un jugador, mostrando una cuenta regresiva visual sobre el tee antes de actuar.

### 8. HUD Modular, Keystrokes y Velocimetro
* **Overlay de Teclas (`ac_hud_keystrokes`)**: Representación en tiempo real de movimiento (WASD/flechas), salto, gancho y disparo con layouts configurables.
* **Velocímetro Digital (`ac_hud_speedometer`)**: Indicador de velocidad vectorial y horizontal en unidades por segundo.
* Posicionamiento X/Y, escala y opacidad ajustables en tiempo real.

### 9. Utilidades y Calidad de Vida
* **Auto-Respuesta AFK (`ac_afk_auto_reply`)**: Envía automáticamente un mensaje al chat si alguien te menciona mientras el juego está minimizado o en segundo plano.
* **Saludo al Último Tag (`ac_greet_message`)**: Atajo de teclado para responder con un saludo al último jugador que interactuó contigo (en sala o susurro).
* **Selector de Sound Packs (`cl_sound_pack`)**: Menú visual para alternar paquetes de sonido del juego.
* **Editor Integrado de Assets**: Permite combinar y colorear texturas de entidades, armas y partículas directamente en el cliente.
* **Inicio Limpio e Inmediato**: Sin pantallas de bloqueo ni modales forzados al arrancar el cliente.

---

## Descarga e Instalacion

1. Dirígete a la sección de **[Releases Oficiales](https://github.com/AgusClient/AClient/releases/latest)**.
2. Descarga el ejecutable **`DDNet.exe`**.
3. Reemplaza o coloca el archivo en tu carpeta de instalación de DDNet o AClient.
4. Ejecuta **`DDNet.exe`**.

---

## Comandos y Variables de Configuracion

### Fast Input & Prediccion
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_inputs` | `0` | Modo de predicción de entrada (0: Off, 1: Fast, 2: Best, 3: Saiko, 4: Delta, 5: F, 6: Cloud, 7: AClient, 8: aX30) |
| `ac_ax30_input_amount` | `100` | Desfase de predicción del motor aX30 en centésimas de tick |
| `ac_ax30_adaptive_jitter` | `1` | Compensación adaptativa de jitter en tiempo real para aX30 |
| `ac_ax30_collision_clamp` | `1` | Filtro de seguridad perimetral anti-snap para aX30 |
| `ac_ax30_others` | `0` | Aplica la predicción aX30 sobre los demás jugadores |
| `ac_ax30_others_amount` | `100` | Desfase aX30 para otros jugadores en centésimas de tick |
| `ac_aclient_input_amount` | `100` | Desfase de predicción del motor AClient (ring-buffer 1:1) |
| `ac_aclient_input_others` | `0` | Aplica el motor AClient a otros jugadores |
| `ac_fast_input_amount` | `20` | Margen de predicción del modo Fast en milisegundos |
| `ac_fast_input_others` | `0` | Aplica el modo Fast a otros jugadores |
| `ac_fast_input_repredict_hook` | `0` | Repredice el gancho al cambiar bruscamente de dirección |

### Race Stats & Mini Statboard
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_mini_statboard` | `1` | Activa o desactiva el overlay de Race Stats en pantalla |
| `ac_mini_statboard_sort` | `0` | Orden del ranking (0: Default, 1: Saves, 2: Muertes, 3: Freezes, 4: Eficiencia, 5: Net, 6: Tiempo, 7: Hook, 8: Lasts) |
| `ac_mini_statboard_anim` | `1` | Activa la animación suave estilo F1 ante adelantamientos |
| `ac_mini_statboard_show_tees` | `1` | Muestra las miniaturas de los tees en la tabla |
| `ac_statboard_keep_on_death` | `0` | Mantiene las estadísticas activas tras morir con `/kill` |
| `ac_statboard_auto_save` | `1` | Guarda automáticamente las estadísticas en el historial |
| `ac_mini_statboard_scale` | `70` | Escala del Mini Statboard en porcentaje |
| `ac_mini_statboard_opacity` | `60` | Opacidad de fondo del Mini Statboard |

### Perfiles KoG / DDNet
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `profile <nombre>` | - | Abre el panel de perfil del jugador indicado |
| `ac_profile_source` | `0` | Fuente de datos para perfiles (0: DDNet, 1: King of Gores) |
| `ac_profile_view_mode` | `0` | Modo de visualización (0: Individual, 1: Equipo) |

### Notificaciones de Escritorio
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `cl_shownotifications` | `1` | Habilita o silencia las notificaciones emergentes de Windows |
| `/notif` o `/notifications` | - | Comando de chat rápido para alternar o fijar notificaciones (`/notif 0` o `/notif 1`) |

### Practica Rapida (Fast Practice)
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_fast_practice_toggle` | - | Alterna el modo de práctica rápida |
| `ac_fast_practice_ghost_alpha` | `45` | Nivel de transparencia del fantasma de práctica |
| `ac_fast_practice_show_text` | `1` | Muestra el indicador superior de modo práctica |

### Gameskins y Presencia en la Nube
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_show_remote_gameskins` | `1` | Muestra los gameskins de armas y gancho de otros usuarios de AClient |
| `cl_download_gameskins` | `0` | Descarga automática en caliente de gameskins desde el repositorio |
| `reload_remote_gameskins` | - | Fuerza el refresco de los gameskins remotos en el servidor actual |

### HUD Modular (Keystrokes & Velocimetro)
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_hud_keystrokes` | `0` | Muestra el panel de teclas pulsadas en pantalla |
| `ac_hud_keystrokes_layout` | `0` | Diseño de teclas (0: WASD + Hook/Fire/Jump, 1: A/D compacto) |
| `ac_hud_speedometer` | `0` | Muestra el velocímetro digital en pantalla |

### Utilidades & AFK
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_afk_auto_reply` | `0` | Responde automáticamente en el chat al ser mencionado estando tabulado |
| `ac_afk_auto_reply_msg` | `"Estoy tabulado / afk"` | Mensaje emitido por la auto-respuesta AFK |
| `ac_greet_message` | `"wenas"` | Mensaje enviado al presionar la tecla de saludo al último tag |
| `cl_sound_pack` | `""` | Paquete de sonido activo |

### Moderacion (Mod Tools)
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `cl_mod_record_on_punish` | `1` | Graba repetición de los últimos 3 minutos al sancionar (Kick/Ban) |
| `tc_mod_weapon` | `0` | Activa el arma de moderación por disparo |
| `tc_mod_weapon_command` | `rcon kill_pl` | Comando ejecutado por el arma de moderación |
| `cl_mod_allow_target_dummy` | `0` | Permite aplicar acciones de moderación sobre el dummy propio |

---

## Creditos y Reconocimientos

* **[DDraceNetwork (DDNet)](https://ddnet.org):** Desarrollado por Dennis Felsing (`def-`) y la comunidad de DDNet.
* **[TClient](https://github.com/TaterClient/TClient):** Desarrollado por Taters (`sjrc6`).
* **[Teeworlds](https://www.teeworlds.com):** Creado originalmente por Magnus Auvinen (`matricks`).
* **AClient:** Desarrollado, personalizado y mantenido por el equipo de **[AgusClient](https://github.com/AgusClient/AClient)**.
