/* Clientx30 Embedded Translations Header */
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
	{"Clientx30", "Clientx30", "Clientx30", "Clientx30", "Clientx30", "Clientx30"},
	{"Settings & Tweaks", "Ajustes y Mejoras", "Настройки и твики", "Configurações e Ajustes", "设置与微调", "Paramètres et Ajustements"},
	{"Language", "Idioma", "Язык", "Idioma", "语言", "Langue"},
	{"Clientx30 General Settings", "Ajustes Generales de Clientx30", "Общие настройки Clientx30", "Configurações Gerais do Clientx30", "Clientx30 常规设置", "Paramètres Généraux Clientx30"},
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
	{"What's New in Clientx30 (Quick Summary)", "Novedades de Clientx30 (Resumen Rápido)", "Что нового в Clientx30 (Краткий обзор)", "Novidades do Clientx30 (Resumo Rápido)", "Clientx30 新功能一览", "Nouveautés de Clientx30 (Résumé Rapide)"},
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
	{"[Clientx30] You must be connected to a server to use /invite.", "[Clientx30] Debes estar conectado a un servidor para usar /invite.", "[Clientx30] Вы должны быть подключены к серверу для использования /invite.", "[Clientx30] Você deve estar conectado a um servidor para usar /invite.", "[Clientx30] 必须在服务器在线状态下才能使用 /invite。", "[Clientx30] Vous devez être connecté à un serveur pour utiliser /invite."},
	{"[Clientx30] Join a team first (e.g. /team 1) before using /invite.", "[Clientx30] Entra primero a un equipo (ej. /team 1) antes de usar /invite.", "[Clientx30] Сначала вступите в команду (например, /team 1) перед вызовом /invite.", "[Clientx30] Entre em uma equipe primeiro (ex. /team 1) antes de usar /invite.", "[Clientx30] 请先加入队伍 (例如 /team 1) 再使用 /invite。", "[Clientx30] Rejoignez une équipe d'abord (ex. /team 1) avant d'utiliser /invite."},
	{"[Clientx30] No connected friends outside your team found to invite.", "[Clientx30] No hay amigos conectados fuera de tu equipo para invitar.", "[Clientx30] Друзей вне вашей команды для приглашения не найдено.", "[Clientx30] Nenhum amigo conectado fora da sua equipe encontrado para convidar.", "[Clientx30] 未发现队伍外的好友在线。", "[Clientx30] Aucun ami connecté trouvé hors de votre équipe à inviter."},
	{"[Clientx30] No other players outside your team found to invite.", "[Clientx30] No hay otros jugadores fuera de tu equipo para invitar.", "[Clientx30] Других игроков вне вашей команды для приглашения не найдено.", "[Clientx30] Nenhum outro jogador fora da sua equipe encontrado para convidar.", "[Clientx30] 未发现队伍外的其他玩家。", "[Clientx30] Aucun autre joueur trouvé hors de votre équipe à inviter."},
	{"[Clientx30] Inviting %d %s to Team %d...", "[Clientx30] Invitando a %d %s al Equipo %d...", "[Clientx30] Приглашение %d %s в Команду %d...", "[Clientx30] Convidando %d %s para a Equipe %d...", "[Clientx30] 正在邀请 %d 名%s加入队伍 %d...", "[Clientx30] Invitation de %d %s dans l'Équipe %d..."},
	{"friend(s)", "amigo(s)", "друзей", "amigo(s)", "好友", "ami(s)"},
	{"player(s)", "jugador(es)", "игроков", "jogador(es)", "玩家", "joueur(s)"},
	{"Fast Practice active! Ghost mode ON (/tp, /tc, /unsolo). Type /practice or use Settings to return.", "¡Fast Practice activo! Modo Fantasma ON (/tp, /tc, /unsolo). Escribe /practice o usa Ajustes para volver.", "Быстрая тренировка активна! Режим призрака ВКЛ (/tp, /tc, /unsolo). Напишите /practice для возврата.", "Fast Practice ativo! Modo Fantasma ON (/tp, /tc, /unsolo). Digite /practice para retornar.", "快速练习已激活！幽灵模式开启 (/tp, /tc, /unsolo)。输入 /practice 或在设置中返回比赛。", "Fast Practice activé ! Mode Fantôme ON (/tp, /tc, /unsolo). Tapez /practice pour revenir."},
	{"Fast Practice disabled. Run state restored!", "¡Fast Practice desactivado! Estado de la run restaurado.", "Быстрая тренировка отключена. Состояние забега восстановлено!", "Fast Practice desativado! Estado da run restaurado.", "快速练习已关闭，比赛状态已恢复！", "Fast Practice désactivé. État de la run restauré !"},
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
