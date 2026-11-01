#ifndef MACRO_CONFIG_INT
#error "This file should only be included through config_includes.h"
#endif

// ==========================================
// AClient Configuration Variables
// Saved to: settings_aclient.cfg (AppData\Roaming\DDNet)
// ==========================================

// Chat / Local Echo / Silent Typing
MACRO_CONFIG_INT(AcShowChatClient, ac_show_chat_client, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat messages from the client such as echo")
MACRO_CONFIG_INT(AcSilentTyping, ac_silent_typing, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide your own typing chat bubble (Silent Typing)")
MACRO_CONFIG_INT(AcHideChatBubbles, ac_hide_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide your own typing chat bubble (alias for ac_silent_typing)")
MACRO_CONFIG_INT(AcHideOthersChatBubbles, ac_hide_others_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide typing chat bubbles of other players on your screen")
MACRO_CONFIG_INT(AcHideStartMessages, ac_hide_start_messages, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide start line and team start warning messages in chat")

// RaceStats (Mini Statboard)
MACRO_CONFIG_INT(AcMiniStatboard, ac_mini_statboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show compact team stats in HUD")
MACRO_CONFIG_INT(AcMiniStatboardColumns, ac_mini_statboard_columns, 255, 0, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Visible columns bitmask: 1=Freezes, 2=Saves, 4=Lasts, 8=Eff, 16=Deaths, 32=Net, 64=Time, 128=Hook")
MACRO_CONFIG_INT(AcMiniStatboardSort, ac_mini_statboard_sort, 0, 0, 8, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sort mini statboard by stat (0=Default, 1=Saves, 2=Deaths, 3=Freezes, 4=Efficiency, 5=Net, 6=Time, 7=Hook, 8=Lasts)")
MACRO_CONFIG_INT(AcMiniStatboardAnim, ac_mini_statboard_anim, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth F1-style ladder animation when ranks change in mini statboard")
MACRO_CONFIG_INT(AcMiniStatboardAnimSpeed, ac_mini_statboard_anim_speed, 10, 3, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animation speed for F1-style rank transitions (3=slow, 10=default, 30=fast)")
MACRO_CONFIG_INT(AcMiniStatboardRankUpColor, ac_mini_statboard_rank_up_color, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show green flash color when gaining rank in mini statboard")
MACRO_CONFIG_INT(AcMiniStatboardRankDownColor, ac_mini_statboard_rank_down_color, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show red flash color when losing rank in mini statboard")
MACRO_CONFIG_INT(AcMiniStatboardShowTees, ac_mini_statboard_show_tees, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show mini Tee skins in stats overlay (default: 1)")
MACRO_CONFIG_INT(AcStatboardKeepOnDeath, ac_statboard_keep_on_death, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep overlay stats on /kill (do not reset on death)")
MACRO_CONFIG_INT(AcStatboardResetOnTeamKill, ac_statboard_reset_on_team_kill, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reset live stats on /kill or team change")
MACRO_CONFIG_INT(AcStatboardAutoSave, ac_statboard_auto_save, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-save race stats to history before resetting on /kill or /team")
MACRO_CONFIG_INT(AcRaceStatsMaxLogs, ac_racestats_max_logs, 50, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max saved race stats logs in history (0 = unlimited)")
MACRO_CONFIG_INT(AcRaceStatsMinDuration, ac_racestats_min_duration, 10, 0, 3600, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimum run duration in seconds to save race stats log")
MACRO_CONFIG_INT(AcRaceStatsSaveOnFinish, ac_racestats_save_on_finish, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-save race stats log when completing the map")
MACRO_CONFIG_INT(AcRaceStatsSaveOnlyOnFinish, ac_racestats_save_only_on_finish, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Save race stats log ONLY when completing the map")
MACRO_CONFIG_INT(AcMiniStatboardMaxPlayers, ac_mini_statboard_max_players, 5, 0, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Max visible players in statboard")
MACRO_CONFIG_INT(AcMiniStatboardScale, ac_mini_statboard_scale, 70, 20, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mini statboard scale percent")
MACRO_CONFIG_INT(AcMiniStatboardOpacity, ac_mini_statboard_opacity, 60, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mini statboard background opacity")
MACRO_CONFIG_INT(AcMiniStatboardX, ac_mini_statboard_x, 95, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mini statboard HUD X position percent")
MACRO_CONFIG_INT(AcMiniStatboardY, ac_mini_statboard_y, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mini statboard HUD Y position percent")

// Fast Practice
MACRO_CONFIG_INT(AcFastPracticeGhostAlpha, ac_fast_practice_ghost_alpha, 45, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ghost transparency percentage in Fast Practice mode")
MACRO_CONFIG_INT(AcFastPracticeShowText, ac_fast_practice_show_text, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show top practice mode text (/tc /invincible) in Fast Practice")
MACRO_CONFIG_INT(AcFastPracticeSpecOriginal, ac_fast_practice_spec_original, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Put original tee in spectator mode (/spec) when activating Fast Practice")

// Auto Team Lock
MACRO_CONFIG_INT(AcAutoTeamLock, ac_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically lock team (/lock 1) after joining a team")
MACRO_CONFIG_INT(AcAutoTeamLockDelay, ac_auto_team_lock_delay, 1, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay in seconds before sending /lock 1 after joining a team")

// Background Notifications
MACRO_CONFIG_INT(AcNotifyUnfreeze, ac_notify_unfreeze, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notify when unfreezing while tabbed out")
MACRO_CONFIG_INT(AcChangeTileNotification, ac_change_tile_notification, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Notify when moved or tile changes while tabbed out")
MACRO_CONFIG_INT(AcNotifySound, ac_notify_sound, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play sound on background notification")
MACRO_CONFIG_INT(AcNotifyFlash, ac_notify_flash, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Flash taskbar on background notification")
MACRO_CONFIG_INT(AcNotifyDesktop, ac_notify_desktop, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show desktop notification banner on background notification")

// Fast Input Prediction Modes & Amounts
MACRO_CONFIG_INT(AcInputs, ac_inputs, 0, 0, 8, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input prediction mode (0 = off, 1 = fast, 2 = best, 3 = saiko, 4 = delta, 5 = f, 6 = cloud, 7 = aclient, 8 = ax30)")

// aX30 Mode
MACRO_CONFIG_INT(AcAX30InputAmount, ac_ax30_input_amount, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "aX30 prediction offset in 0.01 ticks")
MACRO_CONFIG_INT(AcAX30AdaptiveJitter, ac_ax30_adaptive_jitter, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "aX30 adaptive latency compensation")
MACRO_CONFIG_INT(AcAX30CollisionClamp, ac_ax30_collision_clamp, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "aX30 boundary safety filter")
MACRO_CONFIG_INT(AcAX30Interpolation, ac_ax30_interpolation, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "aX30 motion profile preset (1-3)")
MACRO_CONFIG_INT(AcAX30Others, ac_ax30_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply aX30 to other players")
MACRO_CONFIG_INT(AcAX30OthersAmount, ac_ax30_others_amount, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "aX30 prediction offset for other players in 0.01 ticks")

// AClient Mode (Ultimate Fast Input: Best architecture without smoothing, sub-tick interpolation, latency comp)
MACRO_CONFIG_INT(AcAClientInputAmount, ac_aclient_input_amount, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient input prediction amount in 0.01 ticks")
MACRO_CONFIG_INT(AcAClientInputLatencyComp, ac_aclient_input_latency_comp, 0, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient input latency compensation (0-50%)")
MACRO_CONFIG_INT(AcAClientInputInterpolation, ac_aclient_input_interpolation, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient input interpolation mode (1 = linear, 2 = cubic, 3 = smooth)")
MACRO_CONFIG_INT(AcAClientInputOthers, ac_aclient_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply AClient input to other tees")
MACRO_CONFIG_INT(AcAClientInputOthersAmount, ac_aclient_input_others_amount, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient input prediction amount for other tees in 0.01 ticks")

// Fast Mode
MACRO_CONFIG_INT(AcFastInputAmount, ac_fast_input_amount, 20, 1, 40, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fast input prediction offset in ms")
MACRO_CONFIG_INT(AcFastInputOthers, ac_fast_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply fast input to other tees")
MACRO_CONFIG_INT(AcFastInputOthersAmount, ac_fast_input_others_amount, 10, 1, 40, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fast input prediction offset for other tees in ms")

// Best Mode
MACRO_CONFIG_INT(AcBestInputAmount, ac_best_input_amount, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input prediction amount in 0.01 ticks")
MACRO_CONFIG_INT(AcBestInputSmoothing, ac_best_input_smoothing, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input smoothing amount (0-100%)")
MACRO_CONFIG_INT(AcBestInputLatencyComp, ac_best_input_latency_comp, 0, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input latency compensation (0-50%)")
MACRO_CONFIG_INT(AcBestInputInterpolation, ac_best_input_interpolation, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input interpolation mode (1 = linear, 2 = cubic, 3 = smooth)")
MACRO_CONFIG_INT(AcBestInputOthers, ac_best_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply best input to other tees")
MACRO_CONFIG_INT(AcBestInputOthersAmount, ac_best_input_others_amount, 100, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input prediction amount for other tees in 0.01 ticks")

// Saiko Mode
MACRO_CONFIG_INT(AcSaikoInputAmount, ac_saiko_input_amount, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Saiko input amount in 0.01 ticks")
MACRO_CONFIG_INT(AcSaikoInputOthers, ac_saiko_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply Saiko input to other tees")
MACRO_CONFIG_INT(AcSaikoInputOthersAmount, ac_saiko_input_others_amount, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Saiko input amount for other tees in 0.01 ticks")

// Delta Mode
MACRO_CONFIG_INT(AcDeltaInputAmount, ac_delta_input_amount, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delta input amount in 0.01 ticks")
MACRO_CONFIG_INT(AcDeltaInputOthers, ac_delta_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply delta input to other tees")
MACRO_CONFIG_INT(AcDeltaInputOthersAmount, ac_delta_input_others_amount, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delta input amount for other tees in 0.01 ticks")

// F Mode
MACRO_CONFIG_INT(AcFInputAmount, ac_f_input_amount, 1000, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "F input amount in 0.001-tick units")
MACRO_CONFIG_INT(AcFInputOthers, ac_f_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply F input to other tees")
MACRO_CONFIG_INT(AcFInputOthersAmount, ac_f_input_others_amount, 1000, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "F input amount for other tees in 0.001-tick units")

// Cloud Mode
MACRO_CONFIG_INT(AcCloudInputAmount, ac_cloud_input_amount, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cloud input amount in 0.01 ticks")
MACRO_CONFIG_INT(AcCloudInputOthers, ac_cloud_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply cloud input to other tees")
MACRO_CONFIG_INT(AcCloudInputOthersAmount, ac_cloud_input_others_amount, 100, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cloud input amount for other tees in 0.01 ticks")

// Repredict Hook
MACRO_CONFIG_INT(AcFastInputRepredictHook, ac_fast_input_repredict_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Repredict fast input hook when direction changes")

// Fast Input Others Filter
MACRO_CONFIG_INT(AcFastInputOthersOnlyFrozen, ac_fast_input_others_only_frozen, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only apply fast input prediction to other tees when they are frozen")

// ==========================================
// AClient Modular HUD (Beta)
// ==========================================
MACRO_CONFIG_INT(AcHudKeystrokes, ac_hud_keystrokes, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show on-screen Keystrokes overlay (Beta)")
MACRO_CONFIG_INT(AcHudKeystrokesX, ac_hud_keystrokes_x, 3, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keystrokes HUD horizontal position percent")
MACRO_CONFIG_INT(AcHudKeystrokesY, ac_hud_keystrokes_y, 72, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keystrokes HUD vertical position percent")
MACRO_CONFIG_INT(AcHudKeystrokesScale, ac_hud_keystrokes_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keystrokes HUD scale percent")
MACRO_CONFIG_INT(AcHudKeystrokesAlpha, ac_hud_keystrokes_alpha, 85, 20, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keystrokes HUD opacity percent")
MACRO_CONFIG_INT(AcHudKeystrokesLayout, ac_hud_keystrokes_layout, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keystrokes layout: 0=WASD + LMB/RMB/Space, 1=A/D + Hook/Fire/Jump")

MACRO_CONFIG_INT(AcHudSpeedometer, ac_hud_speedometer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show digital Speedometer / Movement Info HUD (Beta)")
MACRO_CONFIG_INT(AcHudSpeedometerX, ac_hud_speedometer_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedometer HUD horizontal position percent")
MACRO_CONFIG_INT(AcHudSpeedometerY, ac_hud_speedometer_y, 82, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedometer HUD vertical position percent")
MACRO_CONFIG_INT(AcHudSpeedometerScale, ac_hud_speedometer_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedometer HUD scale percent")
MACRO_CONFIG_INT(AcHudSpeedometerAlpha, ac_hud_speedometer_alpha, 85, 20, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedometer HUD opacity percent")

// Profiles (0: DDNet, 1: KoG)
MACRO_CONFIG_INT(AcProfileSource, ac_profile_source, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Profile data source (0: DDNet, 1: KoG)")
MACRO_CONFIG_INT(AcProfileViewMode, ac_profile_view_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Profile view mode (0: Individual, 1: Team)")

// Custom Gameskin Sharing & Downloads
MACRO_CONFIG_INT(AcShowRemoteGameSkins, ac_show_remote_gameskins, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show other AClient players' custom gameskins")
MACRO_CONFIG_INT(ClDownloadGameskins, cl_download_gameskins, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Download gameskins on-the-fly")
MACRO_CONFIG_STR(ClGameskinDownloadUrl, cl_gameskin_download_url, 128, "https://raw.githubusercontent.com/AgusClient/gameskins/main/", CFGFLAG_CLIENT | CFGFLAG_SAVE, "URL used to download gameskins")
MACRO_CONFIG_STR(AcPresenceUrl, ac_presence_url, 128, "https://hmrgihzjynnnappzbvdd.supabase.co", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Presence relay server URL")

// AClient Authentication & HWID
MACRO_CONFIG_INT(ClAuthRequired, cl_auth_required, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Requerir inicio de sesion con modal al iniciar (0: apagado, 1: encendido)")
MACRO_CONFIG_STR(ClAuthUser, cl_auth_user, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient authentication username")
MACRO_CONFIG_STR(ClAuthPass, cl_auth_pass, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient authentication password")
MACRO_CONFIG_INT(ClAuthRemember, cl_auth_remember, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Remember AClient login credentials")
MACRO_CONFIG_STR(ClAuthHwid, cl_auth_hwid, 65, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "AClient machine hardware ID")

// Greet & Last Tag System
MACRO_CONFIG_STR(AcGreetMessage, ac_greet_message, 128, "wenas", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mensaje de saludo al presionar el bind de saludar al ultimo que te taggeo")
MACRO_CONFIG_INT(AcGreetWhisperMode, ac_greet_whisper_mode, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Responder por susurro si el ultimo tag fue un susurro recibido")

// AFK / Tabbed out Auto-Reply
MACRO_CONFIG_INT(AcAfkAutoReply, ac_afk_auto_reply, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-responder si te taggean mientras el juego esta tabulado (AFK)")
MACRO_CONFIG_STR(AcAfkAutoReplyMsg, ac_afk_auto_reply_msg, 128, "Estoy tabulado / afk", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mensaje de auto-respuesta cuando estas tabulado y te taggean")
MACRO_CONFIG_INT(AcAfkAutoReplyCooldown, ac_afk_auto_reply_cooldown, 30, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cooldown en segundos para no repetir auto-respuesta al mismo jugador")
MACRO_CONFIG_INT(AcAfkNotifyOnReturn, ac_afk_notify_on_return, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Avisar en chat local al volver al juego si alguien te taggeo mientras estabas tabulado")

// Sound Packs
MACRO_CONFIG_STR(AcSoundPack, ac_sound_pack, 128, "default", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Current sound pack / audio theme")
