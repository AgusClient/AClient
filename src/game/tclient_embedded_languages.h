/* AClient Embedded Translations Header */
#ifndef GAME_TCLIENT_EMBEDDED_LANGUAGES_H
#define GAME_TCLIENT_EMBEDDED_LANGUAGES_H

#include <cstring>

struct SEmbeddedTranslationEntry
{
	const char *m_pKey;
	const char *m_pEs;
	const char *m_pRu;
	const char *m_pPt;
	const char *m_pZh;
	const char *m_pFr;
};

static const SEmbeddedTranslationEntry s_aEmbeddedTranslations[] = {
	{"AClient", "AClient", "AClient", "AClient", "AClient", "AClient"},
	{"Settings & Tweaks", "Ajustes y Mejoras", "Настройки и твики", "Configurações e Ajustes", "设置与微调", "Paramètres et Ajustements"},
	{"Language", "Idioma", "Язык", "Idioma", "语言", "Langue"},
	{"AClient General Settings", "Ajustes Generales de AClient", "Общие настройки AClient", "Configurações Gerais do AClient", "AClient 常规设置", "Paramètres Généraux AClient"},
	{"General & Tweaks", "General y Ajustes", "Общие и твики", "Geral e Ajustes", "常规与微调", "Général et Ajustements"},
	{"Fast Practice (Ghost Mode)", "Fast Practice (Modo Fantasma)", "Быстрая тренировка (Режим призрака)", "Fast Practice (Modo Fantasma)", "快速练习 (幽灵模式)", "Fast Practice (Mode Fantôme)"},
	{"Allow Practice overlay/ghost rendering", "Permitir renderizado fantasma/overlay en práctica", "Разрешить оверлей/призрака в тренировке", "Permitir renderização fantasma/overlay na prática", "允许渲染练习幽灵/覆盖层", "Autoriser le rendu fantôme/overlay en pratique"},
	{"Practice ghost alpha", "Transparencia del fantasma de práctica", "Прозрачность призрака тренировки", "Transparência do fantasma de prática", "练习幽灵透明度", "Transparence du fantôme de pratique"},
	{"Show '[PRACTICE MODE]' top text", "Mostrar texto superior '[PRACTICE MODE]'", "Показывать верхний текст '[PRACTICE MODE]'", "Exibir texto superior '[PRACTICE MODE]'", "显示顶部 '[PRACTICE MODE]' 提示", "Afficher le texte supérieur '[PRACTICE MODE]'"},
	{"Disable Fast Practice (Return to Run)", "Desactivar Fast Practice (Volver a la Run)", "Отключить тренировку (Вернуться в забег)", "Desativar Fast Practice (Retornar à Run)", "关闭快速练习 (返回实际比赛)", "Désactiver Fast Practice (Retourner à la Run)"},
	{"Enable Fast Practice (Ghost Mode)", "Activar Fast Practice (Modo Fantasma)", "Включить тренировку (Режим призрака)", "Ativar Fast Practice (Modo Fantasma)", "开启快速练习 (幽灵模式)", "Activer Fast Practice (Mode Fantôme)"},
	{"Auto Team Lock & Invites", "Auto Team Lock e Invitaciones", "Авто-блокировка команды и приглашения", "Auto Bloqueio de Equipe e Convites", "自动锁队与组队邀请", "Verrouillage Automatique d'Équipe et Invitations"},
	{"Auto lock team on join (/lock 1)", "Bloquear equipo automáticamente al unirse (/lock 1)", "Авто-блокировка команды при входе (/lock 1)", "Bloquear equipe automaticamente ao entrar (/lock 1)", "进队后自动锁定队伍 (/lock 1)", "Verrouiller l'équipe automatiquement à l'entrée (/lock 1)"},
	{"Lock delay (max 5m)", "Retardo de bloqueo (máx 5m)", "Задержка блокировки (макс 5м)", "Atraso do bloqueio (máx 5m)", "锁队延迟 (最高5分钟)", "Délai de verrouillage (max 5m)"},
	{"Chat: /invite all  (invites all server players to team)", "Chat: /invite all (invita a todo el servidor al equipo)", "Чат: /invite all (пригласить весь сервер в команду)", "Chat: /invite all (convida todo o servidor para a equipe)", "聊天: /invite all (邀请全服玩家进队)", "Chat : /invite all (invite tout le serveur dans l'équipe)"},
	{"Chat: /invite friends  (invites only friends to team)", "Chat: /invite friends (invita solo a amigos al equipo)", "Чат: /invite friends (пригласить только друзей в команду)", "Chat: /invite friends (convida apenas amigos para a equipe)", "聊天: /invite friends (仅邀请好友进队)", "Chat : /invite friends (invite seulement les amis dans l'équipe)"},
	{"Invite all key (/invite all)", "Tecla para invitar a todos (/invite all)", "Клавиша пригласить всех (/invite all)", "Tecla para convidar todos (/invite all)", "一键邀请全服 (/invite all)", "Touche pour inviter tout le monde (/invite all)"},
	{"Invite friends key (/invite friends)", "Tecla para invitar amigos (/invite friends)", "Клавиша пригласить друзей (/invite friends)", "Tecla para convidar amigos (/invite friends)", "一键邀请好友 (/invite friends)", "Touche pour inviter les amis (/invite friends)"},
	{"Invite All (/invite all)", "Invitar a Todos (/invite all)", "Пригласить всех (/invite all)", "Convidar Todos (/invite all)", "邀请全服 (/invite all)", "Inviter Tout le Monde (/invite all)"},
	{"Invite Friends (/invite friends)", "Invitar Amigos (/invite friends)", "Пригласить друзей (/invite friends)", "Convidar Amigos (/invite friends)", "邀请好友 (/invite friends)", "Inviter les Amis (/invite friends)"},
	{"What's New in AClient (Quick Summary)", "Novedades de AClient (Resumen Rápido)", "Что нового в AClient (Краткий обзор)", "Novidades do AClient (Resumo Rápido)", "AClient 新功能一览", "Nouveautés de AClient (Résumé Rapide)"},
	{"* Race Stats Overlay + Log:", "* Overlay de Race Stats + Historial:", "* Оверлей статистики + Журнал:", "* Overlay do Race Stats + Histórico:", "* 比赛统计悬浮窗 + 历史日志:", "* Overlay Race Stats + Historique :"},
	{"Live HUD stats (Freezes, Saves, Lasts, Efficiency) and auto-saved run time.", "Estadísticas en vivo (Freezes, Saques, Lasts, Eficiencia) y guardado de run.", "Статистика в реальном времени (заморозки, спасения, эффективность) и сохранение времени забега.", "Estatísticas ao vivo no HUD (Freezes, Saves, Lasts, Eficiência) e tempo salvo da run.", "实时 HUD 统计 (冻结、解冻拯救、最后幸存、救援效率) 并自动保存比赛用时。", "Stats HUD en direct (Freezes, Sauvetages, Survie, Efficacité) et temps de run sauvegardé."},
	{"* Fast Practice (Ghost Mode):", "* Fast Practice (Modo Fantasma):", "* Быстрая тренировка (Режим призрака):", "* Fast Practice (Modo Fantasma):", "* 快速练习 (幽灵模式):", "* Fast Practice (Mode Fantôme) :"},
	{"Practice any map part with /tp, /tc, /invincible without leaving your real run.", "Practica cualquier parte del mapa con /tp, /tc, /invincible sin salirte de tu run.", "Тренируйте любую часть карты с /tp, /tc, /invincible не прерывая свой реальный забег.", "Pratique qualquer parte do mapa com /tp, /tc, /invincible sem sair da sua run real.", "随时使用 /tp、/tc、/invincible 在任意地点练习，不影响队伍的真实比赛。", "Entraînez-vous partout avec /tp, /tc, /invincible sans quitter votre run réelle."},
	{"* Independent Fast Input:", "* Fast Input Independiente:", "* Независимый Fast Input:", "* Fast Input Independente:", "* 独立输入预测 (Fast Input):", "* Fast Input Indépendant :"},
	{"Set separate visual prediction ms for yourself (Amount) and others (Others Amount).", "Configura ms de predicción visual distintos para ti (Amount) y los demás (Others Amount).", "Раздельная настройка миллисекунд визуального предсказания для себя (Amount) и других (Others Amount).", "Defina ms de predição visual separados para você (Amount) e outros (Others Amount).", "分别设置自身 (Amount) 与其他玩家 (Others Amount) 的独立视觉预测毫秒。", "Réglez des ms de prédiction visuelle distincts pour vous (Amount) et les autres (Others Amount)."},
	{"* Auto Team Lock (up to 5m):", "* Auto Team Lock (hasta 5m):", "* Авто-блокировка команды (до 5м):", "* Auto Team Lock (até 5m):", "* 自动队伍锁定 (最高5分钟):", "* Verrouillage d'équipe auto (jusqu'à 5m) :"},
	{"Automatically locks team (/lock 1) after configured delay when joining.", "Bloquea el equipo automáticamente (/lock 1) tras el tiempo configurado al entrar.", "Автоматически блокирует команду (/lock 1) через заданное время при входе.", "Bloqueia a equipe automaticamente (/lock 1) após o atraso configurado ao entrar.", "进队后按照设置的延迟自动执行 /lock 1 锁定队伍。", "Verrouille l'équipe automatiquement (/lock 1) après le délai défini."},
	{"* Bulk Invites:", "* Invitaciones Masivas:", "* Массовые приглашения:", "* Convites em Massa:", "* 批量队伍邀请:", "* Invitations Groupées :"},
	{"Use /invite all or /invite friends with anti-flood queue and Tab auto-complete.", "Usa /invite all o /invite friends con cola anti-flood y autocompletado con Tab.", "Команды /invite all и /invite friends с защитой от флуда и автодополнением по Tab.", "Use /invite all ou /invite friends com fila anti-flood e autocompletar com Tab.", "支持 /invite all 与 /invite friends，具备防刷屏队列及 Tab 自动补全。", "Utilisez /invite all ou /invite friends avec file anti-spam et complétion Tab."},
	{"* Active Vulkan Renderer:", "* Renderizador Vulkan Activo:", "* Активный рендерер Vulkan:", "* Renderizador Vulkan Ativo:", "* 原生 Vulkan 渲染后端:", "* Moteur de Rendu Vulkan Actif :"},
	{"Compiled SPIR-V shaders included for smoother frame rates and lower CPU usage.", "Shaders SPIR-V incluidos para mayor fluidez y menor consumo de CPU.", "Встроенные шейдеры SPIR-V для плавной частоты кадров и снижения нагрузки на ЦП.", "Shaders SPIR-V incluídos para maior fluidez e menor consumo de CPU.", "内置编译好的 SPIR-V 着色器，帧率更稳且大幅降低 CPU 占用。", "Shaders SPIR-V inclus pour un framerate plus fluide et moins d'utilisation CPU."},
	{"[AClient] You must be connected to a server to use /invite.", "[AClient] Debes estar conectado a un servidor para usar /invite.", "[AClient] Вы должны быть подключены к серверу для использования /invite.", "[AClient] Você deve estar conectado a um servidor para usar /invite.", "[AClient] 必须在服务器在线状态下才能使用 /invite。", "[AClient] Vous devez être connecté à un serveur pour utiliser /invite."},
	{"[AClient] Join a team first (e.g. /team 1) before using /invite.", "[AClient] Entra primero a un equipo (ej. /team 1) antes de usar /invite.", "[AClient] Сначала вступите в команду (например, /team 1) перед вызовом /invite.", "[AClient] Entre em uma equipe primeiro (ex. /team 1) antes de usar /invite.", "[AClient] 请先加入队伍 (例如 /team 1) 再使用 /invite。", "[AClient] Rejoignez une équipe d'abord (ex. /team 1) avant d'utiliser /invite."},
	{"[AClient] No connected friends outside your team found to invite.", "[AClient] No hay amigos conectados fuera de tu equipo para invitar.", "[AClient] Друзей вне вашей команды для приглашения не найдено.", "[AClient] Nenhum amigo conectado fora da sua equipe encontrado para convidar.", "[AClient] 未发现队伍外的好友在线。", "[AClient] Aucun ami connecté trouvé hors de votre équipe à inviter."},
	{"[AClient] No other players outside your team found to invite.", "[AClient] No hay otros jugadores fuera de tu equipo para invitar.", "[AClient] Других игроков вне вашей команды для приглашения не найдено.", "[AClient] Nenhum outro jogador fora da sua equipe encontrado para convidar.", "[AClient] 未发现队伍外的其他玩家。", "[AClient] Aucun autre joueur trouvé hors de votre équipe à inviter."},
	{"[AClient] Inviting %d %s to Team %d...", "[AClient] Invitando a %d %s al Equipo %d...", "[AClient] Приглашение %d %s в Команду %d...", "[AClient] Convidando %d %s para a Equipe %d...", "[AClient] 正在邀请 %d 名%s加入队伍 %d...", "[AClient] Invitation de %d %s dans l'Équipe %d..."},
	{"friend(s)", "amigo(s)", "друзей", "amigo(s)", "好友", "ami(s)"},
	{"player(s)", "jugador(es)", "игроков", "jogador(es)", "玩家", "joueur(s)"},
	{"Fast Practice active! Ghost mode ON (/tp, /tc, /unsolo). Type /practice or use Settings to return.", "¡Fast Practice activo! Modo Fantasma ON (/tp, /tc, /unsolo). Escribe /practice o usa Ajustes para volver.", "Быстрая тренировка активна! Режим призрака ВКЛ (/tp, /tc, /unsolo). Напишите /practice для возврата.", "Fast Practice ativo! Modo Fantasma ON (/tp, /tc, /unsolo). Digite /practice para retornar.", "快速练习已激活！幽灵模式开启 (/tp, /tc, /unsolo)。输入 /practice 或在设置中返回比赛。", "Fast Practice activé ! Mode Fantôme ON (/tp, /tc, /unsolo). Tapez /practice pour revenir."},
	{"Fast Practice disabled. Run state restored!", "¡Fast Practice desactivado! Estado de la run restaurado.", "Быстрая тренировка отключена. Состояние забега восстановлено!", "Fast Practice desativado! Estado da run restaurado.", "快速练习已关闭，比赛状态已恢复！", "Fast Practice désactivé. État de la run restauré !"},
	{"Keep stats on death (do not reset on /kill)", "Conservar estadísticas al morir (no reiniciar al matarse)", "Сохранять статистику при смерти (не сбрасывать при /kill)", "Manter estatísticas ao morrer (não reiniciar no /kill)", "死亡时保留统计数据 (自杀 /kill 时不重置)", "Conserver les statistiques à la mort (ne pas réinitialiser au /kill)"},
	{"Column: Deaths", "Columna: Muertes", "Колонка: Смерти", "Coluna: Mortes", "显示列: 死亡次数", "Colonne : Morts"},
{"Background Alerts (Tabbed out)", "Alertas en Segundo Plano (Tabeado)", "Оповещения в фоновом режиме", "Alertas em Segundo Plano", "后台挂起提醒", "Alertes en Arrière-plan"},
	{"Notify when unfreezing (tabbed out)", "Avisar al descongelarme (tabeado)", "Оповещать при разморозке в фоне", "Avisar ao descongelar (em segundo plano)", "后台解除冰冻时提醒", "Alerter lors du dégel (arrière-plan)"},
	{"Notify when moved or tile changes (tabbed out)", "Avisar si me mueven / cambio de tile (tabeado)", "Оповещать при смене тайла или перемещении", "Avisar se me moverem / mudar de bloco", "后台被拉拽或移动方块时提醒", "Alerter si déplacé ou changement de case"},
	{"Alert methods (independent):", "Métodos de alerta (independientes):", "Способы оповещения (независимые):", "Métodos de alerta (independentes):", "提醒方式 (可独立多选):", "Méthodes d'alerte (indépendantes) :"},
	{"Sound alert", "Sonido de alerta", "Звуковое оповещение", "Som de alerta", "声音提醒", "Alerte sonore"},
	{"Taskbar flash", "Parpadeo en la barra de tareas", "Мигание на панели задач", "Piscar na barra de tarefas", "任务栏图标闪烁", "Clignotement barre des tâches"},
	{"Desktop notification", "Notificación de escritorio", "Уведомление на рабочем столе", "Notificação na área de trabalho", "桌面弹窗通知", "Notification de bureau"},
	{"Skin Stealer", "Skin Stealer (Copiar Skins)", "Кража скина (Копирование)", "Copiar Skins (Skin Stealer)", "一键复制皮肤 (Skin Stealer)", "Voleur de Skin (Copie)"},
	{"Steal skin key (aiming at player)", "Tecla para copiar skin (apuntando al jugador)", "Клавиша копирования скина (по прицелу)", "Tecla para copiar skin (mirando no jogador)", "瞄准一键复制皮肤快捷键", "Touche pour copier le skin (viser le joueur)"},
	{"Chat: /stealskin [name]  or  Console: steal_skin [name]", "Chat: /stealskin [nombre]  |  Consola: steal_skin [nombre]", "Чат: /stealskin [ник]  |  Консоль: steal_skin [ник]", "Chat: /stealskin [nome]  |  Console: steal_skin [nome]", "聊天: /stealskin [名字]  |  控制台: steal_skin [名字]", "Chat : /stealskin [nom]  |  Console : steal_skin [nom]"},
	{"You can also right-click / click on any player in Scoreboard (Tab) to steal skin.", "También puedes hacer clic sobre cualquier jugador en el Scoreboard (Tab) para copiar su skin.", "Также можно нажать на любого игрока в таблице счета (Tab), чтобы скопировать скин.", "Você também pode clicar em qualquer jogador no Scoreboard (Tab) para copiar a skin.", "也可在 Tab 计分板中点击任意玩家菜单选择复制皮肤。", "Vous pouvez aussi cliquer sur n'importe quel joueur dans le Scoreboard (Tab) pour copier son skin."},
	{"Steal skin from aimed player now", "Copiar skin del jugador apuntado ahora", "Скопировать скин игрока под прицелом", "Copiar skin do jogador mirado agora", "立即复制当前瞄准玩家的皮肤", "Copier le skin du joueur visé maintenant"},
	{"Steal Skin", "Copiar Skin", "Копировать скин", "Copiar Skin", "复制皮肤", "Copier Skin"},
	{"You were unfrozen!", "¡Has sido descongelado!", "Вас разморозили!", "Você foi descongelado!", "你已被解冻！", "Vous avez été dégelé !"},
	{"You were moved or changed tile!", "¡Te han movido o has cambiado de casilla!", "Вас переместили или сменился тайл!", "Você foi movido ou mudou de bloco!", "你被移动或位置发生变化！", "Vous avez été déplacé ou avez changé de case !"},
	{"[AClient] Skin copied from %s: %s", "[AClient] Skin copiada de %s: %s", "[AClient] Скин скопирован у %s: %s", "[AClient] Skin copiada de %s: %s", "[AClient] 已复制 %s 的皮肤: %s", "[AClient] Skin copié de %s : %s"},
	{"[AClient] No target player found to steal skin from.", "[AClient] No se encontró ningún jugador objetivo para copiar skin.", "[AClient] Целевой игрок для копирования скина не найден.", "[AClient] Nenhum jogador encontrado para copiar skin.", "[AClient] 未找到可复制皮肤的目标玩家。", "[AClient] Aucun joueur cible trouvé pour copier le skin."},
	{"[AClient] You cannot steal your own skin.", "[AClient] No puedes copiar tu propia skin.", "[AClient] Нельзя скопировать свой собственный скин.", "[AClient] Você não pode copiar sua própria skin.", "[AClient] 无法复制自己的皮肤。", "[AClient] Vous ne pouvez pas copier votre propre skin."},
	{"* Background Alerts (Tabbed out):", "* Alertas en Segundo Plano (Tabeado):", "* Оповещения в фоновом режиме:", "* Alertas em Segundo Plano:", "* 后台切屏提醒:", "* Alertes en Arrière-plan :"},
	{"Granular alerts (sound, taskbar flash, desktop notification) on unfreeze or tile change.", "Avisos configurables (sonido, parpadeo de ventana, notificación de escritorio) al descongelarte o moverte.", "Настраиваемые оповещения (звук, мигание в панели задач, уведомление) при разморозке или движении.", "Avisos configuráveis (som, piscar da janela, notificação) ao descongelar ou se mover.", "支持声音、任务栏闪烁与桌面通知多重独立提醒，在解冻或被移动时触发。", "Alertes personnalisables (son, clignotement barre, notification) lors du dégel ou déplacement."},
	{"* Skin Stealer:", "* Skin Stealer (Copiar Skins):", "* Кража скина:", "* Copiar Skins (Skin Stealer):", "* 一键复制皮肤 (Skin Stealer):", "* Voleur de Skin :"},
	{"Instantly copy skins and colors by aiming with bind, via /stealskin [name] or from Scoreboard (Tab).", "Copia al instante la skin y colores apuntando con tu bind, con /stealskin [nombre] o desde el Scoreboard.", "Мгновенно копируйте скин и цвета игрока по прицелу биндом, через /stealskin [ник] или из таблицы счета.", "Copie instantaneamente skins e cores mirando com bind, via /stealskin [nome] ou pelo Scoreboard.", "通过准星绑定快捷键、/stealskin [名字] 指令或 Tab 计分板一键直接复制玩家皮肤与颜色。", "Copiez instantanément les skins et couleurs en visant, via /stealskin [nom] ou depuis le Scoreboard."},
{"Chat & Client Messages (Echo)", "Chat y Mensajes de Cliente (Echo)", "Чат и клиентские сообщения (Echo)", "Chat e Mensagens do Cliente (Echo)", "聊天与客户端提示信息 (Echo)", "Chat et Messages Client (Echo)"},
	{"Show client messages in chat (Echo / Local notices)", "Mostrar mensajes de cliente en chat (Echo / Avisos locales)", "Показывать сообщения клиента в чате (Echo / локальные оповещения)", "Exibir mensagens do cliente no chat (Echo / Avisos locais)", "在聊天栏显示客户端本地提示 (Echo / 本地通知)", "Afficher les messages client dans le chat (Echo / Avis locaux)"},
	{"Controls whether local echo messages (like [AClient], command feedback and notices) appear in the chat.", "Controla si los mensajes locales de echo (como [AClient], confirmaciones y avisos) aparecen en el chat.", "Управляет отображением локальных эхо-сообщений (таких как [AClient], подтверждения команд и уведомления) в чате.", "Controla se mensagens locais de echo (como [AClient], confirmações de comandos e avisos) aparecem no chat.", "控制是否在聊天框中显示客户端本地回显信息 (例如 [AClient]、指令执行反馈与提示)。", "Contrôle si les messages d'écho locaux (comme [AClient], confirmations de commande et avis) s'affichent dans le chat."},
	{"Send Test Echo Message", "Enviar Mensaje de Prueba (Echo Test)", "Отправить тестовое эхо-сообщение", "Enviar Mensagem de Teste (Echo Test)", "发送测试回显消息 (Echo 测试)", "Envoyer un Message Test (Test Echo)"},
	{"[AClient] This is a test Echo message.", "[AClient] Este es un mensaje de prueba de Echo.", "[AClient] Это тестовое эхо-сообщение.", "[AClient] Esta é uma mensagem de teste de Echo.", "[AClient] 这是一条 Echo 测试提示消息。", "[AClient] Ceci est un message test d'Echo."},
	{"* Client Echo Control:", "* Control de Mensajes Echo:", "* Управление эхо-сообщениями:", "* Controle de Mensagens Echo:", "* 客户端回显控制:", "* Contrôle des Messages d'Écho :"},
	{"Enable or silence local client notices ([AClient], command confirmations, script echoes).", "Activa o silencia avisos locales ([AClient], confirmación de comandos y ecos de scripts).", "Включение или отключение локальных уведомлений клиента ([AClient], подтверждения команд, эхо скриптов).", "Ative ou silencie avisos locais ([AClient], confirmações de comandos e ecos de scripts).", "开启或静音客户端本地提示 ([AClient]、指令反馈与脚本回显)。", "Activez ou coupez les avis locaux ([AClient], confirmations de commande, échos de script)."},
	{"Shows the compact live team statistics table on your screen during the game.", "Muestra la tabla compacta de estadísticas de equipo en pantalla durante la partida.", "Отображает компактную таблицу статистики команды на экране во время игры.", "Mostra a tabela compacta de estatísticas da equipe na tela durante a partida.", "在游戏屏幕上显示团队实时紧凑统计表。", "Affiche le tableau compact des statistiques d'équipe à l'écran pendant la partie."},
	{"Keeps your accumulated statistics intact even if you or a teammate dies or uses /kill.", "Conserva acumuladas las estadísticas aunque tú o tu compañero mueran (/kill).", "Сохраняет накопленную статистику, даже если вы или тиммейт погибли (/kill).", "Mantém as estatísticas acumuladas mesmo se você ou seu colega morrerem (/kill).", "即便自己或队友阵亡 (/kill)，也保留累计统计数据。", "Conserve les statistiques accumulées même en cas de mort ou de /kill."},
	{"Automatically resets statistics whenever the team dies or you switch rooms (/team).", "Reinicia automáticamente las estadísticas al morir el equipo o al cambiar de sala (/team).", "Автоматически сбрасывает статистику при гибели команды или смене (/team).", "Redefine automaticamente as estatísticas quando a equipe morre ou troca de sala (/team).", "当队伍全灭或切换队伍 (/team) 时自动重置统计数据。", "Réinitialise automatiquement les statistiques à la mort de l'équipe ou changement de /team."},
	{"Automatically saves current run stats to RaceStats history file before resetting.", "Guarda automáticamente la partida en el historial de RaceStats antes de reiniciar.", "Автоматически сохраняет статистику в историю RaceStats перед сбросом.", "Salva automaticamente a partida no histórico do RaceStats antes de redefinir.", "在重置前自动将当前记录保存到 RaceStats 历史记录中。", "Sauvegarde automatiquement les stats de la course dans l'historique avant réinitialisation."},
	{"Shows how many times each player was frozen during the current run.", "Muestra cuántas veces el jugador cayó en freeze durante la ronda actual.", "Показывает, сколько раз игрок замерзал за текущий забег.", "Mostra quantas vezes o jogador congelou na rodada atual.", "显示每位玩家在当前轮次中被冰冻的次数。", "Indique combien de fois le joueur a été gelé pendant la course."},
	{"Shows how many times each player unfroze a teammate using the hammer.", "Muestra cuántas veces el jugador salvó/descongeló a un compañero con el martillo.", "Показывает, сколько раз игрок разморозил тиммейта молотом.", "Mostra quantas vezes o jogador salvou/descongelou um parceiro com martelo.", "显示每位玩家使用锤子解冻救起队友的次数。", "Indique combien de fois le joueur a dégélé un coéquipier au marteau."},
	{"Shows the number of deaths or /kill for each player during the current run.", "Muestra la cantidad de muertes o /kill de cada jugador en la ronda actual.", "Показывает количество смертей или /kill каждого игрока в текущем забеге.", "Mostra o número de mortes ou /kill de cada jogador na rodada atual.", "显示每位玩家在当前轮次中的阵亡或 /kill 次数。", "Indique le nombre de morts ou /kill de chaque joueur pendant la course."},
	{"Save success rate: Saves / (Saves + Freezes) %. Dynamically shifts colors from Red (0%) to Emerald Green (100%).", "Tasa de éxito: Saves / (Saves + Freezes) %. Cambia dinámicamente de Rojo (0%) a Verde Esmeralda (100%).", "Эффективность: Saves / (Saves + Freezes) %. Плавно меняет цвет от красного (0%) до зелёного (100%).", "Taxa de sucesso: Saves / (Saves + Freezes) %. Muda dinamicamente de Vermelho (0%) para Verde Esmeralda (100%).", "救援效率: Saves / (Saves + Freezes) %。颜色从红 (0%) 动态过渡至翡翠绿 (100%)。", "Taux d'efficacité: Saves / (Saves + Freezes) %. Dégradé dynamique du Rouge (0%) au Vert Émeraude (100%)."},
	{"Column: Net Balance (+/-)", "Columna: Balance Neto (+/-)", "Колонка: Чистый баланс (+/-)", "Coluna: Balanço Líquido (+/-)", "列: 净贡献值 (+/-)", "Colonne : Bilan Net (+/-)"},
	{"Direct difference between Saves and Freezes (Saves - Freezes). Green if positive (+), Gray if zero, Red if negative (-).", "Diferencia directa: Saves - Freezes. Verde si es positivo (+), Gris si es cero y Rojo si es negativo (-).", "Разница Saves - Freezes. Зелёный при плюсе (+), серый при нуле, красный при минусе (-).", "Diferença direta: Saves - Freezes. Verde se positivo (+), Cinza se zero e Vermelho se negativo (-).", "净救援差值 (Saves - Freezes)。正数绿色 (+)，归零灰色，负数红色 (-)。", "Différence directe: Saves - Freezes. Vert si positif (+), Gris si zéro, Rouge si négatif (-)."},
	{"Column: Frozen Time", "Columna: Tiempo Congelado", "Колонка: Время в заморозке", "Coluna: Tempo Congelado", "列: 被冻时长", "Colonne : Temps Gelé"},
	{"Total accumulated time the player spent stuck inside freeze during the run (e.g. 15s or 1m 24s).", "Tiempo total acumulado que el jugador pasó atrapado en freeze durante la ronda (ej. 15s o 1m 24s).", "Суммарное время, проведённое игроком в заморозке за забег (напр. 15s или 1m 24s).", "Tempo total acumulado que o jogador passou preso no freeze na rodada (ex: 15s ou 1m 24s).", "玩家在当前跑图中处于冰冻状态的累计总时间 (例如 15s 或 1m 24s)。", "Temps total cumulé passé gelé pendant la course (ex: 15s ou 1m 24s)."},
	{"Column: Hook Accuracy", "Columna: Precisión de Gancho", "Колонка: Точность хука", "Coluna: Precisão de Gancho", "列: 抓钩命中率", "Colonne : Précision du Grappin"},
	{"Hook hit accuracy: hooks that latched onto a player vs total hooks fired.", "Porcentaje de precisión: hooks que atraparon a un jugador vs total de hooks lanzados al aire.", "Метрика точности: процент хуков, попавших в игрока, от общего числа бросков.", "Precisão do gancho: ganchos que agarraram um jogador vs total de ganchos disparados.", "抓钩命中率: 成功抓取玩家的钩子次数 / 发射钩子总次数 %。", "Précision du grappin : grappins ayant touché un joueur par rapport au total tiré."},
	{"Times the player was the sole unfrozen survivor alive in the team (requires 15+ ticks stabilization).", "Veces que el jugador quedó como el único miembro descongelado con vida en su equipo (requiere 15+ ticks).", "Сколько раз игрок оставался единственным незамороженным выжившим в команде (от 15 тиков).", "Vezes que o jogador foi o único sobrevivente descongelado na equipe (mínimo 15 ticks).", "成为队伍中唯一存活未被冰冻玩家的次数 (防抖需保持 15+ ticks 以上)。", "Nombre de fois où le joueur était le seul survivant non-gelé de l'équipe (15+ ticks requis)."},
	{"* Race Stats HUD & History:", "* Race Stats HUD e Historial:", "* Race Stats HUD и история:", "* Race Stats HUD e Histórico:", "* 竞赛实时 HUD 与历史记录:", "* HUD Race Stats et Historique :"},
	{"Live stats (Freezes, Saves, Deaths, Dynamic % Eff, Net +/-, Frozen time, Hook accuracy) & auto-saved log.", "Stats en vivo (Freezes, Saves, Muertes, % Eficiencia dinámico, Net +/-, Tiempo congelado, Precisión) e historial.", "Живая статистика (Freezes, Saves, Смерти, динамический % Eff, Баланс +/-, Время, Точность) и авто-лог.", "Estatísticas ao vivo (Freezes, Saves, Mortes, % Eficiência dinâmico, Net +/-, Tempo, Precisão) e histórico.", "实时团队数据 (冰冻、解冻救援、阵亡、动态 % 效率、净分值、被冻时长、抓钩率) 及自动保存日志。", "Stats en direct (Freezes, Saves, Morts, % Eff dynamique, Bilan +/-, Temps gelé, Précision) et historique."},
	{"* Auto-Invite Friends:", "* Invitación Automática a Amigos:", "* Авто-приглашение друзей:", "* Convidar Amigos Automaticamente:", "* 一键邀请在线好友:", "* Invitation Automatique d'Amis :"},
	{"Invite all online friends to your team with /invite friends, with anti-flood queue and Tab auto-complete.", "Invita a todos tus amigos conectados al equipo con /invite friends (cola anti-flood y tecla rápida).", "Пригласите всех друзей онлайн в команду командой /invite friends (защита от флуда и автодополнение).", "Convide todos os amigos online para sua equipe com /invite friends (fila anti-flood e atalho).", "使用 /invite friends 邀请全服在线好友入队，带防刷屏队列及 Tab 自动补全。", "Invitez tous vos amis connectés avec /invite friends, avec file anti-spam et complétion Tab."},
	{"[AClient] /invite all has been disabled. Use /invite friends.", "[AClient] /invite all ha sido desactivado. Usa /invite friends.", "[AClient] /invite all отключена. Используйте /invite friends.", "[AClient] /invite all foi desativado. Use /invite friends.", "[AClient] /invite all 已停用，请使用 /invite friends。", "[AClient] /invite all a été désactivé. Utilisez /invite friends."},
	{"Silent typing (Hide 3-dots bubble when typing)", "Escritura silenciosa (Ocultar burbuja de 3 puntos al escribir)", "Бесшумный ввод (Скрыть облачко при вводе)", "Digitação silenciosa (Ocultar bolha de 3 pontos)", "静默打字 (输入时隐藏三点聊天气泡)", "Saisie silencieuse (Masquer la bulle de 3 points)"},
	{"Prevents the 3-dots bubble (...) from appearing above your Tee when the chat is open.", "Evita que aparezca la burbuja con los 3 puntos (...) sobre tu Tee cuando tienes el chat abierto o estás escribiendo.", "Предотвращает появление облачка с точками (...) над вашим персонажем, когда открыт чат.", "Evita que a bolha com 3 pontos (...) apareça sobre seu Tee quando o chat estiver aberto.", "当聊天框打开时，防止在你的提子头顶显示三点聊天气泡 (...)", "Empêche la bulle avec les 3 points (...) d'apparaître au-dessus de votre Tee lorsque le chat est ouvert."},
	{"Hide typing bubbles of other players", "Ocultar burbujas de chat de los demás jugadores", "Скрыть облачка чата других игроков", "Ocultar bolhas de chat dos outros jogadores", "隐藏其他玩家的聊天气泡", "Masquer les bulles de chat des autres joueurs"},
	{"Hides the 3-dots typing bubble of other players on your screen when they are typing.", "Oculta visualmente en tu pantalla la burbuja de 3 puntos sobre los demás jugadores cuando ellos estén escribiendo.", "Скрывает на вашем экране облачко с точками у других игроков, когда они пишут.", "Oculta visualmente em sua tela a bolha de 3 pontos sobre os outros jogadores quando eles estiverem digitando.", "当其他玩家正在输入时，在你的屏幕上隐藏他们头顶的三点气泡。", "Masque visuellement sur votre écran la bulle de 3 points au-dessus des autres joueurs lorsqu'ils écrivent."},
	{"Show All Stats", "Activar Todas", "Все статистики", "Mostrar Todas", "显示所有数据", "Toutes les stats"},
	{"Basic Stats", "Solo Básicas", "Основные", "Básicas", "基础数据", "Stats de base"},
	{"Put original in spec on practice start (/spec)", "Poner original en spec al iniciar práctica (/spec)", "Отправлять оригинал в спектаторы при старте (/spec)", "Colocar original em spec ao iniciar prática (/spec)", "开启练习时将原本体设为旁观 (/spec)", "Mettre l'original en spec au démarrage (/spec)"},
	{"Puts your original Tee into spectator mode (/spec) when entering Fast Practice, keeping control on your practice character.", "Pone a tu Tee original en modo espectador (/spec) al entrar a Fast Practice, manteniendo el control sobre tu personaje de práctica.", "Переводит вашего оригинального персонажа в режим наблюдателя (/spec) при запуске тренировки.", "Coloca seu Tee original em modo espectador (/spec) ao entrar no Fast Practice.", "进入快速练习时将原本体设为观众模式 (/spec)，同时保持对练习提子的控制。", "Passe votre Tee original en mode spectateur (/spec) en entrant en Fast Practice."},
	{"AClient Settings", "Ajustes de AClient", "Настройки AClient", "Configurações do AClient", "AClient 设置", "Paramètres AClient"},
	{"RaceStats History", "Historial de RaceStats", "История RaceStats", "Histórico de RaceStats", "RaceStats 历史记录", "Historique RaceStats"},
	{"Fast Input Mode:", "Modo de Fast Input:", "Режим Fast Input:", "Modo de Fast Input:", "Fast Input 模式:", "Mode Fast Input :"},
	{"Fast Input others", "Fast Input a otros", "Fast Input для других", "Fast Input para outros", "对其他玩家应用 Fast Input", "Fast Input pour les autres"},
	{"Best input others", "Best Input a otros", "Best Input для других", "Best Input para outros", "对其他玩家应用 Best Input", "Best Input pour les autres"},
	{"Saiko input others", "Saiko Input a otros", "Saiko Input для других", "Saiko Input para outros", "对其他玩家应用 Saiko Input", "Saiko Input pour les autres"},
	{"Delta input others", "Delta Input a otros", "Delta Input для других", "Delta Input para outros", "对其他玩家应用 Delta Input", "Delta Input pour les autres"},
	{"F input others", "F Input a otros", "F Input для других", "F Input para outros", "对其他玩家应用 F Input", "F Input pour les autres"},
	{"Cloud input others", "Cloud Input a otros", "Cloud Input для других", "Cloud Input para outros", "对其他玩家应用 Cloud Input", "Cloud Input pour les autres"},
	{"Others offset", "Offset otros tees", "Смещение для других", "Offset outros tees", "其他玩家偏移量", "Décalage autres tees"},
	{"Prediction offset", "Offset de predicción", "Смещение предсказания", "Offset de predição", "预测偏移量", "Décalage de prédiction"},
	{"Smoothing", "Suavizado", "Сглаживание", "Suavização", "平滑度", "Lissage"},
	{"Latency compensation", "Compensación de latencia", "Компенсация задержки", "Compensação de latência", "延迟补偿", "Compensation de latence"},
	{"Interpolation", "Interpolación", "Интерполяция", "Interpolação", "插值模式", "Interpolation"},
	{"Linear", "Lineal", "Линейная", "Linear", "线性", "Linéaire"},
	{"Cubic", "Cúbica", "Кубическая", "Cúbica", "三次 Hermite", "Cubique"},
	{"Smooth", "Suave", "Плавная", "Suave", "平滑自适应", "Lisse"},
	{"ticks", "ticks", "тиков", "ticks", "刻 (ticks)", "ticks"},
	{"Off", "Desactivado", "Выкл", "Desativado", "关闭", "Désactivé"},
};

static const char *TCLocalizeEmbedded(const char *pStr, const char *pLangFile)
{
	if(!pStr || !pLangFile || !pLangFile[0])
		return nullptr;

	int LangIndex = -1; // 0=es, 1=ru, 2=pt, 3=zh, 4=fr
	if(std::strstr(pLangFile, "spanish")) LangIndex = 0;
	else if(std::strstr(pLangFile, "russian")) LangIndex = 1;
	else if(std::strstr(pLangFile, "portuguese")) LangIndex = 2;
	else if(std::strstr(pLangFile, "chinese")) LangIndex = 3;
	else if(std::strstr(pLangFile, "french")) LangIndex = 4;
	else return nullptr;

	for(const auto &Entry : s_aEmbeddedTranslations)
	{
		if(std::strcmp(Entry.m_pKey, pStr) == 0)
		{
			switch(LangIndex)
			{
			case 0: return Entry.m_pEs;
			case 1: return Entry.m_pRu;
			case 2: return Entry.m_pPt;
			case 3: return Entry.m_pZh;
			case 4: return Entry.m_pFr;
			}
		}
	}
	return nullptr;
}

#endif
