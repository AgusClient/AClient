# AClient

[![Release](https://img.shields.io/github/v/release/AgusClient/AClient?color=orange&logo=github)](https://github.com/AgusClient/AClient/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-blue?logo=windows)](https://github.com/AgusClient/AClient/releases/latest)
[![Signature](https://img.shields.io/badge/Authenticode-Signed-brightgreen?logo=windows)](https://github.com/AgusClient/AClient/releases/latest)
[![Base DDNet](https://img.shields.io/badge/Base-DDNet-red)](https://ddnet.org)
[![License](https://img.shields.io/badge/License-Zlib-green)](license.txt)

**AClient** es el cliente avanzado y competitivo para **DDraceNetwork (DDNet)** y **Teeworlds**, desarrollado sobre la base oficial de **DDNet**. Incorpora una suite completa de herramientas para moderación, gestión y preservación de demos de evidencia, sincronización de gameskins compartidos en vivo en la nube, múltiples algoritmos de respuesta rápida de entrada (Fast Input), perfiles completos de King of Gores (KoG) y DDNet, RaceStats HUD en tiempo real, HUD modular, práctica rápida con fantasma, editor de texturas y notificaciones avanzadas de escritorio.

El binario oficial para Windows x64 se distribuye compilado en modo Release y firmado digitalmente mediante certificado Authenticode (*AClient Software*).

---

## Caracteristicas Principales

### 1. Suite de Moderacion (Mod Tools)
Panel completo de administración integrado en **Ajustes -> Mod** y en el Scoreboard, diseñado para administradores y moderadores de servidores:
* **Menú Contextual Rápido en Scoreboard**: Clic sobre cualquier jugador para desplegar el panel de acciones inmediatas de sanción con motivos y duraciones predefinidas.
* **Presets Configurables de Mute (muteid)**:
  * 6 botones configurables de duración en segundos (por defecto: 60s, 300s, 900s, 3600s, 14400s y 86400s).
  * 3 razones predefinidas y editables desde el menú (por ejemplo: Spam, Insultos, Bloqueo).
* **Presets Configurables de Kick (kick)**:
  * 3 razones rápidas personalizables (por defecto: AFK, Troll, Reglas).
* **Presets Configurables de Ban (ban)**:
  * 4 botones configurables de duración en minutos (por defecto: 5m, 60m, 1440m / 1 día, 10080m / 7 días).
  * 3 razones rápidas personalizables (por defecto: Cheats, Insultos, Griefing).
* **Moderación del Dummy Local (`cl_mod_allow_target_dummy`)**: Opción de habilitar los botones de sanción contra el propio dummy local para verificar y probar el flujo de moderación sin afectar a terceros.
* **Botón Directo para Discord**: Acceso con un solo clic para abrir el canal de moderación directamente en la aplicación de Discord o en el navegador.
* **Arma de Moderación (`tc_mod_weapon`)**: Permite ejecutar un comando RCON (`tc_mod_weapon_command`, por defecto `rcon kill_pl`) apuntando y disparando directamente al jugador con el cursor, mostrando una cuenta regresiva visual sobre el tee antes de actuar.

---

### 2. Sistema de Demos y Evidencias
Herramientas avanzadas para la grabación automática de pruebas de moderación y reproducción de partidas:
* **Auto-Grabado en Sanciones (`cl_mod_record_on_punish`)**: Al confirmar una sanción de Kick o Ban sobre un jugador infractor, el cliente guarda automáticamente un clip de los últimos 3 minutos de partida mediante búfer circular en `demos/replays/`.
* **Preservación Dual de Evidencias**: El sistema conserva de forma simultánea tanto el clip cortado de 3 minutos como la demo completa de la sesión, garantizando que el moderador disponga del fragmento exacto y del contexto completo ante cualquier apelación o reporte.
* **Marcadores de Corte de Demos (Demo Slicing)**: Posibilidad de marcar puntos de inicio (`cl_demo_slice_begin`) y final (`cl_demo_slice_end`) para recortar fragmentos específicos de cualquier partida directamente desde el cliente.
* **Controles Visuales del Reproductor (Demo Player)**:
  * Velocímetro digital e indicador de velocidad en tiempo real (`cl_demo_show_speed`).
  * Indicador visual de estado de reproducción y pausa (`cl_demo_show_pause`).
  * Atajos rápidos de teclado para avanzar, retroceder y controlar la velocidad (`cl_demo_keyboard_shortcuts`).
  * Inclusión automática del nombre del jugador dentro de los metadatos de la demo (`cl_demo_name`).

---

### 3. Gameskins Compartidos en la Nube
Sistema de presencia y sincronización de apariencias personalizadas en tiempo real:
* **Visualización de Armas y Gancho por Jugador**: Cada jugador de AClient muestra sus armas y su gancho personalizados en tiempo real a todos los demás jugadores que utilicen AClient en el servidor.
* **Presencia en Tiempo Real con Supabase**: Sincronización ultrarrápida sin sobrecarga de red; actualización instantánea de presencia al equipar un skin, auto-leave al salir o cambiar de nick y limpieza automática de sesiones inactivas (TTL de 35 segundos).
* **Descarga Automática en Caliente (`cl_download_gameskins`)**: Descarga transparente de gameskins en tiempo real desde el repositorio oficial de AgusClient (`cl_gameskin_download_url`), o modo estricto local sin descargas para redes limitadas.
* **Texture Pooling en Memoria**: Motor de video optimizado que reutiliza las texturas cargadas en la GPU para tees que utilicen el mismo skin, previniendo duplicación de VRAM y eliminando caídas de fotogramas al visualizar múltiples armas.
* **Refresco Bajo Demanda**: Botón interactivo en Ajustes -> Tee y comando de consola `reload_remote_gameskins`.

---

### 4. Fast Input (Motores de Respuesta y Prediccion de Entrada)
Suite de predicción de entrada diseñada para eliminar el retardo entre la acción física del jugador y su respuesta visual en el servidor (`ac_inputs`):
* **Selector de 8 Motores de Entrada**:
  * `0: Off` - Vanilla (comportamiento estándar sin predicción de entrada adicional).
  * `1: Fast` - Predicción por milisegundos (`ac_fast_input_amount`) con soporte opcional de repredicción de gancho ante giros rápidos (`ac_fast_input_repredict_hook`).
  * `2: Best` - Algoritmo estilo BestClient con suavizado dinámico (`ac_best_input_smoothing`), compensación de latencia (`ac_best_input_latency_comp`) y 3 tipos de interpolación (lineal, cúbica y suave).
  * `3: Saiko` - Motor estilo Saiko con ajuste granular en centésimas de tick (`ac_saiko_input_amount`).
  * `4: Delta` - Algoritmo de interpolación delta (`ac_delta_input_amount`).
  * `5: F` - Motor F con cálculo en micro-ticks (`ac_f_input_amount`).
  * `6: Cloud` - Predicción de entrada Cloud (`ac_cloud_input_amount`).
  * `8: aX30` - Motor insignia de AClient con compensación adaptativa de jitter (`ac_ax30_adaptive_jitter`) y filtro de seguridad perimetral de colisión (`ac_ax30_collision_clamp`).
* **Configuración Individual por Motor**: Cada algoritmo cuenta con sus propios valores ajustables tanto para el jugador local como para otros jugadores en el servidor.
* **Predicción para Otros Jugadores**: Opciones dedicadas (`ac_ax30_others`, `ac_fast_input_others`, etc.) para aplicar predicción de movimiento sobre los demás tees en pantalla.
* **Filtro Only When Frozen (`ac_fast_input_others_only_frozen`)**: Permite aplicar la predicción sobre los demás jugadores exclusivamente cuando se encuentren congelados (freeze), manteniendo su posición normal mientras se mueven libremente.
* **Fast Input Independiente en Práctica Rápida**: Opción para usar un ajuste de entrada independiente (`ac_fast_practice_fast_input_override`) mientras se entrena en Fast Practice.

---

### 5. Caracteristicas Adicionales

#### Perfiles KoG (King of Gores) & DDNet
* **Doble Fuente de Perfiles (`ac_profile_source`)**: Consulta perfiles oficiales de DDNet o de King of Gores (KoG.tw) desde el juego mediante `profile <nombre>`.
* **Búsqueda Insensible a Mayúsculas/Minúsculas**: Encuentra nicks con mayúsculas y minúsculas indistintamente gracias a la resolución inteligente con renovación de sesión CSRF.
* **Base de Datos Local de Mapas KoG**: Categorización instantánea por dificultades (Easy, Main, Hard, Insane, Extreme y Mod).
* **Botón "Votar Dif"**: Si el mapa seleccionado pertenece a otra dificultad en el servidor actual, emite el voto de cambio de categoría con un solo clic.
* **Modo Equipo (`ac_profile_view_mode 1`)**: Analiza la lista de miembros de tu equipo actual y resalta los mapas pendientes comunes para completar juntos.

#### RaceStats HUD y Mini Statboard en Tiempo Real
* **Métricas en Vivo**: Seguimiento de Freezes, Saves (Hammer y Hook), Lasts, Eficiencia, Muertes, Net, Tiempo y Hooks.
* **Animación Suave Estilo F1 (`ac_mini_statboard_anim`)**: Transiciones visuales con destellos de color al ganar o perder puestos en carrera.
* **Miniaturas de Skins**: Representación gráfica de la skin de cada tee directamente en la tabla.
* **Persistencia Configurable (`ac_statboard_keep_on_death`)**: Opción para no reiniciar las estadísticas tras morir con `/kill` y guardado automático en el historial de carreras.

#### HUD Modular, Keystrokes y Velocimetro
* **Overlay de Teclas Pulsadas (`ac_hud_keystrokes`)**: Representación visual de movimiento, gancho, salto y disparo en pantalla con layouts configurables (WASD completo o A/D compacto).
* **Velocímetro Digital (`ac_hud_speedometer`)**: Indicador de velocidad horizontal y vectorial en unidades por segundo.
* **Frametime Numérico en HUD (`ac_show_frametime`)**: Contador numérico limpio en la esquina superior derecha sin el sufijo `ms`, en armonía con los monitores de FPS y predicción.

#### Practica Rapida (Fast Practice)
* Entrenamiento instantáneo de zonas complejas de mapas sin reiniciar la carrera.
* **Fantasma Visual Interactivo (`ac_fast_practice_ghost_alpha`)**: Transparencia graduable para comparar tu trayectoria en vivo con la pasada original.
* **Auto-Spec (`ac_fast_practice_spec_original`)**: Pasa automáticamente al modo espectador a tu tee original al iniciar la práctica.

#### Notificaciones de Escritorio y Segundo Plano
* **Banners de Windows (Toast Notifications)**: Alertas emergentes para menciones en el chat y convocatorias de votación cuando el cliente está minimizado (`cl_shownotifications`, `/notif`).
* **Alertas de Eventos en Segundo Plano**: Notificaciones por sonido, parpadeo en barra de tareas o aviso emergente al descongelarse (`ac_notify_unfreeze`) o al cambiar de casilla (`ac_change_tile_notification`).

#### Utilidades de Chat y Calidad de Vida (QoL)
* **Auto Team Lock (`ac_auto_team_lock`)**: Bloqueo automático del equipo (`/lock 1`) tras unirse a una sala con retardo ajustable (`ac_auto_team_lock_delay`).
* **Auto-Respuesta AFK (`ac_afk_auto_reply`)**: Responde automáticamente en el chat con un mensaje configurable al ser mencionado estando tabulado, con cooldown anti-spam.
* **Saludo al Último Tag (`ac_greet_message`)**: Tecla de acceso rápido para saludar al último jugador que interactuó contigo (en sala o susurro).
* **Chat Silencioso (Silent Typing)**: Oculta tu propia burbuja de escritura (`ac_silent_typing`) y las de otros jugadores (`ac_hide_others_chat_bubbles`).
* **Filtro de Mensajes de Carrera (`ac_hide_start_messages`)**: Oculta los mensajes automáticos de inicio de línea y avisos de equipo para un chat más despejado.
* **Selector de Sound Packs (`ac_sound_pack`)**: Menú visual para alternar paquetes de sonido y efectos de audio.
* **Consola F1 100% Limpia**: Filtrado total de peticiones de base de datos, sincronizaciones y avisos de idioma en la consola F1.
* **Registro de Conexiones Únicas**: Registro de sesiones en Supabase (`player_logs`) vía UPSERT, manteniendo un único registro con la última versión y fecha sin duplicar filas.

---

## Descarga e Instalacion

1. Dirígete a la sección de **[Releases Oficiales](https://github.com/AgusClient/AClient/releases/latest)**.
2. Descarga el paquete portátil **`AClient-windows.zip`**.
3. Descomprime el archivo en cualquier carpeta de tu preferencia.
4. Ejecuta **`DDNet.exe`**.

---

## Comandos y Variables de Configuracion

### Moderacion (Mod Tools)
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `cl_mod_record_on_punish` | `1` | Graba repetición de los últimos 3 minutos al sancionar (Kick/Ban) |
| `cl_mod_allow_target_dummy` | `0` | Permite aplicar acciones de moderación sobre el dummy local para pruebas |
| `tc_mod_weapon` | `0` | Activa el arma de moderación por disparo |
| `tc_mod_weapon_command` | `rcon kill_pl` | Comando ejecutado por el arma de moderación al disparar |
| `cl_mod_mute_sec1` a `cl_mod_mute_sec6` | `60` ... `86400` | Tiempos predefinidos en segundos para los 6 botones de Mute |
| `cl_mod_mute_reason1` a `cl_mod_mute_reason3` | `Spam` ... | Razones rápidas predefinidas para Mute |
| `cl_mod_kick_reason1` a `cl_mod_kick_reason3` | `AFK` ... | Razones rápidas predefinidas para Kick |
| `cl_mod_ban_min1` a `cl_mod_ban_min4` | `5` ... `10080` | Tiempos predefinidos en minutos para los 4 botones de Ban |
| `cl_mod_ban_reason1` a `cl_mod_ban_reason3` | `Cheats` ... | Razones rápidas predefinidas para Ban |

### Demos y Evidencias
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `cl_mod_record_on_punish` | `1` | Guarda el clip de 3 minutos y conserva la demo completa en `demos/replays/` |
| `cl_demo_slice_begin` | `-1` | Marca el tick de inicio para el recorte de demo |
| `cl_demo_slice_end` | `-1` | Marca el tick de fin para el recorte de demo |
| `cl_demo_show_speed` | `0` | Muestra el velocímetro en pantalla durante la reproducción de demos |
| `cl_demo_show_pause` | `1` | Muestra el indicador de pausa o reproducción al cambiar de estado |
| `cl_demo_keyboard_shortcuts` | `1` | Activa atajos de teclado rápidos en el visor de demos |
| `cl_demo_name` | `1` | Guarda el nombre del jugador dentro de los metadatos de la demo |

### Gameskins y Presencia en la Nube
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_show_remote_gameskins` | `1` | Muestra los gameskins de armas y gancho de otros usuarios de AClient |
| `cl_download_gameskins` | `1` | Descarga automática en caliente de gameskins desde el repositorio oficial |
| `cl_gameskin_download_url` | *(URL oficial)* | URL base utilizada para descargar los assets de gameskins |
| `reload_remote_gameskins` | - | Comando para forzar la recarga de gameskins remotos en la partida |

### Fast Input & Motores de Prediccion
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_inputs` | `8` | Modo de predicción activo (0: Off, 1: Fast, 2: Best, 3: Saiko, 4: Delta, 5: F, 6: Cloud, 8: aX30) |
| `ac_ax30_input_amount` | `100` | Desfase de predicción del motor aX30 en centésimas de tick |
| `ac_ax30_adaptive_jitter` | `0` | Compensación inteligente de latencia para aX30 |
| `ac_ax30_collision_clamp` | `0` | Filtro de seguridad perimetral de colisión para aX30 |
| `ac_ax30_others` | `1` | Aplica la predicción aX30 sobre los demás jugadores |
| `ac_ax30_others_amount` | `100` | Desfase aX30 para otros jugadores en centésimas de tick |
| `ac_fast_input_amount` | `20` | Margen de predicción del modo Fast en milisegundos |
| `ac_fast_input_others` | `0` | Aplica el modo Fast a otros jugadores |
| `ac_fast_input_repredict_hook` | `0` | Repredice el gancho al cambiar bruscamente de dirección |
| `ac_fast_input_others_only_frozen` | `0` | Aplica predicción a otros jugadores únicamente cuando estén congelados |
| `ac_best_input_amount` | `100` | Desfase de predicción para el modo Best en centésimas de tick |
| `ac_best_input_smoothing` | `0` | Nivel de suavizado dinámico para el modo Best (0-100%) |
| `ac_best_input_latency_comp` | `0` | Compensación de latencia para el modo Best (0-50%) |
| `ac_best_input_others` | `0` | Aplica el modo Best a los demás jugadores |
| `ac_saiko_input_amount` | `100` | Desfase del motor Saiko en centésimas de tick |
| `ac_saiko_input_others` | `0` | Aplica el motor Saiko a los demás jugadores |
| `ac_delta_input_amount` | `100` | Desfase del motor Delta en centésimas de tick |
| `ac_delta_input_others` | `0` | Aplica el motor Delta a los demás jugadores |
| `ac_f_input_amount` | `1000` | Desfase del motor F en micro-ticks |
| `ac_f_input_others` | `0` | Aplica el motor F a los demás jugadores |
| `ac_cloud_input_amount` | `100` | Desfase del motor Cloud en centésimas de tick |
| `ac_cloud_input_others` | `0` | Aplica el motor Cloud a los demás jugadores |

### Perfiles KoG / DDNet
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `profile <nombre>` | - | Abre el panel de perfil del jugador indicado |
| `ac_profile_source` | `0` | Fuente de datos para perfiles (0: DDNet, 1: King of Gores) |
| `ac_profile_view_mode` | `0` | Modo de visualización (0: Individual, 1: Equipo) |

### RaceStats & Mini Statboard
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_mini_statboard` | `0` | Activa o desactiva el overlay de Race Stats en pantalla |
| `ac_mini_statboard_sort` | `0` | Orden del ranking (0: Default, 1: Saves, 2: Muertes, 3: Freezes, 4: Eficiencia, 5: Net, 6: Tiempo, 7: Hook, 8: Lasts) |
| `ac_mini_statboard_anim` | `1` | Activa la animación suave estilo F1 ante adelantamientos |
| `ac_mini_statboard_show_tees` | `1` | Muestra las miniaturas de los tees en la tabla |
| `ac_statboard_keep_on_death` | `0` | Mantiene las estadísticas activas tras morir con `/kill` |
| `ac_statboard_auto_save` | `1` | Guarda automáticamente las estadísticas en el historial |
| `ac_mini_statboard_scale` | `70` | Escala del Mini Statboard en porcentaje |
| `ac_mini_statboard_opacity` | `60` | Opacidad de fondo del Mini Statboard |

### HUD Modular (Keystrokes, Velocimetro & Frametime)
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_hud_keystrokes` | `0` | Muestra el panel de teclas pulsadas en pantalla |
| `ac_hud_keystrokes_layout` | `0` | Diseño de teclas (0: WASD + Hook/Fire/Jump, 1: A/D compacto) |
| `ac_hud_speedometer` | `0` | Muestra el velocímetro digital en pantalla |
| `ac_show_frametime` | `0` | Muestra el tiempo de fotograma numérico en la esquina superior derecha |

### Practica Rapida (Fast Practice)
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_fast_practice_toggle` | - | Alterna el modo de práctica rápida |
| `ac_fast_practice_ghost_alpha` | `45` | Nivel de transparencia del fantasma de práctica |
| `ac_fast_practice_show_text` | `1` | Muestra el indicador superior de modo práctica |
| `ac_fast_practice_spec_original` | `0` | Pone al tee original en espectador al activar práctica |
| `ac_fast_practice_fast_input_override` | `1` | Aplica configuración independiente de Fast Input en práctica |
| `ac_fast_practice_fast_input_amount` | `20` | Desfase de Fast Input en ms para práctica rápida |

### Notificaciones de Escritorio y Segundo Plano
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `cl_shownotifications` | `0` | Habilita o silencia las notificaciones emergentes de Windows |
| `/notif` o `/notifications` | - | Comando de chat rápido para alternar o fijar notificaciones (`/notif 0` o `/notif 1`) |
| `ac_notify_unfreeze` | `0` | Alerta al descongelarse mientras el juego está minimizado |
| `ac_change_tile_notification` | `0` | Alerta al moverse o cambiar de casilla estando tabulado |
| `ac_notify_sound` | `0` | Reproduce sonido de notificación en segundo plano |
| `ac_notify_flash` | `0` | Hace parpadear la barra de tareas al recibir un evento |
| `ac_notify_desktop` | `0` | Muestra banner emergente en el escritorio |

### Traduccion Automatica
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `translate_auto` / `tc_translate_auto` | `0` | Traduce automáticamente los mensajes entrantes del chat |
| `tc_translate_target` | `"en"` | Idioma destino para la traducción de mensajes |

### Utilidades, Chat & AFK
| Comando / Variable | Valor por Defecto | Descripcion |
| :--- | :---: | :--- |
| `ac_auto_team_lock` | `0` | Bloquea el equipo automáticamente (`/lock 1`) tras unirse |
| `ac_auto_team_lock_delay` | `1` | Retardo en segundos antes de bloquear el equipo |
| `ac_afk_auto_reply` | `0` | Responde automáticamente en el chat al ser mencionado estando tabulado |
| `ac_afk_auto_reply_msg` | `"Estoy tabulado / afk"` | Mensaje emitido por la auto-respuesta AFK |
| `ac_afk_auto_reply_cooldown` | `30` | Cooldown en segundos para no repetir auto-respuesta al mismo jugador |
| `ac_greet_message` | `"wenas"` | Mensaje enviado al presionar la tecla de saludo al último tag |
| `ac_greet_whisper_mode` | `1` | Responde por susurro si el último tag recibido fue por susurro |
| `ac_afk_notify_on_return` | `0` | Avisar en chat local al volver al juego si alguien te taggeo mientras estabas tabulado |
| `ac_silent_typing` | `0` | Oculta la burbuja de escritura sobre tu propio tee |
| `ac_hide_others_chat_bubbles` | `0` | Oculta las burbujas de escritura de los demás jugadores |
| `ac_hide_start_messages` | `1` | Oculta los mensajes de línea de salida y avisos de equipo en el chat |
| `ac_sound_pack` | `"default"` | Paquete de sonido / tema de audio activo |

---

## Creditos y Reconocimientos

* **[DDraceNetwork (DDNet)](https://ddnet.org):** Desarrollado por Dennis Felsing (`def-`) y la comunidad de DDNet.
* **[Teeworlds](https://www.teeworlds.com):** Creado originalmente por Magnus Auvinen (`matricks`).
* **AClient:** Desarrollado, personalizado y mantenido por el equipo de **[AgusClient](https://github.com/AgusClient/AClient)**.
