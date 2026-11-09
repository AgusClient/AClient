#include "tclient.h"

#include "data_version.h"

#include <base/io.h>
#include <base/log.h>

#include <engine/client.h>
#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/external/tinyexpr.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

static constexpr const char *ACLIENT_LATEST_RELEASE_URL = "https://api.github.com/repos/AgusClient/AClient/releases";

CTClient::CTClient()
{
	OnReset();

	// Fallback offline releases
	CAClientRelease Rel272;
	str_copy(Rel272.m_aTag, "v2.7.2", sizeof(Rel272.m_aTag));
	str_copy(Rel272.m_aName, "AClient v2.7.2", sizeof(Rel272.m_aName));
	str_copy(Rel272.m_aPublishedAt, "2026-11-08", sizeof(Rel272.m_aPublishedAt));
	Rel272.m_Body =
		"## Novedades de AClient v2.7.2\n\n"
		"### Selector Personalizable de Modos de Fast Input\n"
		"- **Panel de Visibilidad Dinamico**: Permite seleccionar exactamente que modos de input se muestran en la barra principal mediante el icono de lapiz.\n"
		"- **Distribucion Autoajustable**: Distribuye dinamicamente el ancho de los botones segun la cantidad de modos visibles.\n\n"
		"### Coexistencia aX30 (Original) y aXv2 (Version 2.0)\n"
		"- **aX30 (Formula Clasica)**: Retorna la formula original Catmull-Rom con Horner y filtro clasico.\n"
		"- **aXv2 (Motor Avanzado)**: Motor 2.0 con Hermite cubica monotona (Fritsch-Carlson), blend de velocidad fisica, analisis RTT y clamp volumetrico subtick.\n"
		"- **Modo AClient (Fallback)**: Formula directa 1:1 activable desde el menu de seleccion visible.\n"
		"- **Ajustes y Sliders Independientes**: Cada modo conserva sus valores y toggles de manera aislada.";
	m_vReleases.push_back(Rel272);

	CAClientRelease Rel271;
	str_copy(Rel271.m_aTag, "v2.7.1", sizeof(Rel271.m_aTag));
	str_copy(Rel271.m_aName, "AClient v2.7.1", sizeof(Rel271.m_aName));
	str_copy(Rel271.m_aPublishedAt, "2026-11-07", sizeof(Rel271.m_aPublishedAt));
	Rel271.m_Body =
		"## Novedades de AClient v2.7.1\n\n"
		"### Motor de Prediccion aX30 2.0 (Subticks, Trayectoria y Red)\n"
		"- **Lookahead Real de +1 Tick (P_next) y StartTick**: La simulacion predictiva calcula un tick adicional futuro y registra el estado base del snapshot para alimentar una curva autentica de 4 puntos sin degradarse a cuadratica.\n"
		"- **Hermite Cubica Monotona con Velocidad Fisica (Fritsch-Carlson)**: Combina tangentes geometricas con `m_Vel` real de `CCharacterCore` y recorte monotono sin rebote fantasma (overshoot) al frenar contra muros o techos.\n"
		"- **Compensacion Adaptativa de Jitter por RTT Real (`NETMSG_INPUTTIMING`)**: Mide el RTT en nanosegundos, el margen de llegada al servidor (`TimeLeft`) y el jitter de llegada de snapshots (~50 Hz) con suavizado exponencial (`tau = 250ms`) y limitador de pendiente.\n"
		"- **Filtro Volumetrico de Contorno (`TestBox 28x28` + Biseccion)**: Sustituye el chequeo puntual por la caja real del tee y busqueda binaria de 6 pasos para rozar paredes y bordes de freeze sin tirones hacia atras.\n"
		"- **Gancho Sincronizado en Subticks (`GetFastInputHookPos`)**: Interpola `m_HookPos` y `m_HookState` en el mismo subtick exacto que `m_RenderPos`, eliminando el retraso visual de la cuerda.\n\n"
		"### Sub-Tick Aiming Cronologico y Fast Input\n"
		"- **Captura Cronologica Pre-Click en SDL**: Asocia el delta acumulado de `SDL_MOUSEMOTION` previo al clic dentro del mismo fotograma en `IInput::CEvent`, eliminando la contaminacion post-clic a altos Hz de raton.\n"
		"- **Soporte Universal para Binds Compuestos y Macros**: Captura el flanco de subida directamente en `ConKeyInputState` (`+hook`) y `ConKeyInputCounter` (`+fire`), funcionando con cualquier bind compuesto o alias y respetando macros de angulo.\n"
		"- **Cuantizacion Unificada y `cl_fast_input_others` Consistente**: Alinea el redondeo `round_to_int` de `TargetX/TargetY` y restringe el bloqueo de punteria en `CheckNewInput` unicamente al paquete con flanco de subida pendiente.";
	m_vReleases.push_back(Rel271);

	CAClientRelease Rel270;
	str_copy(Rel270.m_aTag, "v2.7.0", sizeof(Rel270.m_aTag));
	str_copy(Rel270.m_aName, "AClient v2.7.0", sizeof(Rel270.m_aName));
	str_copy(Rel270.m_aPublishedAt, "2026-11-07", sizeof(Rel270.m_aPublishedAt));
	Rel270.m_Body = "";
	m_vReleases.push_back(Rel270);

	CAClientRelease Rel269;
	str_copy(Rel269.m_aTag, "v2.6.9", sizeof(Rel269.m_aTag));
	str_copy(Rel269.m_aName, "AClient v2.6.9", sizeof(Rel269.m_aName));
	str_copy(Rel269.m_aPublishedAt, "2026-11-07", sizeof(Rel269.m_aPublishedAt));
	Rel269.m_Body = "";
	m_vReleases.push_back(Rel269);

	CAClientRelease Rel268;
	str_copy(Rel268.m_aTag, "v2.6.8", sizeof(Rel268.m_aTag));
	str_copy(Rel268.m_aName, "AClient v2.6.8", sizeof(Rel268.m_aName));
	str_copy(Rel268.m_aPublishedAt, "2026-11-07", sizeof(Rel268.m_aPublishedAt));
	Rel268.m_Body =
		"## Novedades de AClient v2.6.8\n\n"
		"### Gestor de Skins Descargadas en Recursos (Assets)\n"
		"- **Nueva pestaña Descargas**: Explora, equipa, previsualiza y elimina skins descargadas en vivo de otros jugadores desde Ajustes -> Recursos.\n"
		"- **Carga Perezosa (Lazy Loading)**: Carga progresiva de miniaturas de hasta 2 skins por fotograma solo en casillas visibles, garantizando fluidez sin microcortes ni saturacion de VRAM.\n"
		"- **Integracion total con Assets Editor**: Soporte completo para utilizar skins descargadas como base o donante en el editor interno.\n\n"
		"### Suite FPS Booster & Optimizador\n"
		"- **Fog Dinamico por Zoom**: Nuevo modo porcentual que adapta el area de renderizado util de forma proporcional al nivel de zoom de la camara.\n"
		"- **Selector de Bordes de Fog**: Interruptor explicito OFF/ON con recuadro simetrico de 2px en Vulkan y OpenGL que se oculta al abrir menus.\n"
		"- **Descarte AABB unificado**: Optimizacion de entidades, nameplates, freeze bars, estelas y proyectiles.\n\n"
		"### Relacion de Aspecto Personalizada y Bandas Negras\n"
		"- **Aspect Ratio & Black Bars**: Modos 4:3, 16:10, 5:4, 16:9, 21:9 y personalizados con bandas negras solidas y paridad en Vulkan y OpenGL.\n"
		"- **Modo en partida**: Retorno automatico a pantalla completa nativa al pausar o abrir menus de configuracion.\n\n"
		"### Historial de Chat y Desplazamiento\n"
		"- **Scroll del Chat**: Desplazamiento comodo con la rueda del raton o teclas RePag/AvPag sin cambiar de arma.\n"
		"- **Historial Ampliado**: Capacidad configurable desde 64 hasta 512 mensajes con liberacion de memoria inmediata.\n\n"
		"### Motor y Rendimiento\n"
		"- **Vulkan 1.1.0 por defecto**: Activado por defecto en Windows con renderizado de alto rendimiento.\n"
		"- **RaceStats optimizado**: Pausa de calculos de carrera mientras el overlay permanezca oculto.";
	m_vReleases.push_back(Rel268);

	CAClientRelease Rel267;
	str_copy(Rel267.m_aTag, "v2.6.7", sizeof(Rel267.m_aTag));
	str_copy(Rel267.m_aName, "AClient v2.6.7", sizeof(Rel267.m_aName));
	str_copy(Rel267.m_aPublishedAt, "2026-11-07", sizeof(Rel267.m_aPublishedAt));
	Rel267.m_Body =
		"## Novedades de AClient v2.6.7\n\n"
		"### Personalizacion de Tema y Colores (AClient Theme & UI)\n"
		"- **Colores independientes**: Tres opciones separadas para personalizar el tema moderno: color de la marca X en opciones ON/OFF, color del borde de tarjetas y bloques, y color del fondo con control de transparencia (Alpha).\n"
		"- **Correcciones de interfaz**: Corregidos los bordes redondeados y subrayados que sobresalian en las pestañas laterales del menu de ajustes.\n\n"
		"### Insignia Oficial AC 30 en Marcador (TAB)\n"
		"- **Nuevo logo en TAB**: Los jugadores que utilizan AClient ahora lucen la insignia oficial AC 30 en lugar de la marca x generica, con adaptacion de opacidad al morir.\n\n"
		"### Visor de Novedades y Versiones\n"
		"- **Desplegable de releases**: Selector interactivo en Configuracion -> AClient para explorar todas las actualizaciones publicadas y consultar sus notas de cambios completas.\n\n"
		"### Opciones de Tee y Menu\n"
		"- **Icono de opciones sobre el Tee**: Opcion configurable para mostrar el icono de menu sobre el personaje al presionar Escape o abrir ajustes.\n"
		"- **Descarga condicional de skins**: Ajuste para descargar skins unicamente mientras se esta congelado.\n\n"
		"### Distribucion\n"
		"- **Paquete Portable Completo (AClient-windows.zip)**: Incluye ejecutable, librerias DLL y assets con shaders Vulkan.\n"
		"- **Binario Independiente (DDNet.exe)**: Actualizacion directa del ejecutable.";
	m_vReleases.push_back(Rel267);

	CAClientRelease Rel266;
	str_copy(Rel266.m_aTag, "v2.6.6", sizeof(Rel266.m_aTag));
	str_copy(Rel266.m_aName, "AClient v2.6.6", sizeof(Rel266.m_aName));
	str_copy(Rel266.m_aPublishedAt, "2026-11-06", sizeof(Rel266.m_aPublishedAt));
	Rel266.m_Body =
		"## Novedades de AClient v2.6.6\n\n"
		"### Soporte de Vulkan por Defecto\n"
		"- **Backend Vulkan habilitado por defecto en Windows**: Se corrigio la configuracion de CMake para que el backend grafico de Vulkan y sus 38 shaders SPIR-V (.spv) se encuentren compilados y enlazados de manera predeterminada.\n\n"
		"### Correccion de Proteccion Cloudflare en KoG (Anti-Spam F1)\n"
		"- **Eliminacion del bucle de reintentos infinitos**: Cuando la web de King of Gores (kog.tw) activa la proteccion anti-bot de Cloudflare, el cliente ya no satura la conexion ni inunda la consola F1 con advertencias continuas.\n"
		"- **Cooldown inteligente**: Implementado un tiempo de espera de 60 segundos ante bloqueos de Cloudflare y registro temporal de jugadores fallidos para evitar re-consultas en cada fotograma mientras se esta en equipo.\n"
		"- **Rate-limit de advertencias**: El mensaje en consola ahora se limita como maximo a una vez por minuto.\n\n"
		"### Ajustes de Identidad en Tee y Descargas\n"
		"- **Edicion integrada**: Nombre, clan y selector de pais/bandera (con modal y filtro) accesibles y unificados dentro del apartado Tee tanto para jugador como para dummy.\n"
		"- **Descarga de skins solo al congelar**: Opcion para descargar skins de la comunidad unicamente mientras el Tee se encuentra congelado.\n\n"
		"### Distribucion\n"
		"- **Paquete Portable Completo (AClient-windows.zip)**: Incluye todas las librerias DLL, assets (data/) con shaders Vulkan compilados y el ejecutable oficial DDNet.exe.\n"
		"- **Binario Independiente (DDNet.exe)**: Para actualizar directamente el ejecutable.";
	m_vReleases.push_back(Rel266);

	CAClientRelease Rel265;
	str_copy(Rel265.m_aTag, "v2.6.5", sizeof(Rel265.m_aTag));
	str_copy(Rel265.m_aName, "AClient v2.6.5", sizeof(Rel265.m_aName));
	str_copy(Rel265.m_aPublishedAt, "2026-11-04", sizeof(Rel265.m_aPublishedAt));
	Rel265.m_Body =
		"## Novedades de AClient v2.6.5\n\n"
		"### Descarga Condicional de Gameskins\n"
		"- **Descargas configurables bajo demanda**: En Configuracion -> Tee, se incorporan opciones para controlar cuando se bajan los gameskins de otros jugadores.\n"
		"- **Cuando estoy freezeado**: Realiza las descargas unicamente mientras tu Tee permanece inmovilizado/congelado, garantizando 0 micro-tirones mientras te mueves.\n"
		"- **Aviso en Chat**: Notificacion visual local en el chat al iniciar y al finalizar la descarga de cada gameskin.\n\n"
		"### Mejoras de Interfaz Moderna (UI)\n"
		"- **Tema Dark-Glass & Violet Hover**: Nuevo diseno estetico moderno con bordes redondeados y efectos glassmorphism.\n"
		"- **Fast Input Reordenado**: Priorizacion de configuraciones de red y prediccion al inicio del menu.";
	m_vReleases.push_back(Rel265);
}

void CTClient::ConRandomTee(IConsole::IResult *pResult, void *pUserData) {}

void CTClient::ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// Resolve type to randomize
	// Check length of type (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag)
	bool RandomizeBody = false;
	bool RandomizeFeet = false;
	bool RandomizeSkin = false;
	bool RandomizeFlag = false;

	if(pResult->NumArguments() == 0)
	{
		RandomizeBody = true;
		RandomizeFeet = true;
		RandomizeSkin = true;
		RandomizeFlag = true;
	}
	else if(pResult->NumArguments() == 1)
	{
		const char *Type = pResult->GetString(0);
		int Length = Type ? str_length(Type) : 0;
		if(Length == 1 && Type[0] == '0')
		{ // Randomize all
			RandomizeBody = true;
			RandomizeFeet = true;
			RandomizeSkin = true;
			RandomizeFlag = true;
		}
		else if(Length == 1)
		{
			// Randomize body
			RandomizeBody = Type[0] == '1';
		}
		else if(Length == 2)
		{
			// Check for body and feet
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
		}
		else if(Length == 3)
		{
			// Check for body, feet and skin
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
		}
		else if(Length == 4)
		{
			// Check for body, feet, skin and flag
			RandomizeBody = Type[0] == '1';
			RandomizeFeet = Type[1] == '1';
			RandomizeSkin = Type[2] == '1';
			RandomizeFlag = Type[3] == '1';
		}
	}

	if(RandomizeBody)
		RandomBodyColor();
	if(RandomizeFeet)
		RandomFeetColor();
	if(RandomizeSkin)
		RandomSkin(pUserData);
	if(RandomizeFlag)
		RandomFlag(pUserData);
	pThis->GameClient()->SendInfo(false);
}

void CTClient::OnInit()
{
	TextRender()->SetCustomFace(g_Config.m_TcCustomFont);
	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();

	// Cleanup old binary if left over from previous in-game update
	Storage()->RemoveBinaryFile("DDNet.old");

	FetchTClientInfo();

	char aError[512] = "";
	if(!Storage()->FileExists("tclient/gui_logo.png", IStorage::TYPE_ALL))
		str_format(aError, sizeof(aError), TCLocalize("%s not found", DATA_VERSION_PATH), "data/tclient/gui_logo.png");
	if(aError[0] == '\0')
		CheckDataVersion(aError, sizeof(aError), Storage()->OpenFile(DATA_VERSION_PATH, IOFLAG_READ, IStorage::TYPE_ALL));
	if(aError[0] != '\0')
	{
		SWarning Warning(aError, TCLocalize("You have probably only installed the TClient DDNet.exe which is not supported, please use the entire TClient folder", "data_version.h"));
		Client()->AddWarning(Warning);
	}
}

static bool LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHL = str_utf8_find_nocase(pLine, pName);
	if(pHL)
	{
		int Length = str_length(pName);
		if(Length > 0 && (pLine == pHL || pHL[-1] == ' ') && (pHL[Length] == 0 || pHL[Length] == ' ' || pHL[Length] == '.' || pHL[Length] == '!' || pHL[Length] == ',' || pHL[Length] == '?' || pHL[Length] == ':'))
			return true;
	}
	return false;
}

bool CTClient::SendNonDuplicateMessage(int Team, const char *pLine)
{
	if(str_comp(pLine, m_PreviousOwnMessage) != 0)
	{
		GameClient()->m_Chat.SendChat(Team, pLine);
		return true;
	}
	str_copy(m_PreviousOwnMessage, pLine);
	return false;
}

void CTClient::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		int ClientId = pMsg->m_ClientId;

		if(ClientId < 0 || ClientId > MAX_CLIENTS)
			return;
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(ClientId == LocalId)
			str_copy(m_PreviousOwnMessage, pMsg->m_pMessage);

		bool PingMessage = false;

		bool ValidIds = !(GameClient()->m_aLocalIds[0] < 0 || (GameClient()->Client()->DummyConnected() && GameClient()->m_aLocalIds[1] < 0));

		if(ValidIds && ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && (!GameClient()->Client()->DummyConnected() || ClientId != GameClient()->m_aLocalIds[1]))
		{
			PingMessage |= LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[0]].m_aName);
			PingMessage |= GameClient()->Client()->DummyConnected() && LineShouldHighlight(pMsg->m_pMessage, GameClient()->m_aClients[GameClient()->m_aLocalIds[1]].m_aName);
		}

		if(pMsg->m_Team == TEAM_WHISPER_RECV)
			PingMessage = true;

		if(!PingMessage)
			return;

		char aPlayerName[MAX_NAME_LENGTH];
		str_copy(aPlayerName, GameClient()->m_aClients[ClientId].m_aName, sizeof(aPlayerName));

		bool PlayerMuted = GameClient()->m_aClients[ClientId].m_Foe || GameClient()->m_aClients[ClientId].m_ChatIgnore;
		if(g_Config.m_TcAutoReplyMuted && PlayerMuted)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMutedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}

		bool WindowActive = m_pGraphics && m_pGraphics->WindowActive();
		if(g_Config.m_TcAutoReplyMinimized && !WindowActive && m_pGraphics)
		{
			char aBuf[256];
			if(pMsg->m_Team == TEAM_WHISPER_RECV || ServerCommandExists("w"))
				str_format(aBuf, sizeof(aBuf), "/w %s %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			else
				str_format(aBuf, sizeof(aBuf), "%s: %s", aPlayerName, g_Config.m_TcAutoReplyMinimizedMessage);
			SendNonDuplicateMessage(0, aBuf);
			return;
		}
	}

	if(MsgType == NETMSGTYPE_SV_VOTESET)
	{
		const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy]; // Do not care about spec behaviour
		const bool Afk = LocalId >= 0 && GameClient()->m_aClients[LocalId].m_Afk; // TODO Depends on server afk time
		CNetMsg_Sv_VoteSet *pMsg = (CNetMsg_Sv_VoteSet *)pRawMsg;
		if(pMsg->m_Timeout && !Afk)
		{
			char aDescription[VOTE_DESC_LENGTH];
			char aReason[VOTE_REASON_LENGTH];
			str_copy(aDescription, pMsg->m_pDescription);
			str_copy(aReason, pMsg->m_pReason);
			bool KickVote = str_startswith(aDescription, "Kick ") != nullptr ? true : false;
			bool SpecVote = str_startswith(aDescription, "Pause ") != nullptr ? true : false;
			bool SettingVote = !KickVote && !SpecVote;
			bool RandomMapVote = SettingVote && str_find_nocase(aDescription, "random");
			bool MapCoolDown = SettingVote && (str_find_nocase(aDescription, "change map") || str_find_nocase(aDescription, "no not change map"));
			bool CategoryVote = SettingVote && (str_find_nocase(aDescription, "☐") || str_find_nocase(aDescription, "☒"));
			bool FunVote = SettingVote && str_find_nocase(aDescription, "funvote");
			bool MapVote = SettingVote && !RandomMapVote && !MapCoolDown && !CategoryVote && !FunVote && (str_find_nocase(aDescription, "Map:") || str_find_nocase(aDescription, "★") || str_find_nocase(aDescription, "✰"));

			if(g_Config.m_TcAutoVoteWhenFar && (MapVote || RandomMapVote))
			{
				int RaceTime = 0;
				if(GameClient()->m_Snap.m_pGameInfoObj && GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
					RaceTime = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();

				if(RaceTime / 60 >= g_Config.m_TcAutoVoteWhenFarTime)
				{
					CGameClient::CClientData *pVoteCaller = nullptr;
					int CallerId = -1;
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(!GameClient()->m_aStats[i].IsActive())
							continue;

						char aBuf[MAX_NAME_LENGTH + 4];
						str_format(aBuf, sizeof(aBuf), "\'%s\'", GameClient()->m_aClients[i].m_aName);
						if(str_find_nocase(pMsg->m_pDescription, aBuf) != nullptr)
						{
							pVoteCaller = &GameClient()->m_aClients[i];
							CallerId = i;
							break;
						}
					}
					if(pVoteCaller)
					{
						bool Friend = pVoteCaller->m_Friend;
						bool SameTeam = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) == pVoteCaller->m_Team && pVoteCaller->m_Team != 0;
						bool MySelf = CallerId == LocalId;

						if(!Friend && !SameTeam && !MySelf)
						{
							GameClient()->m_Voting.Vote(-1);
							if(str_comp(g_Config.m_TcAutoVoteWhenFarMessage, "") != 0)
								SendNonDuplicateMessage(0, g_Config.m_TcAutoVoteWhenFarMessage);
						}
					}
				}
			}
		}
	}

	auto &vServerCommands = GameClient()->m_Chat.m_vServerCommands;
	auto AddSpecId = [&](bool Enable) {
		static const CChat::CCommand SpecId("specid", "v[id]", "Spectate a player");
		vServerCommands.erase(std::remove_if(vServerCommands.begin(), vServerCommands.end(), [](const CChat::CCommand &Command) { return Command == SpecId; }), vServerCommands.end());
		if(Enable)
			vServerCommands.push_back(SpecId);
		GameClient()->m_Chat.m_ServerCommandsNeedSorting = true;
	};
	if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(!ServerCommandExists("specid"));
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(false);
		return;
	}
	if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		if(str_comp_nocase(pMsg->m_pName, "spec") == 0)
			AddSpecId(false);
		else if(str_comp_nocase(pMsg->m_pName, "specid") == 0)
			AddSpecId(ServerCommandExists("spec"));
		return;
	}
}

void CTClient::ConSpecId(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->SpecId(pResult->GetInteger(0));
}

bool CTClient::ChatDoSpecId(const char *pInput)
{
	const char *pNumber = str_startswith_nocase(pInput, "/specid ");
	if(!pNumber)
		return false;

	const int Length = str_length(pInput);
	CChat::CHistoryEntry *pEntry = GameClient()->m_Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
	pEntry->m_Team = 0;
	str_copy(pEntry->m_aText, pInput, Length + 1);

	int ClientId = 0;
	if(!str_toint(pNumber, &ClientId))
		return true;

	SpecId(ClientId);
	return true;
}

void CTClient::SpecId(int ClientId)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(Client()->State() == IClient::STATE_DEMOPLAYBACK || GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		GameClient()->m_Spectator.Spectate(ClientId);
		return;
	}

	if(ClientId < 0 || ClientId > (int)std::size(GameClient()->m_aClients))
		return;
	const auto &Player = GameClient()->m_aClients[ClientId];
	if(!Player.m_Active)
		return;
	char aBuf[256];
	str_copy(aBuf, "/spec \"");
	char *pDst = aBuf + strlen(aBuf);
	str_escape(&pDst, Player.m_aName, aBuf + sizeof(aBuf));
	str_append(aBuf, "\"");
	GameClient()->m_Chat.SendChat(0, aBuf);
}

void CTClient::ConEmoteCycle(IConsole::IResult *pResult, void *pUserData)
{
	CTClient &This = *(CTClient *)pUserData;
	This.m_EmoteCycle += 1;
	if(This.m_EmoteCycle > 15)
		This.m_EmoteCycle = 0;
	This.GameClient()->m_Emoticon.Emote(This.m_EmoteCycle);
}

void CTClient::AirRescue()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return;
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && (GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE) == 0)
	{
		GameClient()->Echo("You are not in practice");
		return;
	}

	auto IsIndexAirLike = [&](int Index) {
		const auto Tile = Collision()->GetTileIndex(Index);
		return Tile == TILE_AIR || Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE;
	};
	auto IsPosAirLike = [&](vec2 Pos) {
		const int Index = Collision()->GetPureMapIndex(Pos);
		return IsIndexAirLike(Index);
	};
	auto IsRadiusAirLike = [&](vec2 Pos, int Radius) {
		for(int y = -Radius; y <= Radius; ++y)
			for(int x = -Radius; x <= Radius; ++x)
				if(!IsPosAirLike(Pos + vec2(x, y) * 32.0f))
					return false;
		return true;
	};

	auto &AirRescuePositions = m_aAirRescuePositions[g_Config.m_ClDummy];
	while(!AirRescuePositions.empty())
	{
		// Get latest pos from positions
		const vec2 NewPos = AirRescuePositions.front();
		AirRescuePositions.pop_front();
		// Check for safety
		if(!IsRadiusAirLike(NewPos, 2))
			continue;
		// Do it
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "/tpxy %f %f", NewPos.x / 32.0f, NewPos.y / 32.0f);
		GameClient()->m_Chat.SendChat(0, aBuf);
		return;
	}

	GameClient()->Echo("No safe position found");
}

void CTClient::ConAirRescue(IConsole::IResult *pResult, void *pUserData)
{
	((CTClient *)pUserData)->AirRescue();
}

void CTClient::ConCalc(IConsole::IResult *pResult, void *pUserData)
{
	int Error = 0;
	double Out = te_interp(pResult->GetString(0), &Error);
	if(Out == NAN || Error != 0)
		log_info("tclient", "Calc error: %d", Error);
	else
		log_info("tclient", "Calc result: %lf", Out);
}


void CTClient::OnConsoleInit()
{
	Console()->Register("calc", "r[expression]", CFGFLAG_CLIENT, ConCalc, this, "Evaluate an expression");
	Console()->Register("airrescue", "", CFGFLAG_CLIENT, ConAirRescue, this, "Rescue to a nearby air tile");

	Console()->Register("tc_random_player", "s[type]", CFGFLAG_CLIENT, ConRandomTee, this, "Randomize player color (0 = all, 1 = body, 2 = feet, 3 = skin, 4 = flag) example: 0011 = randomize skin and flag [number is position]");
	Console()->Chain("tc_random_player", ConchainRandomColor, this);

	Console()->Register("spec_id", "v[id]", CFGFLAG_CLIENT, ConSpecId, this, "Spectate a player by Id");

	Console()->Register("emote_cycle", "", CFGFLAG_CLIENT, ConEmoteCycle, this, "Cycle through emotes");


	Console()->Chain(
		"tc_allow_any_res", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			pfnCallback(pResult, pCallbackUserData);
			((CTClient *)pUserData)->SetForcedAspect();
		},
		this);

	Console()->Chain(
		"tc_regex_chat_ignore", [](IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData) {
			if(pResult->NumArguments() == 1)
			{
				auto Re = Regex(pResult->GetString(0));
				if(!Re.error().empty())
				{
					log_error("tclient", "Invalid regex: %s", Re.error().c_str());
					return;
				}
				((CTClient *)pUserData)->m_RegexChatIgnore = std::move(Re);
			}
			pfnCallback(pResult, pCallbackUserData);
		},
		this);
}

void CTClient::RandomBodyColor()
{
	g_Config.m_ClPlayerColorBody = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomFeetColor()
{
	g_Config.m_ClPlayerColorFeet = ColorHSLA((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f, 1.0f).Pack(false);
}

void CTClient::RandomSkin(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	const auto &Skins = pThis->GameClient()->m_Skins.SkinList().Skins();
	str_copy(g_Config.m_ClPlayerSkin, Skins[std::rand() % (int)Skins.size()].SkinContainer()->Name());
}

void CTClient::RandomFlag(void *pUserData)
{
	CTClient *pThis = static_cast<CTClient *>(pUserData);
	// get the flag count
	int FlagCount = pThis->GameClient()->m_CountryFlags.Num();

	// get a random flag number
	int FlagNumber = std::rand() % FlagCount;

	// get the flag name
	const CCountryFlags::CCountryFlag &Flag = pThis->GameClient()->m_CountryFlags.GetByIndex(FlagNumber);

	// set the flag code as number
	g_Config.m_PlayerCountry = Flag.m_CountryCode;
}

void CTClient::DoFinishCheck()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_TcChangeNameNearFinish <= 0)
		return;
	m_FinishTextTimeout -= Client()->RenderFrameTime();
	if(m_FinishTextTimeout > 0.0f)
		return;
	m_FinishTextTimeout = 1.0f;
	// Check for finish tile
	const auto &NearTile = [this](vec2 Pos, int RadiusInTiles, int Tile) -> bool {
		const CCollision *pCollision = GameClient()->Collision();
		for(int i = 0; i <= RadiusInTiles * 2; ++i)
		{
			const float h = std::ceil(std::pow(std::sin((float)i * pi / 2.0f / (float)RadiusInTiles), 0.5f) * pi / 2.0f * (float)RadiusInTiles);
			const vec2 Pos1 = vec2(Pos.x + (float)(i - RadiusInTiles) * 32.0f, Pos.y - h);
			const vec2 Pos2 = vec2(Pos.x + (float)(i - RadiusInTiles) * 32.0f, Pos.y + h);
			std::vector<int> vIndices = pCollision->GetMapIndices(Pos1, Pos2);
			if(vIndices.empty())
				vIndices.push_back(pCollision->GetPureMapIndex(Pos1));
			for(int &Index : vIndices)
			{
				if(pCollision->GetTileIndex(Index) == Tile)
					return true;
				if(pCollision->GetFrontTileIndex(Index) == Tile)
					return true;
			}
		}
		return false;
	};
	const auto &SendUrgentRename = [this](int Conn, const char *pNewName) {
		CNetMsg_Cl_ChangeInfo Msg;
		Msg.m_pName = pNewName;
		Msg.m_pClan = Conn == 0 ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan;
		Msg.m_Country = Conn == 0 ? g_Config.m_PlayerCountry : g_Config.m_ClDummyCountry;
		Msg.m_pSkin = Conn == 0 ? g_Config.m_ClPlayerSkin : g_Config.m_ClDummySkin;
		Msg.m_UseCustomColor = Conn == 0 ? g_Config.m_ClPlayerUseCustomColor : g_Config.m_ClDummyUseCustomColor;
		Msg.m_ColorBody = Conn == 0 ? g_Config.m_ClPlayerColorBody : g_Config.m_ClDummyColorBody;
		Msg.m_ColorFeet = Conn == 0 ? g_Config.m_ClPlayerColorFeet : g_Config.m_ClDummyColorFeet;
		CMsgPacker Packer(&Msg);
		Msg.Pack(&Packer);
		Client()->SendMsg(Conn, &Packer, MSGFLAG_VITAL);
		GameClient()->m_aCheckInfo[Conn] = Client()->GameTickSpeed(); // 1 second
	};
	int Dummy = g_Config.m_ClDummy;
	const auto &Player = GameClient()->m_aClients[GameClient()->m_aLocalIds[Dummy]];
	if(!Player.m_Active)
		return;
	const char *NewName = g_Config.m_TcFinishName;
	if(str_comp(Player.m_aName, NewName) == 0)
		return;
	if(!NearTile(Player.m_RenderPos, 10, TILE_FINISH))
		return;
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), TCLocalize("Changing name to %s near finish"), NewName);
	GameClient()->Echo(aBuf);
	SendUrgentRename(Dummy, NewName);
}

bool CTClient::ServerCommandExists(const char *pCommand)
{
	for(const auto &Command : GameClient()->m_Chat.m_vServerCommands)
		if(str_comp_nocase(pCommand, Command.m_aName) == 0)
			return true;
	return false;
}

void CTClient::OnRender()
{
	if(m_pTClientInfoTask)
	{
		if(m_pTClientInfoTask->State() == EHttpState::DONE)
		{
			FinishTClientInfo();
			ResetTClientInfoTask();
		}
		else if(m_pTClientInfoTask->State() == EHttpState::ERROR || m_pTClientInfoTask->State() == EHttpState::ABORTED)
		{
			m_UpdateState = UPDATE_STATE_FAIL;
			str_copy(m_aUpdateStatus, "Fallo al consultar GitHub", sizeof(m_aUpdateStatus));
			ResetTClientInfoTask();
		}
	}

	if(m_pDownloadTask)
	{
		m_UpdatePercent = m_pDownloadTask->Progress();
		if(m_pDownloadTask->State() == EHttpState::DONE)
		{
			if(ReplaceExecutable())
			{
				m_UpdateState = UPDATE_STATE_NEED_RESTART;
				str_copy(m_aUpdateStatus, "Actualizacion completada. Reinicie el juego.", sizeof(m_aUpdateStatus));
			}
			else
			{
				m_UpdateState = UPDATE_STATE_FAIL;
				str_copy(m_aUpdateStatus, "Fallo al reemplazar ejecutable.", sizeof(m_aUpdateStatus));
			}
			m_pDownloadTask = nullptr;
		}
		else if(m_pDownloadTask->State() == EHttpState::ERROR || m_pDownloadTask->State() == EHttpState::ABORTED)
		{
			m_UpdateState = UPDATE_STATE_FAIL;
			str_copy(m_aUpdateStatus, "Fallo al descargar actualizacion.", sizeof(m_aUpdateStatus));
			m_pDownloadTask = nullptr;
		}
	}

	DoFinishCheck();
}

bool CTClient::NeedUpdate()
{
	return m_UpdateState == UPDATE_STATE_AVAILABLE ||
	       m_UpdateState == UPDATE_STATE_DOWNLOADING ||
	       m_UpdateState == UPDATE_STATE_NEED_RESTART;
}

void CTClient::ResetTClientInfoTask()
{
	if(m_pTClientInfoTask)
	{
		m_pTClientInfoTask->Abort();
		m_pTClientInfoTask = nullptr;
	}
}

void CTClient::FetchTClientInfo(bool Force)
{
	if(!Force && m_UpdateState != UPDATE_STATE_IDLE && m_UpdateState != UPDATE_STATE_FAIL)
		return;

	if(m_pTClientInfoTask && !m_pTClientInfoTask->Done())
	{
		if(Force)
			m_pTClientInfoTask->Abort();
		else
			return;
	}

	char aUrl[256];
	str_copy(aUrl, ACLIENT_LATEST_RELEASE_URL, sizeof(aUrl));
	m_pTClientInfoTask = HttpGet(aUrl);
	m_pTClientInfoTask->Timeout(CTimeout{10000, 0, 500, 10});
	m_pTClientInfoTask->IpResolve(IPRESOLVE::V4);
	m_pTClientInfoTask->Header("User-Agent: AClient");
	Http()->Run(m_pTClientInfoTask);

	m_UpdateState = UPDATE_STATE_CHECKING;
	str_copy(m_aUpdateStatus, "Buscando actualizacion...", sizeof(m_aUpdateStatus));
}

typedef std::tuple<int, int, int> TVersion;
static const TVersion gs_InvalidTCVersion = std::make_tuple(-1, -1, -1);

static TVersion ParseSemVer(const char *pStr)
{
	if(!pStr || !*pStr)
		return gs_InvalidTCVersion;

	if(*pStr == 'v' || *pStr == 'V')
		pStr++;

	int aVersion[3] = {0, 0, 0};
	char aBuf[64];
	str_copy(aBuf, pStr, sizeof(aBuf));

	char *p = aBuf;
	char *pToken = strtok(p, ".");
	for(int i = 0; i < 3 && pToken; ++i)
	{
		if(!str_isallnum(pToken))
			return gs_InvalidTCVersion;
		aVersion[i] = str_toint(pToken);
		pToken = strtok(nullptr, ".");
	}
	return std::make_tuple(aVersion[0], aVersion[1], aVersion[2]);
}

void CTClient::FinishTClientInfo()
{
	json_value *pJson = m_pTClientInfoTask ? m_pTClientInfoTask->ResultJson() : nullptr;
	if(!pJson)
	{
		m_UpdateState = UPDATE_STATE_FAIL;
		str_copy(m_aUpdateStatus, "Error de conexion con GitHub", sizeof(m_aUpdateStatus));
		return;
	}

	const json_value &Json = *pJson;
	if(Json.type != json_array && Json.type != json_object)
	{
		json_value_free(pJson);
		m_UpdateState = UPDATE_STATE_FAIL;
		str_copy(m_aUpdateStatus, "Respuesta invalida de GitHub", sizeof(m_aUpdateStatus));
		return;
	}

	std::vector<CAClientRelease> vNewReleases;

	auto ParseReleaseObject = [&](const json_value &Rel, bool IsLatest) {
		const json_value &Draft = Rel["draft"];
		bool IsDraft = (Draft.type == json_boolean && Draft.u.boolean);
		if(IsDraft)
			return;

		const json_value &Prerelease = Rel["prerelease"];
		bool IsPrerelease = (Prerelease.type == json_boolean && Prerelease.u.boolean);

		CAClientRelease Entry;
		Entry.m_IsPrerelease = IsPrerelease;

		const json_value &TagName = Rel["tag_name"];
		const json_value &Name = Rel["name"];
		const json_value &Body = Rel["body"];
		const json_value &PublishedAt = Rel["published_at"];

		if(TagName.type == json_string)
			str_copy(Entry.m_aTag, (const char *)TagName, sizeof(Entry.m_aTag));
		if(Name.type == json_string)
			str_copy(Entry.m_aName, (const char *)Name, sizeof(Entry.m_aName));
		else if(Entry.m_aTag[0])
			str_copy(Entry.m_aName, Entry.m_aTag, sizeof(Entry.m_aName));

		if(PublishedAt.type == json_string)
		{
			char aDate[32] = "";
			str_copy(aDate, (const char *)PublishedAt, sizeof(aDate));
			for(int d = 0; aDate[d] != '\0'; ++d)
			{
				if(aDate[d] == 'T')
				{
					aDate[d] = '\0';
					break;
				}
			}
			str_copy(Entry.m_aPublishedAt, aDate, sizeof(Entry.m_aPublishedAt));
		}

		if(Body.type == json_string)
			Entry.m_Body = (const char *)Body;

		if(Entry.m_aTag[0] != '\0')
			vNewReleases.push_back(std::move(Entry));

		if(IsLatest && TagName.type == json_string)
		{
			char aRemoteVersion[64];
			str_copy(aRemoteVersion, (const char *)TagName, sizeof(aRemoteVersion));

			TVersion RemoteVer = ParseSemVer(aRemoteVersion);
			TVersion LocalVer = ParseSemVer(ACLIENT_VERSION);

			if(RemoteVer != gs_InvalidTCVersion && RemoteVer > LocalVer)
			{
				const json_value &Assets = Rel["assets"];
				bool FoundAsset = false;
				if(Assets.type == json_array)
				{
					for(unsigned int i = 0; i < Assets.u.array.length; ++i)
					{
						const json_value &Asset = *Assets.u.array.values[i];
						const json_value &AssetName = Asset["name"];
						const json_value &DownloadUrl = Asset["browser_download_url"];

						if(AssetName.type == json_string && DownloadUrl.type == json_string)
						{
							if(str_comp_nocase((const char *)AssetName, "DDNet.exe") == 0)
							{
								str_copy(m_aDownloadUrl, (const char *)DownloadUrl, sizeof(m_aDownloadUrl));
								FoundAsset = true;
								break;
							}
						}
					}
				}

				if(FoundAsset)
				{
					str_copy(m_aLatestVersion, aRemoteVersion, sizeof(m_aLatestVersion));
					str_copy(m_aVersionStr, aRemoteVersion, sizeof(m_aVersionStr));
					m_UpdateState = UPDATE_STATE_AVAILABLE;
					str_format(m_aUpdateStatus, sizeof(m_aUpdateStatus), "Nueva version disponible: %s", aRemoteVersion);
				}
				else
				{
					m_UpdateState = UPDATE_STATE_FAIL;
					str_copy(m_aUpdateStatus, "Asset DDNet.exe no encontrado en release", sizeof(m_aUpdateStatus));
				}
			}
			else
			{
				m_aVersionStr[0] = '0';
				m_aVersionStr[1] = '\0';
				str_copy(m_aLatestVersion, ACLIENT_VERSION, sizeof(m_aLatestVersion));
				m_UpdateState = UPDATE_STATE_IDLE;
				str_copy(m_aUpdateStatus, "Cliente al dia", sizeof(m_aUpdateStatus));
			}
			m_FetchedTClientInfo = true;
		}
	};

	bool FoundLatestOfficial = false;

	if(Json.type == json_array)
	{
		for(unsigned int i = 0; i < Json.u.array.length && i < 15; ++i)
		{
			const json_value &Rel = *Json.u.array.values[i];
			if(Rel.type == json_object)
			{
				const json_value &Draft = Rel["draft"];
				bool IsDraft = (Draft.type == json_boolean && Draft.u.boolean);
				if(IsDraft)
					continue;

				const json_value &Prerelease = Rel["prerelease"];
				bool IsPrerelease = (Prerelease.type == json_boolean && Prerelease.u.boolean);

				bool IsLatestCandidate = false;
				if(!FoundLatestOfficial && !IsPrerelease)
				{
					IsLatestCandidate = true;
					FoundLatestOfficial = true;
				}

				ParseReleaseObject(Rel, IsLatestCandidate);
			}
		}
	}
	else if(Json.type == json_object)
	{
		const json_value &Draft = Json["draft"];
		bool IsDraft = (Draft.type == json_boolean && Draft.u.boolean);
		const json_value &Prerelease = Json["prerelease"];
		bool IsPrerelease = (Prerelease.type == json_boolean && Prerelease.u.boolean);

		bool IsLatestCandidate = !IsDraft && !IsPrerelease;
		if(IsLatestCandidate)
			FoundLatestOfficial = true;

		ParseReleaseObject(Json, IsLatestCandidate);
	}

	if(!FoundLatestOfficial)
	{
		m_aVersionStr[0] = '0';
		m_aVersionStr[1] = '\0';
		str_copy(m_aLatestVersion, ACLIENT_VERSION, sizeof(m_aLatestVersion));
		m_UpdateState = UPDATE_STATE_IDLE;
		str_copy(m_aUpdateStatus, "Cliente al dia", sizeof(m_aUpdateStatus));
		m_FetchedTClientInfo = true;
	}

	if(!vNewReleases.empty())
	{
		m_vReleases = std::move(vNewReleases);
		if(m_SelectedRelease >= (int)m_vReleases.size())
			m_SelectedRelease = 0;
	}

	json_value_free(pJson);
}

void CTClient::InitiateUpdate()
{
	if(m_aDownloadUrl[0] == '\0')
		return;

	if(m_pDownloadTask && !m_pDownloadTask->Done())
		return;

	m_pDownloadTask = HttpGet(m_aDownloadUrl);
	m_pDownloadTask->Timeout(CTimeout{60000, 0, 500, 20});
	m_pDownloadTask->IpResolve(IPRESOLVE::V4);
	m_pDownloadTask->Header("User-Agent: AClient");
	m_pDownloadTask->WriteToFile(Storage(), "update/DDNet_new.exe", -2);
	Http()->Run(m_pDownloadTask);

	m_UpdateState = UPDATE_STATE_DOWNLOADING;
	m_UpdatePercent = 0;
	str_copy(m_aUpdateStatus, "Descargando actualizacion...", sizeof(m_aUpdateStatus));
}

bool CTClient::ReplaceExecutable()
{
	IOHANDLE CheckFile = Storage()->OpenFile("update/DDNet_new.exe", IOFLAG_READ, -2);
	if(!CheckFile)
	{
		log_error("updater", "Downloaded file not found at update/DDNet_new.exe");
		return false;
	}
	const int64_t FileSize = io_length(CheckFile);
	io_close(CheckFile);

	if(FileSize < 1024 * 1024)
	{
		log_error("updater", "Downloaded file is too small (%ld bytes)", FileSize);
		return false;
	}

	bool Success = true;
	// 1. Remove previous backup if exists
	Storage()->RemoveBinaryFile("DDNet.old");

	// 2. Rename currently running DDNet.exe to DDNet.old
	Success &= Storage()->RenameBinaryFile("DDNet.exe", "DDNet.old");
	if(!Success)
	{
		log_error("updater", "Failed to rename running executable DDNet.exe to DDNet.old");
		return false;
	}

	// 3. Rename update/DDNet_new.exe to DDNet.exe
	Success &= Storage()->RenameBinaryFile("update/DDNet_new.exe", "DDNet.exe");
	if(!Success)
	{
		log_error("updater", "Failed to rename update/DDNet_new.exe to DDNet.exe, restoring old binary");
		Storage()->RenameBinaryFile("DDNet.old", "DDNet.exe");
		return false;
	}

	log_info("updater", "Executable successfully updated to version %s", m_aLatestVersion);
	return true;
}

void CTClient::SetForcedAspect()
{
	// TODO: Fix flashing on windows
	int State = Client()->State();
	bool Force = true;
	if(g_Config.m_AcCustomAspectRatio)
		Force = false;
	else if(g_Config.m_TcAllowAnyRes == 0)
		;
	else if(State == CClient::EClientState::STATE_DEMOPLAYBACK)
		Force = false;
	else if(State == CClient::EClientState::STATE_ONLINE && GameClient()->m_GameInfo.m_AllowZoom && !GameClient()->m_Menus.IsActive())
		Force = false;
	Graphics()->SetForcedAspect(Force);
}

void CTClient::OnStateChange(int OldState, int NewState)
{
	SetForcedAspect();
	for(auto &AirRescuePositions : m_aAirRescuePositions)
		AirRescuePositions = {};
}

void CTClient::OnNewSnapshot()
{
	SetForcedAspect();
	// Update volleyball
	bool IsVolleyBall = false;
	if(g_Config.m_TcVolleyBallBetterBall > 0 && g_Config.m_TcVolleyBallBetterBallSkin[0] != '\0')
	{
		if(g_Config.m_TcVolleyBallBetterBall > 1)
			IsVolleyBall = true;
		else
			IsVolleyBall = str_startswith_nocase(GameClient()->Map()->BaseName(), "volleyball");
	};
	for(auto &Client : GameClient()->m_aClients)
	{
		Client.m_IsVolleyBall = IsVolleyBall && Client.m_DeepFrozen;
	}
	// Update air rescue
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
		{
			const int ClientId = GameClient()->m_aLocalIds[Dummy];
			if(ClientId == -1)
				continue;
			const auto &Char = GameClient()->m_Snap.m_aCharacters[ClientId];
			if(!Char.m_Active)
				continue;
			if(Client()->GameTick(Dummy) % 10 != 0) // Works for both 25tps and 50tps
				continue;
			const auto &Client = GameClient()->m_aClients[ClientId];
			if(Client.m_FreezeEnd == -1) // You aren't safe when frozen
				continue;
			const vec2 NewPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
			// If new pos is under 2 tiles from old pos, don't record a new position
			if(!m_aAirRescuePositions[Dummy].empty())
			{
				const vec2 OldPos = m_aAirRescuePositions[Dummy].front();
				if(distance(NewPos, OldPos) < 64.0f)
					continue;
			}
			if(m_aAirRescuePositions[Dummy].size() >= 256)
				m_aAirRescuePositions[Dummy].pop_back();
			m_aAirRescuePositions[Dummy].push_front(NewPos);
		}
	}
}

constexpr const char STRIP_CHARS[] = {'-', '=', '+', '_', ' '};
static bool IsStripChar(char c)
{
	return std::any_of(std::begin(STRIP_CHARS), std::end(STRIP_CHARS), [c](char s) {
		return s == c;
	});
}

static void StripStr(const char *pIn, char *pOut, const char *pEnd)
{
	if(!pIn)
	{
		*pOut = '\0';
		return;
	}

	while(*pIn && IsStripChar(*pIn))
		pIn++;

	// Special behaviour for empty checkbox
	if((unsigned char)*pIn == 0xE2 && (unsigned char)(*(pIn + 1)) == 0x98 && (unsigned char)(*(pIn + 2)) == 0x90)
	{
		pIn += 3;
		while(*pIn && IsStripChar(*pIn))
			pIn++;
	}

	char *pLastValid = nullptr;
	while(*pIn && pOut < pEnd - 1)
	{
		*pOut = *pIn;
		if(!IsStripChar(*pIn))
			pLastValid = pOut;
		pIn++;
		pOut++;
	}

	if(pLastValid)
		*(pLastValid + 1) = '\0';
	else
		*pOut = '\0';
}

void CTClient::RenderMiniVoteHud()
{
	CUIRect View = {0.0f, 60.0f, 70.0f, 35.0f};
	View.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_R, 3.0f);
	View.Margin(3.0f, &View);

	SLabelProperties Props;
	Props.m_EllipsisAtEnd = true;
	Props.m_MaxWidth = View.w;

	CUIRect Row, LeftColumn, RightColumn, ProgressSpinner;
	char aBuf[256];

	// Vote description
	View.HSplitTop(6.0f, &Row, &View);
	StripStr(GameClient()->m_Voting.VoteDescription(), aBuf, aBuf + sizeof(aBuf));
	Ui()->DoLabel(&Row, aBuf, 6.0f, TEXTALIGN_ML, Props);

	// Vote reason
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(4.0f, &Row, &View);
	Ui()->DoLabel(&Row, GameClient()->m_Voting.VoteReason(), 4.0f, TEXTALIGN_ML, Props);

	// Time left
	str_format(aBuf, sizeof(aBuf), Localize("%ds left"), GameClient()->m_Voting.SecondsLeft());
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	Row.VSplitLeft(2.0f, nullptr, &Row);
	Row.VSplitLeft(3.0f, &ProgressSpinner, &Row);
	Row.VSplitLeft(2.0f, nullptr, &Row);

	SProgressSpinnerProperties ProgressProps;
	ProgressProps.m_Progress = std::clamp((time() - GameClient()->m_Voting.m_Opentime) / (float)(GameClient()->m_Voting.m_Closetime - GameClient()->m_Voting.m_Opentime), 0.0f, 1.0f);
	Ui()->RenderProgressSpinner(ProgressSpinner.Center(), ProgressSpinner.h / 2.0f, ProgressProps);

	Ui()->DoLabel(&Row, aBuf, 3.0f, TEXTALIGN_ML);

	// Bars
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(3.0f, &Row, &View);
	GameClient()->m_Voting.RenderBars(Row);

	// F3 / F4
	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(0.5f, &Row, &View);
	Row.VSplitMid(&LeftColumn, &RightColumn, 4.0f);

	char aKey[64];
	GameClient()->m_Binds.GetKey("vote yes", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == 1 ? ColorRGBA(0.2f, 0.9f, 0.2f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&LeftColumn, aKey[0] == '\0' ? "yes" : aKey, 0.5f, TEXTALIGN_ML);

	GameClient()->m_Binds.GetKey("vote no", aKey, sizeof(aKey));
	TextRender()->TextColor(GameClient()->m_Voting.TakenChoice() == -1 ? ColorRGBA(0.95f, 0.25f, 0.25f, 0.85f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&RightColumn, aKey[0] == '\0' ? "no" : aKey, 0.5f, TEXTALIGN_MR);

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CTClient::RenderCenterLines()
{
	if(g_Config.m_TcShowCenter <= 0)
		return;

	if(GameClient()->m_Scoreboard.IsActive())
		return;

	Graphics()->TextureClear();

	const CScreenRect ScreenRect = Graphics()->GetScreen();
	const auto ScreenMid = (ScreenRect.m_BottomRight + ScreenRect.m_TopLeft) / 2.0f;

	if(g_Config.m_TcShowCenterWidth == 0)
	{
		Graphics()->LinesBegin();
		IGraphics::CLineItem aLines[2] = {
			{ScreenMid.x, ScreenRect.m_TopLeft.y, ScreenMid.x, ScreenRect.m_BottomRight.y},
			{ScreenRect.m_TopLeft.x, ScreenMid.y, ScreenRect.m_BottomRight.x, ScreenMid.y}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->LinesDraw(aLines, std::size(aLines));
		Graphics()->LinesEnd();
	}
	else
	{
		const float W = g_Config.m_TcShowCenterWidth;
		const float X0 = ScreenRect.m_TopLeft.x;
		const float Y0 = ScreenRect.m_TopLeft.y;
		const float X1 = ScreenRect.m_BottomRight.x;
		const float Y1 = ScreenRect.m_BottomRight.y;
		Graphics()->QuadsBegin();
		IGraphics::CQuadItem aQuads[3] = {
			{ScreenMid.x, mix(Y0, Y1, 0.25f) - W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{ScreenMid.x, mix(Y0, Y1, 0.75f) + W / 4.0f, W, (Y1 - Y0 - W) / 2.0f},
			{ScreenMid.x, ScreenMid.y, X1 - X0, W}};
		Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcShowCenterColor, true)));
		Graphics()->QuadsDraw(aQuads, std::size(aQuads));
		Graphics()->QuadsEnd();
	}
}

void CTClient::RenderCtfFlag(vec2 Pos, float Alpha)
{
	// from CItems::RenderFlag
	float Size = 42.0f;
	int QuadOffset;
	if(g_Config.m_TcFakeCtfFlags == 1)
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagRed);
		QuadOffset = GameClient()->m_Items.m_RedFlagOffset;
	}
	else
	{
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteFlagBlue);
		QuadOffset = GameClient()->m_Items.m_BlueFlagOffset;
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->RenderQuadContainerAsSprite(GameClient()->m_Items.m_ItemsQuadContainerIndex, QuadOffset, Pos.x, Pos.y - Size * 0.75f);
}
