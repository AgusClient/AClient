/* Copyright © 2026 BestProject Team */
#include "fast_practice.h"

#include <base/math.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/effects.h>
#include <game/client/components/items.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/aclient_inputs.h>
#include <game/client/gameclient.h>
#include <game/client/laser_data.h>
#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/dragger.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/client/prediction/entities/plasma.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/gamecore.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <limits>
#include <utility>

namespace
{
	void NeutralizeInput(CNetObj_PlayerInput &Input)
	{
		if((Input.m_Fire & 1) != 0)
			Input.m_Fire++;
		Input.m_Fire &= INPUT_STATE_MASK;

		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
		Input.m_WantedWeapon = 0;

		if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
		{
			Input.m_TargetX = 1;
			Input.m_TargetY = 0;
		}
	}

	int ReleasedFireState(int FireState)
	{
		FireState &= INPUT_STATE_MASK;
		if((FireState & 1) != 0)
			FireState = (FireState + 1) & INPUT_STATE_MASK;
		return FireState;
	}

	bool IsFrozenState(const CCharacter *pChar)
	{
		if(!pChar)
			return false;
		const CCharacterCore &Core = *pChar->Core();
		return pChar->m_FreezeTime > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_FreezeEnd != 0;
	}

	int ClampWeaponId(int WeaponId)
	{
		return std::clamp(WeaponId, -1, NUM_WEAPONS - 1);
	}

	std::string LowercaseCopy(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return Text;
	}

	template<typename TTracked>
	bool SameProjectile(const TTracked &A, const TTracked &B)
	{
		return A.m_Owner == B.m_Owner &&
		       A.m_StartTick == B.m_StartTick &&
		       A.m_Type == B.m_Type &&
		       A.m_TuneZone == B.m_TuneZone &&
		       distance(A.m_StartPos, B.m_StartPos) < 0.01f &&
		       distance(A.m_StartVel, B.m_StartVel) < 0.01f;
	}

	bool IsTrackedExplosive(const CProjectileData &Data, int LocalClientId, int DummyClientId)
	{
		const bool PracticeOwned = Data.m_Owner == LocalClientId || (DummyClientId >= 0 && Data.m_Owner == DummyClientId);
		return PracticeOwned && (Data.m_Explosive || Data.m_Type == WEAPON_GRENADE);
	}

	template<typename TTracked>
	vec2 CalcTrackedProjectilePos(const TTracked &Proj, int Tick, int TickSpeed, const CTuningParams *pTuning)
	{
		float Curvature = 0.0f;
		float Speed = 0.0f;
		if(Proj.m_Type == WEAPON_GRENADE)
		{
			Curvature = pTuning->m_GrenadeCurvature;
			Speed = pTuning->m_GrenadeSpeed;
		}
		else if(Proj.m_Type == WEAPON_SHOTGUN)
		{
			Curvature = pTuning->m_ShotgunCurvature;
			Speed = pTuning->m_ShotgunSpeed;
		}
		else
		{
			Curvature = pTuning->m_GunCurvature;
			Speed = pTuning->m_GunSpeed;
		}

		const float Ct = std::max(0.0f, (Tick - Proj.m_StartTick) / (float)TickSpeed);
		return CalcPos(Proj.m_StartPos, Proj.m_StartVel, Curvature, Speed, Ct);
	}
} // namespace

void CFastPractice::ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->Toggle();
}

void CFastPractice::ResetPracticeState()
{
	AcInputs::SetPracticeActive(false);
	RestoreSpecPracticeView();
	m_Enabled = false;
	m_Ready = false;
	m_NeedsRebuild = false;
	m_RequireDummy = false;
	m_EnableLocalClientId = -1;
	m_EnableDummyClientId = -1;
	m_HasDummyAnchor = false;
	m_SuppressFireOnNextPredictTick = false;
	m_InputSuppressTicks = 0;
	m_LastClDummy = g_Config.m_ClDummy;
	m_LastResolvedLocalClientId = -1;
	m_LastResolvedDummyClientId = -1;
	m_LastRoundStartTick = -1;
	m_OverridingSpecSnap = false;
	m_SpecSyntheticPrev = {};
	m_SpecSyntheticCur = {};
	m_aHasServerLockedTargets.fill(false);
	m_aServerLockedTargets.fill(ivec2(1, 0));
	m_aServerLockedFire.fill(0);
	m_aServerLockedNextWeapon.fill(0);
	m_aServerLockedPrevWeapon.fill(0);
	m_aFrozenTargetValid.fill(false);
	m_aSpawnPosValid.fill(false);
	m_aSafePosValid.fill(false);
	m_aFastRenderValid.fill(false);
	m_aPublishedValid.fill(false);
	for(auto &Core : m_aPublishedPredicted)
		Core.Reset();
	for(auto &Core : m_aPublishedPrevPredicted)
		Core.Reset();
	for(auto &Core : m_aPublishedRegularPredicted)
		Core.Reset();
	m_aLastEventTick.fill(-1);
	m_aFreezeSkinTicks.fill(0);
	m_LastFreezeSkinDebounceTick = -1;
	m_MainGhost = {};
	m_DummyGhost = {};
	m_MainAnchor = {};
	m_DummyAnchor = {};
	m_PracticeWorld.Clear();
	m_PracticeWorld.Init(nullptr, nullptr, nullptr);
	m_PracticePrevWorld.Clear();
	m_PracticePrevWorld.Init(nullptr, nullptr, nullptr);
	m_FastOverrunWorld.Clear();
	m_FastOverrunWorld.Init(nullptr, nullptr, nullptr);
	m_vTrackedExplosiveBefore.clear();
	m_vTrackedExplosiveAfter.clear();
	ClearMapEntities();
	ResetStoredInputs();
	ResetAttackTickHistory();
	ResetCommandState();
	ResetPracticeRaceStates();
	ResetPracticeSaves();
	if(GameClient())
		GameClient()->m_PredictedDummyId = -1;
}

void CFastPractice::ResetCommandState()
{
	for(auto &State : m_aPracticeCommandState)
		State = {};
}

void CFastPractice::ResetPracticeRaceStates()
{
	for(auto &State : m_aPracticeRaceState)
		State = {};
}

void CFastPractice::ResetPracticeSaves()
{
	m_DefaultSave = {};
	m_vNamedSaves.clear();
}

void CFastPractice::OnReset()
{
	ResetPracticeState();
}

void CFastPractice::OnMapLoad()
{
	ResetPracticeState();
}

void CFastPractice::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE)
	{
		InvalidateBufferedInputState();
		ResetPracticeState();
	}
}

bool CFastPractice::CanEnable() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	if(!GameClient()->m_Snap.m_pLocalInfo || !GameClient()->m_Snap.m_pLocalCharacter)
		return false;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS)
		return false;

	if(GameClient()->m_Snap.m_pGameInfoObj &&
		(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & (GAMESTATEFLAG_PAUSED | GAMESTATEFLAG_GAMEOVER)))
	{
		return false;
	}

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;

	if(!GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active || GameClient()->m_aClients[LocalClientId].m_Paused)
		return false;

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS)
	{
		if(!GameClient()->m_Snap.m_aCharacters[ActiveClientId].m_Active || GameClient()->m_aClients[ActiveClientId].m_Paused)
			return false;
	}

	return true;
}

bool CFastPractice::IsRealSpectatorTeam(int LocalClientId) const
{
	const bool PausedOrSpec = LocalClientId >= 0 && LocalClientId < MAX_CLIENTS &&
				  (GameClient()->m_aClients[LocalClientId].m_Spec || GameClient()->m_aClients[LocalClientId].m_Paused);
	if(PausedOrSpec)
		return false;
	return GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS;
}

bool CFastPractice::IsOriginalSpecPaused(int LocalClientId) const
{
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;
	return GameClient()->m_aClients[LocalClientId].m_Spec || GameClient()->m_aClients[LocalClientId].m_Paused;
}

void CFastPractice::ApplySpecPracticeView(int ControlledId)
{
	if(!Active() || ControlledId < 0 || ControlledId >= MAX_CLIENTS)
		return;
	if(ControlledId != GameClient()->m_Snap.m_LocalClientId)
		return;
	CCharacter *pPracticeChar = m_PracticeWorld.GetCharacterById(ControlledId);
	if(!pPracticeChar)
		return;

	if(m_aFastRenderValid[ControlledId])
	{
		m_SpecSyntheticPrev = m_aFastRenderPrev[ControlledId];
		m_SpecSyntheticCur = m_aFastRenderCur[ControlledId];
	}
	else
	{
		if(m_aPublishedValid[ControlledId])
		{
			m_aPublishedPrevPredicted[ControlledId].Write(&m_SpecSyntheticPrev);
			m_aPublishedPredicted[ControlledId].Write(&m_SpecSyntheticCur);
		}
		else
		{
			FillRenderCharacter(pPracticeChar, m_SpecSyntheticCur);
			m_SpecSyntheticPrev = m_SpecSyntheticCur;
		}
		m_SpecSyntheticCur.m_Weapon = pPracticeChar->GetActiveWeapon();
		m_SpecSyntheticCur.m_AttackTick = pPracticeChar->GetAttackTick();
		m_SpecSyntheticCur.m_PlayerFlags = PLAYERFLAG_PLAYING;
		m_SpecSyntheticPrev.m_Weapon = m_SpecSyntheticCur.m_Weapon;
		m_SpecSyntheticPrev.m_AttackTick = m_SpecSyntheticCur.m_AttackTick;
		m_SpecSyntheticPrev.m_PlayerFlags = PLAYERFLAG_PLAYING;
	}

	m_SpecSyntheticCur.m_Tick = m_PracticeWorld.GameTick();
	m_SpecSyntheticPrev.m_Tick = std::max(0, m_SpecSyntheticCur.m_Tick - 1);
	m_SpecSyntheticCur.m_AmmoCount = 10;
	m_SpecSyntheticPrev.m_AmmoCount = 10;

	GameClient()->m_Snap.m_SpecInfo.m_Active = false;
	GameClient()->m_Snap.m_SpecInfo.m_UsePosition = false;
	GameClient()->m_Snap.m_pLocalCharacter = &m_SpecSyntheticCur;
	GameClient()->m_Snap.m_pLocalPrevCharacter = &m_SpecSyntheticPrev;
	GameClient()->m_LocalCharacterPos = mix(
		vec2((float)m_SpecSyntheticPrev.m_X, (float)m_SpecSyntheticPrev.m_Y),
		vec2((float)m_SpecSyntheticCur.m_X, (float)m_SpecSyntheticCur.m_Y),
		Client()->PredIntraGameTick(g_Config.m_ClDummy));
	m_OverridingSpecSnap = true;
}

void CFastPractice::RestoreSpecPracticeView()
{
	if(!m_OverridingSpecSnap)
		return;
	m_OverridingSpecSnap = false;
	if(!GameClient())
		return;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const bool StillSpec = LocalId >= 0 && LocalId < MAX_CLIENTS &&
			       (GameClient()->m_aClients[LocalId].m_Spec || GameClient()->m_aClients[LocalId].m_Paused ||
				       (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS));
	if(StillSpec)
	{
		GameClient()->m_Snap.m_SpecInfo.m_Active = true;
		GameClient()->m_Snap.m_pLocalCharacter = nullptr;
		GameClient()->m_Snap.m_pLocalPrevCharacter = nullptr;
	}
}

void CFastPractice::DropPracticeDummy()
{
	if(!m_RequireDummy)
		return;
	const int OldDummyId = m_EnableDummyClientId;
	m_RequireDummy = false;
	m_EnableDummyClientId = -1;
	m_HasDummyAnchor = false;
	m_DummyAnchor = {};
	m_DummyGhost = {};
	if(OldDummyId >= 0 && OldDummyId < MAX_CLIENTS)
	{
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(OldDummyId))
		{
			m_PracticeWorld.ReleaseHooked(OldDummyId);
			pDummy->Destroy();
		}
		if(CCharacter *pPrevDummy = m_PracticePrevWorld.GetCharacterById(OldDummyId))
		{
			m_PracticePrevWorld.ReleaseHooked(OldDummyId);
			pPrevDummy->Destroy();
		}
		if(CCharacter *pOverrunDummy = m_FastOverrunWorld.GetCharacterById(OldDummyId))
		{
			m_FastOverrunWorld.ReleaseHooked(OldDummyId);
			pOverrunDummy->Destroy();
		}
		m_aFastRenderValid[OldDummyId] = false;
		m_aPublishedValid[OldDummyId] = false;
		m_aFreezeSkinTicks[OldDummyId] = 0;
		m_aLastAttackTick[OldDummyId] = -1;
		m_aLastEventTick[OldDummyId] = -1;
		const bool ServerSolo = GameClient()->m_Snap.m_aCharacters[OldDummyId].m_HasExtendedData &&
					(GameClient()->m_Snap.m_aCharacters[OldDummyId].m_ExtendedData.m_Flags & CHARACTERFLAG_SOLO);
		GameClient()->m_aClients[OldDummyId].m_Solo = ServerSolo;
		GameClient()->m_Teams.SetSolo(OldDummyId, ServerSolo);
		std::fill(std::begin(GameClient()->m_aClients[OldDummyId].m_aPredTick), std::end(GameClient()->m_aClients[OldDummyId].m_aPredTick), -1);
	}
	GameClient()->m_PredictedDummyId = -1;
}

int CFastPractice::CurrentLocalPracticeId() const
{
	if(!m_Enabled)
		return -1;

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS &&
		(ActiveClientId == m_EnableLocalClientId || ActiveClientId == m_EnableDummyClientId))
	{
		return ActiveClientId;
	}

	const int InactiveClientId = GameClient()->m_aLocalIds[ActiveConn ^ 1];
	if(InactiveClientId >= 0 && InactiveClientId < MAX_CLIENTS &&
		(InactiveClientId == m_EnableLocalClientId || InactiveClientId == m_EnableDummyClientId))
	{
		return InactiveClientId;
	}

	return -1;
}

int CFastPractice::ConnectionForClientId(int ClientId) const
{
	if(ClientId >= 0 && ClientId == GameClient()->m_aLocalIds[IClient::CONN_DUMMY])
		return IClient::CONN_DUMMY;
	return IClient::CONN_MAIN;
}

bool CFastPractice::ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const
{
	LocalClientId = CurrentLocalPracticeId();
	DummyClientId = -1;

	if(LocalClientId < 0)
	{
		const bool SpecPaused = IsOriginalSpecPaused(m_EnableLocalClientId);
		if(SpecPaused)
			LocalClientId = m_EnableLocalClientId;
	}
	if(LocalClientId < 0)
		return false;
	if(LocalClientId != m_EnableLocalClientId && LocalClientId != m_EnableDummyClientId)
		return false;

	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		DummyClientId = LocalClientId == m_EnableLocalClientId ? m_EnableDummyClientId : m_EnableLocalClientId;

	return true;
}

int CFastPractice::ControlledPracticeId() const
{
	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return CurrentLocalPracticeId();
	return LocalClientId;
}

int CFastPractice::PartnerPracticeId() const
{
	if(!m_Enabled || !m_RequireDummy)
		return -1;
	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return -1;
	return DummyClientId;
}

bool CFastPractice::IsPracticeParticipant(int ClientId) const
{
	if(!m_Enabled || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return ClientId == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && ClientId == m_EnableDummyClientId);
}

bool CFastPractice::ShouldShowPracticeFrozenSkin(int ClientId) const
{
	if(!IsPracticeParticipant(ClientId))
		return false;
	return m_aFreezeSkinTicks[ClientId] >= FREEZE_SKIN_DEBOUNCE_TICKS;
}

void CFastPractice::UpdateFreezeSkinDebounce()
{
	if(!Active())
		return;

	const int Tick = m_PracticeWorld.GameTick();
	if(Tick == m_LastFreezeSkinDebounceTick)
		return;
	m_LastFreezeSkinDebounceTick = Tick;

	const int aIds[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(const int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;

		const bool FrozenNow = m_aPublishedValid[ClientId] && m_aPublishedPredicted[ClientId].m_FreezeEnd != 0;
		if(FrozenNow)
			m_aFreezeSkinTicks[ClientId] = std::min(m_aFreezeSkinTicks[ClientId] + 1, FREEZE_SKIN_DEBOUNCE_TICKS);
		else
			m_aFreezeSkinTicks[ClientId] = 0;
	}
}

int CFastPractice::CurrentPracticeDummyId() const
{
	if(!Active() || !m_RequireDummy)
		return -1;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return -1;
	if(IsRealSpectatorTeam(LocalClientId))
		return -1;
	return DummyClientId;
}

bool CFastPractice::GetLocalRaceState(SLocalRaceState &State) const
{
	State = {};
	if(!Active())
		return false;

	const int ClientId = ControlledPracticeId();
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || IsRealSpectatorTeam(ClientId))
		return false;

	const CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
	if(!pChar)
		return false;

	const SPracticeRaceState &RaceState = m_aPracticeRaceState[ClientId];
	State.m_Position = pChar->Core()->m_Pos;
	State.m_CurrentTick = m_PracticeWorld.GameTick();
	State.m_StartTick = RaceState.m_StartTick >= 0 ? RaceState.m_StartTick : GameClient()->LastRaceTick();
	State.m_Finished = RaceState.m_Finished;
	return true;
}

void CFastPractice::UpdatePracticeRaceState(int ClientId, const CCharacter *pChar, int Tick)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar || !Collision())
		return;

	SPracticeRaceState &RaceState = m_aPracticeRaceState[ClientId];
	if(RaceState.m_StartTick < 0 && !RaceState.m_Finished &&
		ClientId == ControlledPracticeId() && GameClient()->LastRaceTick() >= 0)
	{
		RaceState.m_StartTick = GameClient()->LastRaceTick();
	}

	const auto HasTile = [&](int Index, int WantedTile) {
		return Index >= 0 && (Collision()->GetTileIndex(Index) == WantedTile || Collision()->GetFrontTileIndex(Index) == WantedTile);
	};

	const vec2 Pos = pChar->GetPos();
	bool CornerOnStart = false;
	bool CornerOnFinish = false;
	const float Offset = pChar->GetProximityRadius() / 3.0f;
	static constexpr vec2 s_aCornerOffsets[] = {
		{1.0f, -1.0f},
		{1.0f, 1.0f},
		{-1.0f, -1.0f},
		{-1.0f, 1.0f},
	};
	for(const vec2 CornerOffset : s_aCornerOffsets)
	{
		const int Index = Collision()->GetPureMapIndex(Pos + CornerOffset * Offset);
		CornerOnStart |= HasTile(Index, TILE_START);
		CornerOnFinish |= HasTile(Index, TILE_FINISH);
	}

	const auto HandleMapIndex = [&](int Index) {
		const bool WasStarted = RaceState.m_StartTick >= 0 && !RaceState.m_Finished;
		if(CornerOnStart || HasTile(Index, TILE_START))
		{
			RaceState.m_StartTick = Tick;
			RaceState.m_Finished = false;
		}
		if((CornerOnFinish || HasTile(Index, TILE_FINISH)) && WasStarted)
			RaceState.m_Finished = true;
	};

	const vec2 PrevPos = pChar->m_PrevPos;
	const float Dist = distance(PrevPos, Pos);
	if(Dist <= 0.0f)
	{
		HandleMapIndex(Collision()->GetMapIndex(Pos));
		return;
	}

	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	if(Width <= 0 || Height <= 0)
	{
		HandleMapIndex(Collision()->GetMapIndex(Pos));
		return;
	}

	const int StartNx = std::clamp((int)PrevPos.x / 32, 0, Width - 1);
	const int StartNy = std::clamp((int)PrevPos.y / 32, 0, Height - 1);
	const int EndNx = std::clamp((int)Pos.x / 32, 0, Width - 1);
	const int EndNy = std::clamp((int)Pos.y / 32, 0, Height - 1);
	if(StartNx == EndNx && StartNy == EndNy)
	{
		HandleMapIndex(Collision()->GetMapIndex(Pos));
		return;
	}

	const int Steps = std::min((int)Dist + 1, 256);
	int LastIndex = -1;
	bool AnyTile = false;
	for(int i = 0; i < Steps; i++)
	{
		const float Alpha = (float)i / Dist;
		const vec2 Tmp = mix(PrevPos, Pos, Alpha);
		const int Nx = std::clamp((int)Tmp.x / 32, 0, Width - 1);
		const int Ny = std::clamp((int)Tmp.y / 32, 0, Height - 1);
		const int Index = Ny * Width + Nx;
		if(Index != LastIndex && Collision()->TileExists(Index))
		{
			HandleMapIndex(Index);
			LastIndex = Index;
			AnyTile = true;
		}
	}
	if(!AnyTile)
		HandleMapIndex(Collision()->GetMapIndex(Pos));
}

bool CFastPractice::ForcePredictWeapons() const
{
	return Active() && !IsRealSpectatorTeam(ControlledPracticeId());
}

bool CFastPractice::ForcePredictGrenade() const
{
	return Active() && !IsRealSpectatorTeam(ControlledPracticeId());
}

bool CFastPractice::ForcePredictGunfire() const
{
	return Active() && !IsRealSpectatorTeam(ControlledPracticeId());
}

bool CFastPractice::ForcePredictPlayers() const
{
	return Active() && !IsRealSpectatorTeam(ControlledPracticeId());
}

bool CFastPractice::HasPracticePickup(vec2 Pos, int Type, int Subtype)
{
	return Active() && HasPracticePickupAt(Pos, Type, Subtype);
}

bool CFastPractice::HasPracticePickupAt(vec2 Pos, int Type, int Subtype) const
{
	if(!m_MapEntitiesReady || m_MapWidth <= 0 || m_MapHeight <= 0)
		return false;

	const int BaseTx = std::clamp(round_to_int(Pos.x) / 32, 0, m_MapWidth - 1);
	const int BaseTy = std::clamp(round_to_int(Pos.y) / 32, 0, m_MapHeight - 1);
	const int MinTx = std::max(0, BaseTx - 1);
	const int MaxTx = std::min(m_MapWidth - 1, BaseTx + 1);
	const int MinTy = std::max(0, BaseTy - 1);
	const int MaxTy = std::min(m_MapHeight - 1, BaseTy + 1);

	for(int Ty = MinTy; Ty <= MaxTy; Ty++)
	{
		const int RowBase = Ty * m_MapWidth;
		for(int Tx = MinTx; Tx <= MaxTx; Tx++)
		{
			const STilePickupSlot &Slot = m_vTilePickupSlots[RowBase + Tx];
			for(uint8_t k = 0; k < Slot.m_Count; k++)
			{
				const SMapPickup &Pickup = m_vMapPickups[Slot.m_Start + k];
				if(Pickup.m_Type == Type && Pickup.m_Subtype == Subtype && distance(Pickup.m_Pos, Pos) < 17.0f)
					return true;
			}
		}
	}
	return false;
}

bool CFastPractice::HasFreezePickupNear(const vec2 &Pos, float Radius) const
{
	if(!m_MapEntitiesReady || m_MapWidth <= 0 || m_MapHeight <= 0)
		return false;

	const int BaseTx = std::clamp(round_to_int(Pos.x) / 32, 0, m_MapWidth - 1);
	const int BaseTy = std::clamp(round_to_int(Pos.y) / 32, 0, m_MapHeight - 1);
	const int MinTx = std::max(0, BaseTx - 2);
	const int MaxTx = std::min(m_MapWidth - 1, BaseTx + 2);
	const int MinTy = std::max(0, BaseTy - 2);
	const int MaxTy = std::min(m_MapHeight - 1, BaseTy + 2);

	for(int Ty = MinTy; Ty <= MaxTy; Ty++)
	{
		const int RowBase = Ty * m_MapWidth;
		for(int Tx = MinTx; Tx <= MaxTx; Tx++)
		{
			const STilePickupSlot &Slot = m_vTilePickupSlots[RowBase + Tx];
			for(uint8_t k = 0; k < Slot.m_Count; k++)
			{
				const SMapPickup &Pickup = m_vMapPickups[Slot.m_Start + k];
				if(Pickup.m_Type == POWERUP_FREEZE && distance(Pickup.m_Pos, Pos) < Radius)
					return true;
			}
		}
	}
	return false;
}

void CFastPractice::ClearMapEntities()
{
	if(Collision())
		Collision()->SetExtraDoorTiles(nullptr);
	m_MapEntitiesReady = false;
	m_MapWidth = 0;
	m_MapHeight = 0;
	m_vMapPickups.clear();
	m_vTilePickupSlots.clear();
	m_vMapDoors.clear();
	m_vExtraDoorTiles.clear();
}

void CFastPractice::BuildMapEntities()
{
	if(m_MapEntitiesReady || !Collision() || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0 || !Collision()->GameLayer())
		return;

	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	m_MapWidth = Width;
	m_MapHeight = Height;
	m_vMapPickups.clear();
	m_vTilePickupSlots.assign((size_t)Width * Height, STilePickupSlot{});
	m_vMapDoors.clear();
	m_vExtraDoorTiles.assign((size_t)Width * Height, CDoorTile{});

	static const int s_aSideX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	static const int s_aSideY[8] = {1, 1, 0, -1, -1, -1, 0, 1};

	auto AddPickup = [&](int Index, int x, int y, int Number, int TileFlags) {
		int Type = -1;
		int Subtype = 0;
		if(Index == ENTITY_ARMOR_1)
			Type = POWERUP_ARMOR;
		else if(Index == ENTITY_ARMOR_SHOTGUN)
			Type = POWERUP_ARMOR_SHOTGUN;
		else if(Index == ENTITY_ARMOR_GRENADE)
			Type = POWERUP_ARMOR_GRENADE;
		else if(Index == ENTITY_ARMOR_NINJA)
			Type = POWERUP_ARMOR_NINJA;
		else if(Index == ENTITY_ARMOR_LASER)
			Type = POWERUP_ARMOR_LASER;
		else if(Index == ENTITY_HEALTH_1)
			Type = POWERUP_FREEZE;
		else if(Index == ENTITY_WEAPON_SHOTGUN)
		{
			Type = POWERUP_WEAPON;
			Subtype = WEAPON_SHOTGUN;
		}
		else if(Index == ENTITY_WEAPON_GRENADE)
		{
			Type = POWERUP_WEAPON;
			Subtype = WEAPON_GRENADE;
		}
		else if(Index == ENTITY_WEAPON_LASER)
		{
			Type = POWERUP_WEAPON;
			Subtype = WEAPON_LASER;
		}
		else if(Index == ENTITY_POWERUP_NINJA)
		{
			Type = POWERUP_NINJA;
			Subtype = WEAPON_NINJA;
		}
		else
			return;

		int Flags = 0;
		if(TileFlags & TILEFLAG_XFLIP)
			Flags |= PICKUPFLAG_XFLIP;
		if(TileFlags & TILEFLAG_YFLIP)
			Flags |= PICKUPFLAG_YFLIP;
		if(TileFlags & TILEFLAG_ROTATE)
			Flags |= PICKUPFLAG_ROTATE;

		SMapPickup Pickup;
		Pickup.m_Pos = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
		Pickup.m_Type = Type;
		Pickup.m_Subtype = Subtype;
		Pickup.m_Number = Number;
		Pickup.m_Flags = Flags;
		vec2 DummyMover(0.0f, 0.0f);
		Pickup.m_IsMover = Collision()->MoverSpeed(round_to_int(Pickup.m_Pos.x), round_to_int(Pickup.m_Pos.y), &DummyMover) != 0;
		m_vMapPickups.push_back(Pickup);
	};

	auto AddDoor = [&](int x, int y, int Layer, int Number) {
		const vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
		const bool SolidOrigin = Collision()->GetTile(Pos.x, Pos.y) || Collision()->GetFrontTile(Pos.x, Pos.y);
		for(int Side = 0; Side < 8; Side++)
		{
			const int Neighbor = Collision()->Entity(x + s_aSideX[Side], y + s_aSideY[Side], Layer);
			if(Neighbor < ENTITY_LASER_SHORT || Neighbor > ENTITY_LASER_LONG)
				continue;
			const int Length = 32 * 3 + 32 * (Neighbor - ENTITY_LASER_SHORT) * 3;
			const vec2 Dir(std::sin(pi / 4 * Side), std::cos(pi / 4 * Side));
			vec2 To = Pos + Dir * Length;
			Collision()->IntersectNoLaser(Pos, To, &To, nullptr);

			SMapDoor Door;
			Door.m_From = Pos;
			Door.m_To = To;
			Door.m_Number = Number;
			m_vMapDoors.push_back(Door);
			if(SolidOrigin)
				continue;

			for(int i = 0; i < Length - 1; i++)
			{
				const vec2 CurrentPos = Pos + Dir * i;
				if(Collision()->CheckPoint(CurrentPos))
					break;
				const int Nx = std::clamp(round_to_int(CurrentPos.x) / 32, 0, Width - 1);
				const int Ny = std::clamp(round_to_int(CurrentPos.y) / 32, 0, Height - 1);
				CDoorTile &Tile = m_vExtraDoorTiles[(size_t)Ny * Width + Nx];
				Tile.m_Index = TILE_STOPA;
				Tile.m_Flags = 0;
				Tile.m_Number = (unsigned char)Number;
			}
		}
	};

	for(int y = 0; y < Height; y++)
	{
		for(int x = 0; x < Width; x++)
		{
			const int TileIdx = y * Width + x;
			const uint32_t StartIndex = (uint32_t)m_vMapPickups.size();
			const int GameIndex = Collision()->Entity(x, y, LAYER_GAME);
			if(GameIndex > 0)
			{
				AddPickup(GameIndex, x, y, 0, Collision()->GetTileFlags(TileIdx));
				if(GameIndex == ENTITY_DOOR)
					AddDoor(x, y, LAYER_GAME, 0);
			}
			if(Collision()->FrontLayer())
			{
				const int FrontIndex = Collision()->Entity(x, y, LAYER_FRONT);
				if(FrontIndex > 0)
				{
					AddPickup(FrontIndex, x, y, 0, Collision()->GetFrontTileFlags(TileIdx));
					if(FrontIndex == ENTITY_DOOR)
						AddDoor(x, y, LAYER_FRONT, 0);
				}
			}
			if(Collision()->SwitchLayer())
			{
				const int SwitchIndex = Collision()->Entity(x, y, LAYER_SWITCH);
				if(SwitchIndex > 0)
				{
					const CSwitchTile &Switch = Collision()->SwitchLayer()[TileIdx];
					AddPickup(SwitchIndex, x, y, Switch.m_Number, Switch.m_Flags);
					if(SwitchIndex == ENTITY_DOOR)
						AddDoor(x, y, LAYER_SWITCH, Switch.m_Number);
				}
			}
			const uint32_t Added = (uint32_t)m_vMapPickups.size() - StartIndex;
			m_vTilePickupSlots[TileIdx].m_Start = StartIndex;
			m_vTilePickupSlots[TileIdx].m_Count = (uint8_t)std::min<uint32_t>(Added, 255u);
		}
	}

	Collision()->SetExtraDoorTiles(m_vExtraDoorTiles.data());
	m_MapEntitiesReady = true;
}

void CFastPractice::SpawnStaticPickupsInWorld(CGameWorld &World) const
{
	int NextId = STATIC_PICKUP_ID_BASE;
	for(const SMapPickup &Pickup : m_vMapPickups)
	{
		if(Pickup.m_IsMover)
			continue;
		CPickupData Data;
		Data.m_Pos = Pickup.m_Pos;
		Data.m_Type = Pickup.m_Type;
		Data.m_Subtype = Pickup.m_Subtype;
		Data.m_SwitchNumber = Pickup.m_Number;
		Data.m_Flags = Pickup.m_Flags;
		World.InsertEntity(new CPickup(&World, NextId, &Data));
		NextId--;
	}
}

void CFastPractice::SpawnMoverPickupsInWorld(CGameWorld &World) const
{
	int NextId = -2;
	for(const SMapPickup &Pickup : m_vMapPickups)
	{
		if(!Pickup.m_IsMover)
			continue;
		bool Exists = false;
		for(auto *pPickup = (CPickup *)World.FindFirst(CGameWorld::ENTTYPE_PICKUP); pPickup; pPickup = (CPickup *)pPickup->NextEntity())
		{
			if(pPickup->Type() == Pickup.m_Type && pPickup->Subtype() == Pickup.m_Subtype && distance(pPickup->GetPos(), Pickup.m_Pos) < 17.0f)
			{
				Exists = true;
				break;
			}
		}
		if(Exists)
			continue;

		CPickupData Data;
		Data.m_Pos = Pickup.m_Pos;
		Data.m_Type = Pickup.m_Type;
		Data.m_Subtype = Pickup.m_Subtype;
		Data.m_SwitchNumber = Pickup.m_Number;
		Data.m_Flags = Pickup.m_Flags;
		World.InsertEntity(new CPickup(&World, NextId, &Data));
		NextId--;
	}
}

void CFastPractice::ApplyStaticPickups(CGameWorld &World, CCharacter *pChar) const
{
	if(!pChar || !m_MapEntitiesReady || m_MapWidth <= 0 || m_MapHeight <= 0)
		return;

	const vec2 Pos = pChar->m_Pos;
	const float HitRadius = 14.0f + CPickup::ms_CollisionExtraSize + pChar->GetProximityRadius();
	const float VanillaRadius = (14.0f + CPickup::ms_CollisionExtraSize) * 2.0f;
	const bool IsVanilla = World.m_WorldConfig.m_IsVanilla;
	const bool IsDDRace = World.m_WorldConfig.m_IsDDRace;
	const bool PredictDDRace = World.m_WorldConfig.m_PredictDDRace;
	const int Team = pChar->Team();
	const int TeamSuper = pChar->TeamsCore()->TeamSuper();
	const std::vector<SSwitchers> &vSwitchers = World.Switchers();

	const int BaseTx = std::clamp(round_to_int(Pos.x) / 32, 0, m_MapWidth - 1);
	const int BaseTy = std::clamp(round_to_int(Pos.y) / 32, 0, m_MapHeight - 1);
	const int MinTx = std::max(0, BaseTx - 2);
	const int MaxTx = std::min(m_MapWidth - 1, BaseTx + 2);
	const int MinTy = std::max(0, BaseTy - 2);
	const int MaxTy = std::min(m_MapHeight - 1, BaseTy + 2);

	for(int Ty = MinTy; Ty <= MaxTy; Ty++)
	{
		const int RowBase = Ty * m_MapWidth;
		for(int Tx = MinTx; Tx <= MaxTx; Tx++)
		{
			const STilePickupSlot &Slot = m_vTilePickupSlots[RowBase + Tx];
			for(uint8_t k = 0; k < Slot.m_Count; k++)
			{
				const SMapPickup &Pickup = m_vMapPickups[Slot.m_Start + k];
				if(Pickup.m_IsMover)
					continue;
				const float Dist = distance(Pickup.m_Pos, Pos);
				if(Dist >= HitRadius)
					continue;
				if(IsVanilla && Dist >= VanillaRadius)
					continue;
				if(Pickup.m_Number > 0 && Team >= 0 && Team < NUM_DDRACE_TEAMS &&
					Pickup.m_Number < (int)vSwitchers.size() && !vSwitchers[Pickup.m_Number].m_aStatus[Team])
				{
					continue;
				}

				switch(Pickup.m_Type)
				{
				case POWERUP_FREEZE:
					if(PredictDDRace)
						pChar->Freeze();
					break;

				case POWERUP_ARMOR:
					if(IsDDRace && PredictDDRace && !pChar->IsSuper())
					{
						bool TookWeapon = false;
						for(int j = WEAPON_SHOTGUN; j < NUM_WEAPONS; j++)
						{
							if(pChar->GetWeaponGot(j))
							{
								pChar->SetWeaponGot(j, false);
								pChar->SetWeaponAmmo(j, 0);
								TookWeapon = true;
							}
						}
						pChar->SetNinjaActivationDir(vec2(0, 0));
						pChar->SetNinjaActivationTick(-500);
						pChar->SetNinjaCurrentMoveTime(0);
						if(TookWeapon)
							pChar->SetLastWeapon(WEAPON_GUN);
						if(pChar->GetActiveWeapon() >= WEAPON_SHOTGUN)
							pChar->SetActiveWeapon(WEAPON_HAMMER);
					}
					break;

				case POWERUP_ARMOR_SHOTGUN:
					if(IsDDRace && PredictDDRace && Team != TeamSuper)
					{
						if(pChar->GetWeaponGot(WEAPON_SHOTGUN))
						{
							pChar->SetWeaponGot(WEAPON_SHOTGUN, false);
							pChar->SetWeaponAmmo(WEAPON_SHOTGUN, 0);
							pChar->SetLastWeapon(WEAPON_GUN);
						}
						if(pChar->GetActiveWeapon() == WEAPON_SHOTGUN)
							pChar->SetActiveWeapon(WEAPON_HAMMER);
					}
					break;

				case POWERUP_ARMOR_GRENADE:
					if(IsDDRace && PredictDDRace && Team != TeamSuper)
					{
						if(pChar->GetWeaponGot(WEAPON_GRENADE))
						{
							pChar->SetWeaponGot(WEAPON_GRENADE, false);
							pChar->SetWeaponAmmo(WEAPON_GRENADE, 0);
							pChar->SetLastWeapon(WEAPON_GUN);
						}
						if(pChar->GetActiveWeapon() == WEAPON_GRENADE)
							pChar->SetActiveWeapon(WEAPON_HAMMER);
					}
					break;

				case POWERUP_ARMOR_NINJA:
					if(IsDDRace && PredictDDRace && Team != TeamSuper)
					{
						pChar->SetNinjaActivationDir(vec2(0, 0));
						pChar->SetNinjaActivationTick(-500);
						pChar->SetNinjaCurrentMoveTime(0);
					}
					break;

				case POWERUP_ARMOR_LASER:
					if(IsDDRace && PredictDDRace && Team != TeamSuper)
					{
						if(pChar->GetWeaponGot(WEAPON_LASER))
						{
							pChar->SetWeaponGot(WEAPON_LASER, false);
							pChar->SetWeaponAmmo(WEAPON_LASER, 0);
							pChar->SetLastWeapon(WEAPON_GUN);
						}
						if(pChar->GetActiveWeapon() == WEAPON_LASER)
							pChar->SetActiveWeapon(WEAPON_HAMMER);
					}
					break;

				case POWERUP_WEAPON:
					if(Pickup.m_Subtype >= 0 && Pickup.m_Subtype < NUM_WEAPONS &&
						(!pChar->GetWeaponGot(Pickup.m_Subtype) || pChar->GetWeaponAmmo(Pickup.m_Subtype) != -1))
					{
						pChar->GiveWeapon(Pickup.m_Subtype);
					}
					break;

				case POWERUP_NINJA:
					pChar->GiveNinja();
					break;

				default:
					break;
				}
			}
		}
	}
}

void CFastPractice::CopyDynamicWorld(CGameWorld &Dst, CGameWorld &Src, bool PreserveStaticPickups) const
{
	if(&Dst == &Src)
		return;

	if(!Dst.Collision() || Dst.Collision() != Src.Collision() || Dst.TuningList() != Src.TuningList())
	{
		Dst.CopyWorldClean(&Src);
		if(PreserveStaticPickups)
			SpawnStaticPickupsInWorld(Dst);
		return;
	}

	Dst.m_IsValidCopy = false;
	Dst.m_GameTick = Src.m_GameTick;
	Dst.m_WorldConfig = Src.m_WorldConfig;
	Dst.m_Teams = Src.m_Teams;
	Dst.m_Core.m_vSwitchers = Src.m_Core.m_vSwitchers;
	Dst.m_PredictedEvents.clear();

	static constexpr int s_aDynamicTypes[] = {
		CGameWorld::ENTTYPE_PROJECTILE,
		CGameWorld::ENTTYPE_LASER,
		CGameWorld::ENTTYPE_DRAGGER,
		CGameWorld::ENTTYPE_PLASMA,
		CGameWorld::ENTTYPE_CHARACTER,
		CGameWorld::ENTTYPE_PICKUP,
	};

	for(const int Type : s_aDynamicTypes)
	{
		for(CEntity *pEnt = Dst.FindFirst(Type), *pNext = nullptr; pEnt; pEnt = pNext)
		{
			pNext = pEnt->TypeNext();
			if(Type == CGameWorld::ENTTYPE_PICKUP && PreserveStaticPickups && pEnt->GetId() <= STATIC_PICKUP_ID_BASE)
				continue;
			pEnt->Destroy();
		}

		for(CEntity *pEnt = Src.FindLast(Type); pEnt; pEnt = pEnt->TypePrev())
		{
			if(Type == CGameWorld::ENTTYPE_PICKUP && PreserveStaticPickups && pEnt->GetId() <= STATIC_PICKUP_ID_BASE)
				continue;

			CEntity *pCopy = nullptr;
			if(Type == CGameWorld::ENTTYPE_PROJECTILE)
				pCopy = new CProjectile(*((const CProjectile *)pEnt));
			else if(Type == CGameWorld::ENTTYPE_LASER)
				pCopy = new CLaser(*((const CLaser *)pEnt));
			else if(Type == CGameWorld::ENTTYPE_DRAGGER)
				pCopy = new CDragger(*((const CDragger *)pEnt));
			else if(Type == CGameWorld::ENTTYPE_PLASMA)
				pCopy = new CPlasma(*((const CPlasma *)pEnt));
			else if(Type == CGameWorld::ENTTYPE_CHARACTER)
				pCopy = new CCharacter(*((const CCharacter *)pEnt));
			else if(Type == CGameWorld::ENTTYPE_PICKUP)
				pCopy = new CPickup(*((const CPickup *)pEnt));

			if(pCopy)
			{
				pCopy->m_pParent = nullptr;
				pCopy->m_pChild = nullptr;
				Dst.InsertEntity(pCopy);
			}
		}
	}
}

void CFastPractice::SyncPracticePrevWorld()
{
	CopyDynamicWorld(m_PracticePrevWorld, m_PracticeWorld, true);
	SyncPracticeWorldConfig(m_PracticePrevWorld);
	m_PracticePrevWorld.m_PredictedEvents.clear();
}

void CFastPractice::RenderMapDoors()
{
	if(!Active())
		return;
	const int ClientId = ControlledPracticeId();
	CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
	if(!pChar)
		return;

	const bool Super = pChar->IsSuper();
	const int Team = pChar->Team();
	if(!Super && (Team < 0 || Team >= NUM_DDRACE_TEAMS))
		return;

	CScreenRect ScreenRect = Graphics()->GetScreen();
	ScreenRect.Expand(64.0f);

	const std::vector<SSwitchers> &vSwitchers = m_PracticeWorld.Switchers();
	for(const SMapDoor &Door : m_vMapDoors)
	{
		if((Door.m_From.x < ScreenRect.m_TopLeft.x && Door.m_To.x < ScreenRect.m_TopLeft.x) ||
			(Door.m_From.x > ScreenRect.m_BottomRight.x && Door.m_To.x > ScreenRect.m_BottomRight.x) ||
			(Door.m_From.y < ScreenRect.m_TopLeft.y && Door.m_To.y < ScreenRect.m_TopLeft.y) ||
			(Door.m_From.y > ScreenRect.m_BottomRight.y && Door.m_To.y > ScreenRect.m_BottomRight.y))
		{
			continue;
		}

		const bool Inactive = !Super && Door.m_Number > 0 && Door.m_Number < (int)vSwitchers.size() && !vSwitchers[Door.m_Number].m_aStatus[Team];
		CLaserData Data{};
		Data.m_To = Door.m_From;
		Data.m_From = (Inactive || Super) ? Door.m_From : Door.m_To;
		Data.m_StartTick = -1;
		Data.m_ExtraInfo = true;
		Data.m_Owner = -1;
		Data.m_Type = LASERTYPE_DOOR;
		Data.m_SwitchNumber = Door.m_Number;
		Data.m_Subtype = 0;
		Data.m_Predict = true;
		GameClient()->m_Items.RenderLaserData(&Data);
	}
}

void CFastPractice::PrunePracticeWorld(CGameWorld &World) const
{
	for(CCharacter *pChar = (CCharacter *)World.FindFirst(CGameWorld::ENTTYPE_CHARACTER), *pCharNext = nullptr; pChar; pChar = pCharNext)
	{
		pCharNext = (CCharacter *)pChar->TypeNext();
		const int ClientId = pChar->GetCid();
		if(ClientId != m_EnableLocalClientId && ClientId != m_EnableDummyClientId)
			pChar->Destroy();
	}

	for(const int ClientId : {m_EnableLocalClientId, m_EnableDummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(CCharacter *pChar = World.GetCharacterById(ClientId))
		{
			const int Hooked = pChar->Core()->HookedPlayer();
			if(Hooked >= 0 && Hooked != m_EnableLocalClientId && Hooked != m_EnableDummyClientId)
				pChar->ReleaseHook();
		}
	}

	for(CProjectile *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), *pProjNext = nullptr; pProj; pProj = pProjNext)
	{
		pProjNext = (CProjectile *)pProj->TypeNext();
		const int Owner = pProj->GetOwner();
		if(Owner >= 0 && Owner != m_EnableLocalClientId && Owner != m_EnableDummyClientId)
			pProj->Destroy();
	}

	for(CLaser *pLaser = (CLaser *)World.FindFirst(CGameWorld::ENTTYPE_LASER), *pLaserNext = nullptr; pLaser; pLaser = pLaserNext)
	{
		pLaserNext = (CLaser *)pLaser->TypeNext();
		const int Owner = pLaser->GetOwner();
		if(Owner >= 0 && Owner != m_EnableLocalClientId && Owner != m_EnableDummyClientId)
			pLaser->Destroy();
	}

	for(CPickup *pPickup = (CPickup *)World.FindFirst(CGameWorld::ENTTYPE_PICKUP), *pPickupNext = nullptr; pPickup; pPickup = pPickupNext)
	{
		pPickupNext = (CPickup *)pPickup->TypeNext();
		if(pPickup->InDDNetTile())
			continue;
		if(HasPracticePickupAt(pPickup->GetPos(), pPickup->Type(), pPickup->Subtype()))
			pPickup->Destroy();
	}
}

void CFastPractice::ClearPracticeProjectilesAndLasers(CGameWorld &World) const
{
	for(CProjectile *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), *pProjNext = nullptr; pProj; pProj = pProjNext)
	{
		pProjNext = (CProjectile *)pProj->TypeNext();
		const int Owner = pProj->GetOwner();
		if(Owner == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && Owner == m_EnableDummyClientId))
			pProj->Destroy();
	}

	for(CLaser *pLaser = (CLaser *)World.FindFirst(CGameWorld::ENTTYPE_LASER), *pLaserNext = nullptr; pLaser; pLaser = pLaserNext)
	{
		pLaserNext = (CLaser *)pLaser->TypeNext();
		const int Owner = pLaser->GetOwner();
		if(Owner == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && Owner == m_EnableDummyClientId))
			pLaser->Destroy();
	}
}

void CFastPractice::SyncPracticeWorldConfig(CGameWorld &World)
{
	World.m_WorldConfig = GameClient()->m_GameWorld.m_WorldConfig;
	World.m_WorldConfig.m_PredictWeapons = true;
	World.m_WorldConfig.m_PredictFreeze = true;
	World.m_WorldConfig.m_PredictTiles = true;
	World.m_WorldConfig.m_PredictDDRace = true;
	World.m_WorldConfig.m_PredictEvents = false;

	const auto &vSrcSwitchers = GameClient()->m_GameWorld.m_Core.m_vSwitchers;
	if(World.m_Core.m_vSwitchers.size() < vSrcSwitchers.size())
	{
		const size_t OldSize = World.m_Core.m_vSwitchers.size();
		World.m_Core.m_vSwitchers.resize(vSrcSwitchers.size());
		for(size_t i = OldSize; i < vSrcSwitchers.size(); i++)
			World.m_Core.m_vSwitchers[i] = vSrcSwitchers[i];
	}

	bool aPracticeSolo[MAX_CLIENTS];
	for(int i = 0; i < MAX_CLIENTS; i++)
		aPracticeSolo[i] = World.m_Teams.GetSolo(i);

	World.m_Teams = GameClient()->m_Teams;

	const int aIds[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(const int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(const CCharacter *pChar = World.GetCharacterById(ClientId))
		{
			World.m_Teams.SetSolo(ClientId, pChar->Core()->m_Solo);
			if(pChar->IsSuper())
				World.m_Teams.Team(ClientId, World.m_Teams.TeamSuper());
		}
		else
		{
			World.m_Teams.SetSolo(ClientId, aPracticeSolo[ClientId]);
		}
	}
}

void CFastPractice::SpawnParticipantFromAnchor(CGameWorld &World, const SAnchorData &Anchor) const
{
	if(!Anchor.m_Valid || Anchor.m_ClientId < 0 || Anchor.m_ClientId >= MAX_CLIENTS)
		return;
	if(World.GetCharacterById(Anchor.m_ClientId))
		return;

	CNetObj_Character CharObj = Anchor.m_Char;
	CNetObj_DDNetCharacter DDNetObj = Anchor.m_DDNet;
	CCharacter *pChar = new CCharacter(&World, Anchor.m_ClientId, &CharObj, Anchor.m_HasDDNet ? &DDNetObj : nullptr);
	World.InsertEntity(pChar);
	pChar->Read(&CharObj, Anchor.m_HasDDNet ? &DDNetObj : nullptr, true);
	pChar->m_GameTeam = GameClient()->IsTeamPlay() ? GameClient()->m_aClients[Anchor.m_ClientId].m_Team : Anchor.m_ClientId;
}

bool CFastPractice::Rebuild()
{
	if(m_EnableLocalClientId < 0 || m_EnableLocalClientId >= MAX_CLIENTS)
		return false;

	BuildMapEntities();
	m_PracticeWorld.CopyWorldClean(&GameClient()->m_GameWorld);
	PrunePracticeWorld(m_PracticeWorld);
	SpawnMoverPickupsInWorld(m_PracticeWorld);
	SyncPracticeWorldConfig(m_PracticeWorld);
	m_PracticeWorld.m_PredictedEvents.clear();
	ResetPracticeRaceStates();

	if(!m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		SpawnParticipantFromAnchor(m_PracticeWorld, m_MainAnchor);
	if(m_RequireDummy && m_EnableDummyClientId >= 0 && !m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
		SpawnParticipantFromAnchor(m_PracticeWorld, m_DummyAnchor);

	if(!m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
	{
		m_Ready = false;
		return false;
	}
	if(m_RequireDummy && !m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
	{
		m_Ready = false;
		return false;
	}

	m_aSpawnPosValid.fill(false);
	m_aSafePosValid.fill(false);
	m_aFastRenderValid.fill(false);
	m_aLastEventTick.fill(-1);

	CaptureAnchorsFromSnapshot();
	CaptureFrozenTargets();
	CaptureServerLockedTargets();
	m_LastClDummy = g_Config.m_ClDummy;

	const int aIds[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
		{
			const vec2 Pos = pChar->Core()->m_Pos;
			m_aSpawnPos[ClientId] = Pos;
			m_aSpawnPosValid[ClientId] = true;
			if(IsSafeRescuePosition(Pos, pChar->GetProximityRadius()))
			{
				m_aSafePos[ClientId] = Pos;
				m_aSafePosValid[ClientId] = true;
			}
			CNetObj_PlayerInput Neutral = {};
			BuildLiveInputByConn(Neutral, ConnectionForClientId(ClientId));
			NeutralizeInput(Neutral);
			Neutral.m_Fire = ReleasedFireState(Neutral.m_Fire);
			const int Slot = ConnectionForClientId(ClientId);
			if(m_aHasServerLockedTargets[Slot])
			{
				Neutral.m_Fire = m_aServerLockedFire[Slot];
				Neutral.m_NextWeapon = m_aServerLockedNextWeapon[Slot];
				Neutral.m_PrevWeapon = m_aServerLockedPrevWeapon[Slot];
			}
			pChar->SetInput(&Neutral);
			pChar->m_CanMoveInFreeze = false;
		}
	}

	ResetStoredInputs();
	const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		StoreNeutralInput(Dummy != 0, PredTick);

	m_PracticeWorld.m_GameTick = PredTick;
	m_PracticePrevWorld.CopyWorldClean(&m_PracticeWorld);
	SpawnStaticPickupsInWorld(m_PracticePrevWorld);
	SyncPracticeWorldConfig(m_PracticePrevWorld);
	m_PracticePrevWorld.m_PredictedEvents.clear();
	m_FastOverrunWorld.Clear();
	m_FastOverrunWorld.Init(nullptr, nullptr, nullptr);

	m_Ready = true;
	m_NeedsRebuild = false;
	ResetAttackTickHistory();
	SeedPredictionHistory();
	PublishParticipantCores(m_EnableLocalClientId, m_EnableDummyClientId);
	return true;
}

void CFastPractice::CaptureAnchorsFromSnapshot()
{
	const auto &&Capture = [&](int ClientId, SAnchorData &Anchor) {
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		{
			Anchor = {};
			return;
		}
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			// Preserve existing valid anchor when original tee is in /spec or briefly dead
			if(Anchor.m_Valid && Anchor.m_ClientId == ClientId)
				return;
			Anchor = {};
			return;
		}

		Anchor.m_Valid = true;
		Anchor.m_ClientId = ClientId;
		Anchor.m_Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		Anchor.m_HasDDNet = GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedData;
		if(Anchor.m_HasDDNet)
			Anchor.m_DDNet = GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData;
	};

	Capture(m_EnableLocalClientId, m_MainAnchor);
	Capture(m_EnableDummyClientId, m_DummyAnchor);
	m_HasDummyAnchor = m_DummyAnchor.m_Valid;
}

bool CFastPractice::ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const
{
	if(!Anchor.m_Valid)
		return false;

	CCharacter *pChar = World.GetCharacterById(Anchor.m_ClientId);
	if(!pChar)
		return false;

	CNetObj_Character CharObj = Anchor.m_Char;
	if(Anchor.m_HasDDNet)
	{
		CNetObj_DDNetCharacter DDNetObj = Anchor.m_DDNet;
		pChar->Read(&CharObj, &DDNetObj, true);
	}
	else
	{
		pChar->Read(&CharObj, nullptr, true);
	}

	CNetObj_PlayerInput NeutralInput = {};
	BuildLiveInputByConn(NeutralInput, ConnectionForClientId(Anchor.m_ClientId));
	NeutralizeInput(NeutralInput);
	NeutralInput.m_Fire = ReleasedFireState(NeutralInput.m_Fire);
	pChar->SetInput(&NeutralInput);
	pChar->m_CanMoveInFreeze = false;
	return true;
}

void CFastPractice::ResetAttackTickHistory()
{
	m_aLastAttackTick.fill(-1);

	if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		m_aLastAttackTick[m_EnableLocalClientId] = pChar->GetAttackTick();
	if(m_EnableDummyClientId >= 0)
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			m_aLastAttackTick[m_EnableDummyClientId] = pChar->GetAttackTick();
}

void CFastPractice::ReleaseBufferedInputState()
{
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		NeutralizeInput(GameClient()->m_Controls.m_aInputData[Conn]);
		NeutralizeInput(GameClient()->m_Controls.m_aLastData[Conn]);
		GameClient()->m_Controls.m_aInputData[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aInputData[Conn].m_Fire);
		GameClient()->m_Controls.m_aLastData[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aLastData[Conn].m_Fire);
		GameClient()->m_Controls.m_aFastInput[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aFastInput[Conn].m_Fire);
		NeutralizeInput(GameClient()->m_Controls.m_aFastInput[Conn]);
	}

	NeutralizeInput(GameClient()->m_DummyInput);
	GameClient()->m_DummyInput.m_Fire = ReleasedFireState(GameClient()->m_DummyInput.m_Fire);
	NeutralizeInput(GameClient()->m_HammerInput);
	GameClient()->m_HammerInput.m_Fire = ReleasedFireState(GameClient()->m_HammerInput.m_Fire);
	GameClient()->m_DummyFire = 0;
}

void CFastPractice::InvalidateBufferedInputState()
{
	if(!m_Enabled)
		return;
	ReleaseBufferedInputState();
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
}

void CFastPractice::CaptureServerLockedTargets()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		if(m_aHasServerLockedTargets[Slot])
			continue;
		const CNetObj_PlayerInput &LastInput = GameClient()->m_Controls.m_aLastData[Slot];
		const CNetObj_PlayerInput &CurInput = GameClient()->m_Controls.m_aInputData[Slot];
		const CNetObj_PlayerInput &Input = (LastInput.m_TargetX != 0 || LastInput.m_TargetY != 0) ? LastInput : CurInput;
		int TargetX = Input.m_TargetX;
		int TargetY = Input.m_TargetY;
		if(TargetX == 0 && TargetY == 0)
		{
			TargetX = 1;
			TargetY = 0;
		}
		m_aServerLockedTargets[Slot] = ivec2(TargetX, TargetY);
		m_aServerLockedFire[Slot] = ReleasedFireState(Input.m_Fire);
		m_aServerLockedNextWeapon[Slot] = Input.m_NextWeapon & INPUT_STATE_MASK;
		m_aServerLockedPrevWeapon[Slot] = Input.m_PrevWeapon & INPUT_STATE_MASK;
		m_aHasServerLockedTargets[Slot] = true;
	}
}

void CFastPractice::RestoreServerLockedInputs()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		if(!m_aHasServerLockedTargets[Slot])
			continue;
		auto SyncSlot = [&](CNetObj_PlayerInput &Dst) {
			Dst.m_Direction = 0;
			Dst.m_Jump = 0;
			Dst.m_Hook = 0;
			Dst.m_WantedWeapon = 0;
			Dst.m_Fire = m_aServerLockedFire[Slot];
			Dst.m_NextWeapon = m_aServerLockedNextWeapon[Slot];
			Dst.m_PrevWeapon = m_aServerLockedPrevWeapon[Slot];
		};
		SyncSlot(GameClient()->m_Controls.m_aInputData[Slot]);
		SyncSlot(GameClient()->m_Controls.m_aLastData[Slot]);
		SyncSlot(GameClient()->m_Controls.m_aFastInput[Slot]);
	}
	if(m_aHasServerLockedTargets[!g_Config.m_ClDummy])
	{
		const int DummySlot = !g_Config.m_ClDummy;
		GameClient()->m_DummyInput.m_Fire = m_aServerLockedFire[DummySlot];
		GameClient()->m_DummyInput.m_NextWeapon = m_aServerLockedNextWeapon[DummySlot];
		GameClient()->m_DummyInput.m_PrevWeapon = m_aServerLockedPrevWeapon[DummySlot];
	}
}

void CFastPractice::Enable()
{
	if(m_Enabled || !CanEnable())
		return;

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int InactiveConn = ActiveConn ^ 1;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	m_EnableLocalClientId = (ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS) ? ActiveClientId : GameClient()->m_Snap.m_LocalClientId;

	const int CandidateDummyId = Client()->DummyConnected() ? GameClient()->m_aLocalIds[InactiveConn] : -1;
	m_EnableDummyClientId = CandidateDummyId;
	if(m_EnableDummyClientId < 0 || m_EnableDummyClientId >= MAX_CLIENTS ||
		!GameClient()->m_Snap.m_aCharacters[m_EnableDummyClientId].m_Active || GameClient()->m_aClients[m_EnableDummyClientId].m_Paused)
	{
		m_EnableDummyClientId = -1;
	}
	m_RequireDummy = m_EnableDummyClientId >= 0;

	if(m_RequireDummy &&
		(GameClient()->m_aClients[m_EnableLocalClientId].m_Solo || GameClient()->m_aClients[m_EnableDummyClientId].m_Solo))
	{
		EchoPractice("you and dummy must not be in solo");
		m_EnableLocalClientId = -1;
		m_EnableDummyClientId = -1;
		m_RequireDummy = false;
		return;
	}

	// Capture server-locked fire/weapon counters before neutralizing controls
	m_aHasServerLockedTargets.fill(false);
	CaptureServerLockedTargets();

	if(!Rebuild())
	{
		ResetPracticeState();
		return;
	}

	CaptureAnchorsFromSnapshot();
	if(!m_MainAnchor.m_Valid || (m_RequireDummy && !m_HasDummyAnchor))
	{
		ResetPracticeState();
		return;
	}

	m_Enabled = true;
	m_LastRoundStartTick = GameClient()->m_Snap.m_pGameInfoObj ? GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick : -1;
	AcInputs::SetPracticeActive(true);
	if(g_Config.m_AcFastPracticeSpecOriginal)
		SendSpecOriginal();
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	GameClient()->m_PredictedWorld.m_PredictedEvents.clear();
	GameClient()->m_GameWorld.m_PredictedEvents.clear();
	ResetCommandState();
	ResetPracticeSaves();
	if(CCharacter *pLocal = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pLocal);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	ReleaseBufferedInputState();
	PublishParticipantCores(m_EnableLocalClientId, m_EnableDummyClientId);
	GameClient()->m_PredictedTick = m_PracticeWorld.GameTick();
}

void CFastPractice::Disable()
{
	if(!m_Enabled)
		return;

	const int LocalClientId = m_EnableLocalClientId;
	const int DummyClientId = m_EnableDummyClientId;

	for(const int ClientId : {LocalClientId, DummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		const bool ServerSolo = GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedData &&
					(GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_SOLO);
		GameClient()->m_aClients[ClientId].m_Solo = ServerSolo;
		GameClient()->m_Teams.SetSolo(ClientId, ServerSolo);
		std::fill(std::begin(GameClient()->m_aClients[ClientId].m_aPredTick), std::end(GameClient()->m_aClients[ClientId].m_aPredTick), -1);
	}

	RestoreSpecPracticeView();

	if(Client()->State() == IClient::STATE_ONLINE && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		for(const int ClientId : {LocalClientId, DummyClientId})
		{
			if(ClientId < 0 || ClientId >= MAX_CLIENTS)
				continue;
			if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			{
				CCharacterCore SnapCore;
				SnapCore.Reset();
				SnapCore.Read(&GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur);
				if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedData)
					SnapCore.ReadDDNet(&GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData);
				SnapCore.m_ActiveWeapon = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Weapon;
				GameClient()->m_aClients[ClientId].m_Predicted = SnapCore;
				GameClient()->m_aClients[ClientId].m_PrevPredicted = SnapCore;
				GameClient()->m_aClients[ClientId].m_RegularPredicted = SnapCore;
				if(ClientId == GameClient()->m_Snap.m_LocalClientId)
				{
					GameClient()->m_PredictedChar = SnapCore;
					GameClient()->m_PredictedPrevChar = SnapCore;
					GameClient()->m_LocalCharacterPos = SnapCore.m_Pos;
				}
			}
		}
	}

	GameClient()->m_PredictedDummyId = -1;
	GameClient()->m_PredictedWorld.m_PredictedEvents.clear();
	GameClient()->m_GameWorld.m_PredictedEvents.clear();
	ReleaseBufferedInputState();
	RestoreServerLockedInputs();
	ResetPracticeState();
}

void CFastPractice::Toggle()
{
	if(m_Enabled)
		Disable();
	else
		Enable();
}

bool CFastPractice::ConsumeKillCommand()
{
	if(!m_Enabled)
		return false;
	ResetPracticeToAnchor();
	return true;
}

void CFastPractice::ResetPracticeToAnchor()
{
	if(!m_Enabled)
		return;

	CaptureAnchorsFromSnapshot();
	if(!m_MainAnchor.m_Valid || (m_RequireDummy && !m_HasDummyAnchor))
	{
		Disable();
		return;
	}

	if(!Rebuild())
	{
		Disable();
		return;
	}

	if(!ApplyAnchorToCharacter(m_PracticeWorld, m_MainAnchor))
	{
		Disable();
		return;
	}

	if(m_RequireDummy && !ApplyAnchorToCharacter(m_PracticeWorld, m_DummyAnchor))
	{
		Disable();
		return;
	}

	if(CCharacter *pMain = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
	{
		NormalizeCharacterAfterReset(pMain, false);
		m_aPracticeCommandState[m_EnableLocalClientId].m_InvincibleAddedEndlessJump = false;
	}
	if(m_RequireDummy)
	{
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
		{
			NormalizeCharacterAfterReset(pDummy, false);
			m_aPracticeCommandState[m_EnableDummyClientId].m_InvincibleAddedEndlessJump = false;
		}
	}

	m_PracticeWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	m_aFreezeSkinTicks.fill(0);
	ResetAttackTickHistory();
	if(CCharacter *pMain = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pMain);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	ReleaseBufferedInputState();

	const int ControlledId = ControlledPracticeId();
	const int PartnerId = PartnerPracticeId();
	FinishMutation(ControlledId, PartnerId, m_PracticeWorld.GetCharacterById(ControlledId), false);
}

void CFastPractice::PrepareInputForSend(int *pData, int Size, bool Dummy)
{
	if(!m_Enabled || !pData)
		return;

	const int ControlledId = ControlledPracticeId();
	if(IsRealSpectatorTeam(ControlledId))
		return;

	const int Conn = g_Config.m_ClDummy ^ (int)Dummy;
	CNetObj_PlayerInput Source{};
	if(Size >= (int)sizeof(CNetObj_PlayerInput))
		mem_copy(&Source, pData, sizeof(Source));
	else
		BuildLiveInputByConn(Source, Conn);

	if(m_LastClDummy != g_Config.m_ClDummy)
	{
		m_LastClDummy = g_Config.m_ClDummy;
		m_SuppressFireOnNextPredictTick = true;
		m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
		ReleaseBufferedInputState();
		NeutralizeInput(Source);
		Source.m_Fire = ReleasedFireState(Source.m_Fire);
	}

	if(Conn >= 0 && Conn < NUM_DUMMIES && !m_aHasServerLockedTargets[Conn])
		CaptureServerLockedTargets();

	StoreInputByConn(Source, Conn);

	CNetObj_PlayerInput Neutral{};
	BuildNeutralInput(Neutral, Dummy, true);
	if(Size >= (int)sizeof(CNetObj_PlayerInput))
		mem_copy(pData, &Neutral, sizeof(Neutral));
}

int CFastPractice::WeaponFireSound(int Weapon)
{
	switch(Weapon)
	{
	case WEAPON_GUN: return SOUND_GUN_FIRE;
	case WEAPON_SHOTGUN: return SOUND_SHOTGUN_FIRE;
	case WEAPON_GRENADE: return SOUND_GRENADE_FIRE;
	case WEAPON_HAMMER: return SOUND_HAMMER_FIRE;
	case WEAPON_LASER: return SOUND_LASER_FIRE;
	case WEAPON_NINJA: return SOUND_NINJA_FIRE;
	default: return -1;
	}
}

void CFastPractice::TrackFireSound(int ClientId, CCharacter *pChar)
{
	if(!pChar || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const int AttackTick = pChar->GetAttackTick();
	if(AttackTick <= m_aLastAttackTick[ClientId])
		return;

	m_aLastAttackTick[ClientId] = AttackTick;

	if(!GameClient()->m_SuppressEvents && pChar->GetActiveWeapon() == WEAPON_HAMMER)
		MaybePlayHammerHitEffect(pChar);

	if(!g_Config.m_SndGame || GameClient()->m_SuppressEvents)
		return;

	const int SoundId = WeaponFireSound(pChar->GetActiveWeapon());
	if(SoundId < 0)
		return;
	if(SoundId == SOUND_GUN_FIRE && !g_Config.m_SndGun)
		return;

	GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SoundId, 1.0f, pChar->Core()->m_Pos);
}

void CFastPractice::MaybePlayHammerHitEffect(CCharacter *pChar)
{
	if(!pChar || pChar->GetActiveWeapon() != WEAPON_HAMMER)
		return;
	if(pChar->Core()->m_HammerHitDisabled)
		return;

	vec2 Direction = vec2((float)pChar->LatestInput()->m_TargetX, (float)pChar->LatestInput()->m_TargetY);
	if(length(Direction) < 0.001f)
		Direction = vec2((float)std::max(1, pChar->Core()->m_Direction), 0.0f);
	else
		Direction = normalize(Direction);

	const vec2 ProjStartPos = pChar->m_Pos + Direction * pChar->GetProximityRadius() * 0.75f;
	CEntity *apEnts[MAX_CLIENTS];
	const int Num = m_PracticeWorld.FindEntities(ProjStartPos, pChar->GetProximityRadius() * 0.5f, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; ++i)
	{
		auto *pTarget = static_cast<CCharacter *>(apEnts[i]);
		if(!pTarget || pTarget == pChar || !pChar->CanCollide(pTarget->GetCid()))
			continue;

		vec2 HitPos = ProjStartPos;
		if(length(pTarget->m_Pos - ProjStartPos) > 0.0f)
			HitPos = pTarget->m_Pos - normalize(pTarget->m_Pos - ProjStartPos) * pChar->GetProximityRadius() * 0.5f;

		GameClient()->m_Effects.HammerHit(HitPos, 1.0f, 1.0f);
		break;
	}
}

void CFastPractice::CaptureFrozenTargets()
{
	m_aFrozenTargetValid.fill(false);
	const int aIds[] = {GameClient()->m_Snap.m_LocalClientId, GameClient()->m_aLocalIds[0], GameClient()->m_aLocalIds[1], m_EnableLocalClientId, m_EnableDummyClientId};
	for(int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		const int Slot = (ClientId == GameClient()->m_aLocalIds[1]) ? 1 : 0;
		const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[Slot];
		int TargetX = Input.m_TargetX;
		int TargetY = Input.m_TargetY;
		if(TargetX == 0 && TargetY == 0)
		{
			TargetX = 1;
			TargetY = 0;
		}
		m_aFrozenTarget[ClientId] = ivec2(TargetX, TargetY);
		m_aFrozenTargetValid[ClientId] = true;
	}
}

void CFastPractice::ResetStoredInputs()
{
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		m_aNextStoredInput[Conn] = 0;
		m_aStoredInputCount[Conn] = 0;
		for(int i = 0; i < INPUT_HISTORY_SIZE; i++)
			m_aaStoredInputs[Conn][i] = {};
	}
}

void CFastPractice::StoreNeutralInput(bool Dummy, int Tick)
{
	CNetObj_PlayerInput Neutral{};
	BuildNeutralInput(Neutral, Dummy, true);
	const int Conn = g_Config.m_ClDummy ^ (int)Dummy;
	SStoredInput &Slot = m_aaStoredInputs[Conn][m_aNextStoredInput[Conn] % INPUT_HISTORY_SIZE];
	Slot.m_Input = Neutral;
	Slot.m_Tick = Tick;
	m_aNextStoredInput[Conn] = (m_aNextStoredInput[Conn] + 1) % INPUT_HISTORY_SIZE;
	m_aStoredInputCount[Conn] = std::min(m_aStoredInputCount[Conn] + 1, (int)INPUT_HISTORY_SIZE);
}

void CFastPractice::StoreInputByConn(const CNetObj_PlayerInput &Input, int Conn)
{
	if(Conn < 0 || Conn >= NUM_DUMMIES)
		return;
	const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
	if(m_aStoredInputCount[Conn] > 0)
	{
		const int PrevIdx = (m_aNextStoredInput[Conn] - 1 + INPUT_HISTORY_SIZE) % INPUT_HISTORY_SIZE;
		if(Tick == m_aaStoredInputs[Conn][PrevIdx].m_Tick)
		{
			m_aaStoredInputs[Conn][PrevIdx].m_Input = Input;
			return;
		}
		if(Tick < m_aaStoredInputs[Conn][PrevIdx].m_Tick)
		{
			m_aNextStoredInput[Conn] = 0;
			m_aStoredInputCount[Conn] = 0;
		}
	}
	SStoredInput &Slot = m_aaStoredInputs[Conn][m_aNextStoredInput[Conn] % INPUT_HISTORY_SIZE];
	Slot.m_Input = Input;
	Slot.m_Tick = Tick;
	m_aNextStoredInput[Conn] = (m_aNextStoredInput[Conn] + 1) % INPUT_HISTORY_SIZE;
	m_aStoredInputCount[Conn] = std::min(m_aStoredInputCount[Conn] + 1, (int)INPUT_HISTORY_SIZE);
}

const CNetObj_PlayerInput *CFastPractice::GetStoredInputByConn(int Tick, int Conn) const
{
	if(Conn < 0 || Conn >= NUM_DUMMIES)
		return nullptr;
	const int Count = m_aStoredInputCount[Conn];
	if(Count <= 0)
		return nullptr;

	// Ring buffer is monotonically non-decreasing in m_Tick; newest entry is at Next - 1 (O(1) fast path).
	for(int Step = 1; Step <= Count; Step++)
	{
		const int Idx = (m_aNextStoredInput[Conn] - Step + INPUT_HISTORY_SIZE) % INPUT_HISTORY_SIZE;
		const SStoredInput &Slot = m_aaStoredInputs[Conn][Idx];
		if(Slot.m_Tick >= 0 && Slot.m_Tick <= Tick)
			return &Slot.m_Input;
	}
	return nullptr;
}

void CFastPractice::BuildLiveInputByConn(CNetObj_PlayerInput &OutInput, int Conn) const
{
	const int SafeConn = (Conn == 1) ? 1 : 0;
	OutInput = GameClient()->m_Controls.m_aInputData[SafeConn];
	if(OutInput.m_TargetX == 0 && OutInput.m_TargetY == 0)
	{
		OutInput.m_TargetX = 1;
		OutInput.m_TargetY = 0;
	}
}

void CFastPractice::BuildLiveInput(CNetObj_PlayerInput &OutInput, bool Dummy) const
{
	const int Conn = Dummy ? (!g_Config.m_ClDummy) : g_Config.m_ClDummy;
	BuildLiveInputByConn(OutInput, Conn);
}

void CFastPractice::BuildNeutralInput(CNetObj_PlayerInput &OutInput, bool Dummy, bool UseFrozenTarget) const
{
	CNetObj_PlayerInput Source{};
	BuildLiveInput(Source, Dummy);
	OutInput = {};
	OutInput.m_Direction = 0;
	OutInput.m_Jump = 0;
	OutInput.m_Hook = 0;
	OutInput.m_WantedWeapon = 0;
	OutInput.m_PlayerFlags = PLAYERFLAG_PLAYING;

	const int Slot = g_Config.m_ClDummy ^ (int)Dummy;
	int ClientId = GameClient()->m_aLocalIds[Slot];
	if(!Dummy && ClientId < 0)
		ClientId = GameClient()->m_Snap.m_LocalClientId;

	if(Slot >= 0 && Slot < NUM_DUMMIES && m_aHasServerLockedTargets[Slot])
	{
		OutInput.m_Fire = m_aServerLockedFire[Slot];
		OutInput.m_NextWeapon = m_aServerLockedNextWeapon[Slot];
		OutInput.m_PrevWeapon = m_aServerLockedPrevWeapon[Slot];
		OutInput.m_TargetX = m_aServerLockedTargets[Slot].x;
		OutInput.m_TargetY = m_aServerLockedTargets[Slot].y;
	}
	else
	{
		OutInput.m_Fire = ReleasedFireState(Source.m_Fire);
		OutInput.m_NextWeapon = Source.m_NextWeapon & INPUT_STATE_MASK;
		OutInput.m_PrevWeapon = Source.m_PrevWeapon & INPUT_STATE_MASK;
		if(UseFrozenTarget && ClientId >= 0 && ClientId < MAX_CLIENTS && m_aFrozenTargetValid[ClientId])
		{
			OutInput.m_TargetX = m_aFrozenTarget[ClientId].x;
			OutInput.m_TargetY = m_aFrozenTarget[ClientId].y;
		}
		else
		{
			OutInput.m_TargetX = Source.m_TargetX;
			OutInput.m_TargetY = Source.m_TargetY;
		}
	}
	if(OutInput.m_TargetX == 0 && OutInput.m_TargetY == 0)
		OutInput.m_TargetX = 1;
}

void CFastPractice::FillRenderCharacter(CCharacter *pChar, CNetObj_Character &Out) const
{
	pChar->GetCore().Write(&Out);
	Out.m_Tick = pChar->GameWorld()->GameTick();
	Out.m_Weapon = pChar->GetActiveWeapon();
	Out.m_AttackTick = pChar->GetAttackTick();
	Out.m_PlayerFlags = PLAYERFLAG_PLAYING;

	const int ClientId = pChar->GetCid();
	int SnapEmote = EMOTE_NORMAL;
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		SnapEmote = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Emote;

	if(pChar->Core()->m_DeepFrozen)
		Out.m_Emote = EMOTE_PAIN;
	else if(pChar->m_FreezeTime > 0 || pChar->Core()->m_FreezeEnd != 0 || pChar->Core()->m_LiveFrozen)
		Out.m_Emote = EMOTE_BLINK;
	else
		Out.m_Emote = SnapEmote;
}

void CFastPractice::SeedPredictionHistory()
{
	const int BaseTick = m_PracticeWorld.GameTick();
	for(const int ClientId : {m_EnableLocalClientId, m_EnableDummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
		if(!pChar)
			continue;
		for(int i = 0; i < 200; i++)
		{
			const int Tick = BaseTick - i;
			if(Tick < 0)
				break;
			GameClient()->m_aClients[ClientId].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
			GameClient()->m_aClients[ClientId].m_aPredTick[Tick % 200] = Tick;
		}
	}
}

void CFastPractice::CachePredictedCore(int ClientId, const CCharacterCore &Core)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedPredicted[ClientId] = Core;
	m_aPublishedPredicted[ClientId].SetCoreWorld(nullptr, nullptr, nullptr);
	m_aPublishedValid[ClientId] = true;
}

void CFastPractice::CachePrevPredictedCore(int ClientId, const CCharacterCore &Core)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedPrevPredicted[ClientId] = Core;
	m_aPublishedPrevPredicted[ClientId].SetCoreWorld(nullptr, nullptr, nullptr);
}

void CFastPractice::CacheRegularPredictedCore(int ClientId, const CCharacterCore &Core)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedRegularPredicted[ClientId] = Core;
	m_aPublishedRegularPredicted[ClientId].SetCoreWorld(nullptr, nullptr, nullptr);
}

void CFastPractice::RepublishCachedCores() const
{
	const int ControlledId = ControlledPracticeId();
	const int aIds[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(const int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_aPublishedValid[ClientId])
			continue;
		CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		ClientData.m_Predicted = m_aPublishedPredicted[ClientId];
		ClientData.m_PrevPredicted = m_aPublishedPrevPredicted[ClientId];
		ClientData.m_RegularPredicted = m_aPublishedRegularPredicted[ClientId];
		ClientData.m_Solo = m_aPublishedPredicted[ClientId].m_Solo;
		GameClient()->m_Teams.SetSolo(ClientId, m_aPublishedPredicted[ClientId].m_Solo);
		if(ClientId == ControlledId)
		{
			GameClient()->m_PredictedChar = m_aPublishedPredicted[ClientId];
			GameClient()->m_PredictedPrevChar = m_aPublishedPrevPredicted[ClientId];
		}
	}
}

void CFastPractice::PublishParticipantCores(int LocalClientId, int DummyClientId)
{
	for(const int ClientId : {LocalClientId, DummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
		{
			CachePredictedCore(ClientId, pChar->GetCore());
			CachePrevPredictedCore(ClientId, pChar->GetCore());
			CacheRegularPredictedCore(ClientId, pChar->GetCore());
		}
	}
	RepublishCachedCores();
}

void CFastPractice::StorePredictionState(int Tick, int FinalTickRegular, int FinalTickOthers, int FinalTickSelf)
{
	(void)FinalTickOthers;
	const int aIds[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(const int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
		if(!pChar)
			continue;

		if(Tick == FinalTickSelf || Tick == FinalTickRegular)
			CachePredictedCore(ClientId, pChar->GetCore());
		if(Tick == FinalTickRegular)
			CacheRegularPredictedCore(ClientId, pChar->GetCore());

		GameClient()->m_aClients[ClientId].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
		GameClient()->m_aClients[ClientId].m_aPredTick[Tick % 200] = Tick;

		if(Tick > FinalTickRegular)
		{
			FillRenderCharacter(pChar, m_aFastRenderCur[ClientId]);
			m_aFastRenderValid[ClientId] = true;
		}
	}
	RepublishCachedCores();
}

bool CFastPractice::GetFastInputRenderCharacter(int ClientId, CNetObj_Character &Prev, CNetObj_Character &Cur) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_aFastRenderValid[ClientId])
		return false;
	Prev = m_aFastRenderPrev[ClientId];
	Cur = m_aFastRenderCur[ClientId];
	return true;
}

bool CFastPractice::IsDeathTile(const CCharacter *pChar) const
{
	if(!pChar || !Collision())
		return false;
	const vec2 Pos = pChar->Core()->m_Pos;
	if(pChar->IsSuper() || pChar->Core()->m_Invincible)
		return false;
	if(const_cast<CCharacter *>(pChar)->GameLayerClipped(Pos))
		return true;

	const float Radius = pChar->GetProximityRadius() / 3.0f;
	static constexpr vec2 s_aOffsets[] = {
		{1.0f, -1.0f},
		{1.0f, 1.0f},
		{-1.0f, -1.0f},
		{-1.0f, 1.0f},
	};
	for(const vec2 Offset : s_aOffsets)
	{
		const int Corner = Collision()->GetPureMapIndex(Pos + Offset * Radius);
		if(Corner >= 0 && (Collision()->GetTileIndex(Corner) == TILE_DEATH || Collision()->GetFrontTileIndex(Corner) == TILE_DEATH))
			return true;
	}
	return false;
}

void CFastPractice::ResetCharacterToSaved(int ClientId, CCharacter *pChar, int Tick)
{
	if(!pChar || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	vec2 SavedPos = pChar->Core()->m_Pos;
	if(m_aSafePosValid[ClientId])
		SavedPos = m_aSafePos[ClientId];
	else if(m_aPracticeCommandState[ClientId].m_HasRescueAuto)
		SavedPos = m_aPracticeCommandState[ClientId].m_RescueAutoPos;
	else if(m_aSpawnPosValid[ClientId])
		SavedPos = m_aSpawnPos[ClientId];

	StoreLastDeathPosition(ClientId, pChar->Core()->m_Pos);
	if(!GameClient()->m_SuppressEvents)
		GameClient()->m_Effects.PlayerSpawn(pChar->Core()->m_Pos, 1.0f, 0.6f);
	TeleportCharacter(pChar, SavedPos);
	(void)Tick;
}

void CFastPractice::PlayCoreEvents(CCharacter *pChar, int Tick)
{
	if(!pChar)
		return;
	const int ClientId = pChar->GetCid();
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(Tick <= m_aLastEventTick[ClientId])
		return;
	m_aLastEventTick[ClientId] = Tick;

	const vec2 Pos = pChar->Core()->m_Pos;
	const int Events = pChar->Core()->m_TriggeredEvents;
	if(!GameClient()->m_SuppressEvents && (Events & COREEVENT_AIR_JUMP))
		GameClient()->m_Effects.AirJump(Pos, 1.0f, 1.0f);
	if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
	{
		if(Events & COREEVENT_GROUND_JUMP)
			GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, Pos);
		if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
			GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_PLAYER, 1.0f, Pos);
		if(Events & COREEVENT_HOOK_ATTACH_GROUND)
			GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1.0f, Pos);
		if(Events & COREEVENT_HOOK_HIT_NOHOOK)
			GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1.0f, Pos);
	}
}

void CFastPractice::CollectTrackedProjectiles(CGameWorld &World, int LocalClientId, int DummyClientId, std::vector<STrackedProjectile> &vOut) const
{
	vOut.clear();
	for(auto *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = (CProjectile *)pProj->TypeNext())
	{
		const CProjectileData Data = pProj->GetData();
		if(!IsTrackedExplosive(Data, LocalClientId, DummyClientId))
			continue;

		STrackedProjectile Proj;
		Proj.m_Owner = Data.m_Owner;
		Proj.m_StartTick = Data.m_StartTick;
		Proj.m_Type = Data.m_Type;
		Proj.m_TuneZone = Data.m_TuneZone;
		Proj.m_StartPos = Data.m_StartPos;
		Proj.m_StartVel = Data.m_StartVel;
		vOut.push_back(Proj);
	}
}

void CFastPractice::ShiftPracticeTickBase(int Delta)
{
	if(Delta == 0)
		return;

	auto ShiftWorld = [&](CGameWorld &World) {
		World.m_GameTick += Delta;
		for(CCharacter *pChar = (CCharacter *)World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChar; pChar = (CCharacter *)pChar->TypeNext())
		{
			pChar->ShiftTickBase(Delta);
			CCharacterCore Core = pChar->GetCore();
			if(Core.m_FreezeStart > 0)
				Core.m_FreezeStart += Delta;
			if(Core.m_FreezeEnd > 0)
				Core.m_FreezeEnd += Delta;
			pChar->SetCore(Core);
		}
		for(CProjectile *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = (CProjectile *)pProj->TypeNext())
			pProj->ShiftStartTick(Delta);
		for(CLaser *pLaser = (CLaser *)World.FindFirst(CGameWorld::ENTTYPE_LASER); pLaser; pLaser = (CLaser *)pLaser->TypeNext())
			pLaser->ShiftEvalTick(Delta);
		for(CPlasma *pPlasma = (CPlasma *)World.FindFirst(CGameWorld::ENTTYPE_PLASMA); pPlasma; pPlasma = (CPlasma *)pPlasma->TypeNext())
			pPlasma->ShiftEvalTick(Delta);
		for(SSwitchers &Switcher : World.Switchers())
		{
			for(int t = 0; t < NUM_DDRACE_TEAMS; t++)
			{
				if(Switcher.m_aEndTick[t] > 0)
					Switcher.m_aEndTick[t] += Delta;
				if(Switcher.m_aLastUpdateTick[t] > 0)
					Switcher.m_aLastUpdateTick[t] += Delta;
			}
		}
	};

	ShiftWorld(m_PracticeWorld);
	ShiftWorld(m_PracticePrevWorld);

	for(const int ClientId : {m_EnableLocalClientId, m_EnableDummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(m_aLastAttackTick[ClientId] > 0)
			m_aLastAttackTick[ClientId] += Delta;
		if(m_aLastEventTick[ClientId] > 0)
			m_aLastEventTick[ClientId] += Delta;
		if(m_aPracticeRaceState[ClientId].m_StartTick >= 0)
			m_aPracticeRaceState[ClientId].m_StartTick = std::max(0, m_aPracticeRaceState[ClientId].m_StartTick + Delta);
	}
	if(m_LastFreezeSkinDebounceTick > 0)
		m_LastFreezeSkinDebounceTick += Delta;

	auto ShiftSaveSlot = [&](SPracticeSaveSlot &Slot) {
		if(!Slot.m_Valid)
			return;
		Slot.m_SavedTick += Delta;
	};
	ShiftSaveSlot(m_DefaultSave);
	for(auto &Slot : m_vNamedSaves)
		ShiftSaveSlot(Slot);

	SeedPredictionHistory();
}

void CFastPractice::TickPracticeWorld()
{
	if(!Active())
		return;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return;
	}

	if(IsRealSpectatorTeam(LocalClientId))
	{
		Disable();
		return;
	}

	CCharacter *pLocalChar = m_PracticeWorld.GetCharacterById(LocalClientId);
	if(!pLocalChar)
	{
		m_NeedsRebuild = true;
		return;
	}

	CCharacter *pDummyChar = (m_RequireDummy && DummyClientId >= 0) ? m_PracticeWorld.GetCharacterById(DummyClientId) : nullptr;
	if(m_RequireDummy && !pDummyChar)
	{
		m_NeedsRebuild = true;
		return;
	}

	const float FastInputOffsetTicks = AcInputs::EffectiveOffsetTicks();
	const int FastInputTicks = AcInputs::PredictionTicks(FastInputOffsetTicks);
	const int FastInputOthersTicks = AcInputs::AnyOthers() ? AcInputs::PredictionTicksOthers(FastInputOffsetTicks) : 0;
	const int FinalTickRegular = Client()->PredGameTick(g_Config.m_ClDummy);

	// Small backward PredGameTick jitter must not wipe the practice session
	if(m_PracticeWorld.GameTick() > FinalTickRegular)
	{
		if(m_PracticeWorld.GameTick() - FinalTickRegular > Client()->GameTickSpeed() * 3)
		{
			m_NeedsRebuild = true;
			return;
		}
		RepublishCachedCores();
		return;
	}

	if(FinalTickRegular - m_PracticeWorld.GameTick() > Client()->GameTickSpeed() * 3)
	{
		ShiftPracticeTickBase((FinalTickRegular - 1) - m_PracticeWorld.GameTick());
	}

	const int FinalTickSelf = FinalTickRegular + FastInputTicks;
	const int FinalTickOthers = FinalTickRegular + FastInputOthersTicks;
	const bool FastInputOverrun = FinalTickSelf > FinalTickRegular;

	if(FastInputOverrun)
		std::fill(std::begin(m_aFastRenderValid), std::end(m_aFastRenderValid), false);

	const int LocalConn = ConnectionForClientId(LocalClientId);
	const int DummyConn = LocalConn ^ 1;
	const int BaseGameTick = m_PracticeWorld.GameTick();

	bool HasRegularWorld = false;

	SyncPracticeWorldConfig(m_PracticeWorld);
	if(FastInputOverrun && BaseGameTick == FinalTickRegular)
	{
		StorePredictionState(FinalTickRegular, FinalTickRegular, FinalTickOthers, FinalTickSelf);
		CopyDynamicWorld(m_FastOverrunWorld, m_PracticeWorld, false);
		SyncPracticeWorldConfig(m_FastOverrunWorld);
		HasRegularWorld = true;
	}

	const int aParticipantIds[] = {LocalClientId, DummyClientId};
	const bool TrackExplosives = !GameClient()->m_SuppressEvents || g_Config.m_SndGame;

	for(int Tick = BaseGameTick + 1; Tick <= FinalTickSelf; Tick++)
	{
		pLocalChar = m_PracticeWorld.GetCharacterById(LocalClientId);
		pDummyChar = (m_RequireDummy && DummyClientId >= 0) ? m_PracticeWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		{
			Disable();
			return;
		}

		if(Tick == FinalTickRegular)
		{
			SyncPracticePrevWorld();
			for(const int ClientId : aParticipantIds)
			{
				if(ClientId < 0 || ClientId >= MAX_CLIENTS)
					continue;
				if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
				{
					CachePrevPredictedCore(ClientId, pChar->GetCore());
					FillRenderCharacter(pChar, m_aFastRenderPrev[ClientId]);
				}
			}
		}

		if(Tick > FinalTickRegular)
		{
			for(const int ClientId : aParticipantIds)
			{
				if(ClientId < 0 || ClientId >= MAX_CLIENTS)
					continue;
				if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
				{
					CachePrevPredictedCore(ClientId, pChar->GetCore());
					FillRenderCharacter(pChar, m_aFastRenderPrev[ClientId]);
				}
			}
		}

		const CNetObj_PlayerInput *pInputData = GetStoredInputByConn(Tick, LocalConn);
		const CNetObj_PlayerInput *pDummyInputData = !pDummyChar ? nullptr : GetStoredInputByConn(Tick, DummyConn);
		CNetObj_PlayerInput LiveInput{};
		CNetObj_PlayerInput LiveDummyInput{};
		CNetObj_PlayerInput LocalNeutralizedInput{};
		CNetObj_PlayerInput DummyNeutralizedInput{};

		if(!pInputData)
		{
			BuildLiveInputByConn(LiveInput, LocalConn);
			pInputData = &LiveInput;
		}
		if(pDummyChar && !pDummyInputData)
		{
			BuildLiveInputByConn(LiveDummyInput, DummyConn);
			pDummyInputData = &LiveDummyInput;
		}

		if(FastInputTicks > 0 && Tick > FinalTickRegular)
			pInputData = &GameClient()->m_Controls.m_aFastInput[LocalConn];

		const bool RegularTick = Tick <= FinalTickRegular;
		const bool SuppressTransitionTick = Tick == BaseGameTick + 1 && (m_SuppressFireOnNextPredictTick || GameClient()->m_IsDummySwapping);
		const bool SuppressCooldownTick = m_InputSuppressTicks > 0;
		if(SuppressTransitionTick || SuppressCooldownTick)
		{
			if(pInputData)
			{
				LocalNeutralizedInput = *pInputData;
				LocalNeutralizedInput.m_Fire = ReleasedFireState(pLocalChar->LatestInput()->m_Fire);
				LocalNeutralizedInput.m_WantedWeapon = 0;
				LocalNeutralizedInput.m_NextWeapon = 0;
				LocalNeutralizedInput.m_PrevWeapon = 0;
				pInputData = &LocalNeutralizedInput;
			}
			if(pDummyInputData)
			{
				DummyNeutralizedInput = *pDummyInputData;
				DummyNeutralizedInput.m_Fire = ReleasedFireState(pDummyChar->LatestInput()->m_Fire);
				DummyNeutralizedInput.m_WantedWeapon = 0;
				DummyNeutralizedInput.m_NextWeapon = 0;
				DummyNeutralizedInput.m_PrevWeapon = 0;
				pDummyInputData = &DummyNeutralizedInput;
			}
			if(RegularTick)
			{
				if(m_InputSuppressTicks > 0)
					m_InputSuppressTicks--;
				m_SuppressFireOnNextPredictTick = false;
			}
		}

		if(pDummyChar && g_Config.m_ClDummyHammer)
		{
			DummyNeutralizedInput = pDummyInputData ? *pDummyInputData : CNetObj_PlayerInput{};
			pDummyInputData = &DummyNeutralizedInput;
			const vec2 Dir = pLocalChar->Core()->m_Pos - pDummyChar->Core()->m_Pos;
			DummyNeutralizedInput.m_TargetX = (int)Dir.x;
			DummyNeutralizedInput.m_TargetY = (int)Dir.y;
			if(DummyNeutralizedInput.m_TargetX == 0 && DummyNeutralizedInput.m_TargetY == 0)
				DummyNeutralizedInput.m_TargetY = -1;
		}

		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar && pDummyChar->GetCid() < pLocalChar->GetCid();
		const bool PredictEvents = m_PracticeWorld.m_WorldConfig.m_PredictEvents;
		if(!RegularTick)
			m_PracticeWorld.m_WorldConfig.m_PredictEvents = false;

		pLocalChar->m_CanMoveInFreeze = false;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;
		if(g_Config.m_ClPredictFreeze == 2 && FinalTickRegular - 1 - FinalTickRegular % 2 <= Tick)
			pLocalChar->m_CanMoveInFreeze = true;

		if(RegularTick && TrackExplosives)
			CollectTrackedProjectiles(m_PracticeWorld, LocalClientId, DummyClientId, m_vTrackedExplosiveBefore);

		if(DummyFirst && pDummyInputData)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		m_PracticeWorld.m_GameTick = Tick;
		if(RegularTick)
		{
			UpdatePracticeRaceState(LocalClientId, pLocalChar, Tick);
			if(pDummyChar)
				UpdatePracticeRaceState(DummyClientId, pDummyChar, Tick);
		}
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);

		ApplyStaticPickups(m_PracticeWorld, pLocalChar);
		if(pDummyChar)
			ApplyStaticPickups(m_PracticeWorld, pDummyChar);

		m_PracticeWorld.Tick();
		m_PracticeWorld.m_WorldConfig.m_PredictEvents = PredictEvents;

		if(Tick == FinalTickRegular)
		{
			for(auto *pPrevProj = (CProjectile *)m_PracticePrevWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pPrevProj; pPrevProj = (CProjectile *)pPrevProj->TypeNext())
			{
				if(pPrevProj->m_DestroyTick >= 0)
					continue;
				const CProjectileData PrevData = pPrevProj->GetData();
				bool Found = false;
				for(auto *pCurProj = (CProjectile *)m_PracticeWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pCurProj; pCurProj = (CProjectile *)pCurProj->TypeNext())
				{
					const CProjectileData CurData = pCurProj->GetData();
					if(SameProjectile(
						   STrackedProjectile{PrevData.m_Owner, PrevData.m_StartTick, PrevData.m_Type, PrevData.m_TuneZone, PrevData.m_StartPos, PrevData.m_StartVel},
						   STrackedProjectile{CurData.m_Owner, CurData.m_StartTick, CurData.m_Type, CurData.m_TuneZone, CurData.m_StartPos, CurData.m_StartVel}))
					{
						Found = true;
						break;
					}
				}
				if(!Found)
					pPrevProj->m_DestroyTick = Tick;
			}
		}

		if(RegularTick)
		{
			TrackSafeRescuePosition(LocalClientId, pLocalChar);
			if(pDummyChar)
				TrackSafeRescuePosition(DummyClientId, pDummyChar);
			if(IsSafeRescuePosition(pLocalChar->Core()->m_Pos, pLocalChar->GetProximityRadius()))
			{
				m_aSafePos[LocalClientId] = pLocalChar->Core()->m_Pos;
				m_aSafePosValid[LocalClientId] = true;
			}
			if(pDummyChar && IsSafeRescuePosition(pDummyChar->Core()->m_Pos, pDummyChar->GetProximityRadius()))
			{
				m_aSafePos[DummyClientId] = pDummyChar->Core()->m_Pos;
				m_aSafePosValid[DummyClientId] = true;
			}

			PlayCoreEvents(pLocalChar, Tick);
			PlayCoreEvents(pDummyChar, Tick);

			auto ResetIfDead = [&](int ClientId, CCharacter *pChar) {
				if(!pChar || !IsDeathTile(pChar))
					return;
				ResetCharacterToSaved(ClientId, pChar, Tick);
			};
			ResetIfDead(LocalClientId, pLocalChar);
			if(pDummyChar)
				ResetIfDead(DummyClientId, pDummyChar);
			pLocalChar = m_PracticeWorld.GetCharacterById(LocalClientId);
			pDummyChar = (m_RequireDummy && DummyClientId >= 0) ? m_PracticeWorld.GetCharacterById(DummyClientId) : nullptr;

			if(TrackExplosives && !m_vTrackedExplosiveBefore.empty())
			{
				CollectTrackedProjectiles(m_PracticeWorld, LocalClientId, DummyClientId, m_vTrackedExplosiveAfter);
				for(const auto &TrackedProj : m_vTrackedExplosiveBefore)
				{
					const bool StillExists = std::any_of(m_vTrackedExplosiveAfter.begin(), m_vTrackedExplosiveAfter.end(), [&](const STrackedProjectile &Candidate) {
						return SameProjectile(TrackedProj, Candidate);
					});
					if(StillExists)
						continue;
					const int TickSpeed = Client()->GameTickSpeed();
					const int TuneZone = std::clamp(TrackedProj.m_TuneZone, 0, TuneZone::NUM - 1);
					const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);
					const vec2 PrevPos = CalcTrackedProjectilePos(TrackedProj, Tick - 1, TickSpeed, pTuning);
					const vec2 CurPos = CalcTrackedProjectilePos(TrackedProj, Tick, TickSpeed, pTuning);
					vec2 ImpactPos = CurPos;
					Collision()->IntersectLine(PrevPos, CurPos, &ImpactPos, nullptr);
					if(!GameClient()->m_SuppressEvents)
						GameClient()->m_Effects.Explosion(ImpactPos, 1.0f);
					if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
						GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_GRENADE_EXPLODE, 1.0f, ImpactPos);
				}
			}

			TrackFireSound(LocalClientId, pLocalChar);
			if(pDummyChar)
				TrackFireSound(DummyClientId, pDummyChar);
		}

		StorePredictionState(Tick, FinalTickRegular, FinalTickOthers, FinalTickSelf);

		if(RegularTick && Tick == FinalTickRegular)
		{
			CopyDynamicWorld(m_FastOverrunWorld, m_PracticeWorld, false);
			SyncPracticeWorldConfig(m_FastOverrunWorld);
			HasRegularWorld = true;
		}

		if(RegularTick && Tick > GameClient()->m_aLastNewPredictedTick[LocalConn])
		{
			GameClient()->m_aLastNewPredictedTick[LocalConn] = Tick;
			GameClient()->m_NewPredictedTick = true;
		}
		if(RegularTick && pDummyChar && Tick > GameClient()->m_aLastNewPredictedTick[DummyConn])
			GameClient()->m_aLastNewPredictedTick[DummyConn] = Tick;
	}

	if(FastInputOverrun && HasRegularWorld)
	{
		CopyDynamicWorld(m_PracticeWorld, m_FastOverrunWorld, false);
		SyncPracticeWorldConfig(m_PracticeWorld);
	}
	if(!FastInputOverrun)
		std::fill(std::begin(m_aFastRenderValid), std::end(m_aFastRenderValid), false);

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	GameClient()->m_PredictedTick = FinalTickRegular;
	if(GameClient()->m_NewPredictedTick)
		GameClient()->m_Ghost.OnNewPredictedSnapshot();
	m_LastResolvedLocalClientId = LocalClientId;
	m_LastResolvedDummyClientId = DummyClientId;
}

void CFastPractice::SyncFromPrediction()
{
	if(!m_Enabled)
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return;
	}

	if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		Disable();
		return;
	}

	if(m_RequireDummy && (!Client()->DummyConnected() || m_EnableDummyClientId < 0 || !GameClient()->m_aClients[m_EnableDummyClientId].m_Active))
		DropPracticeDummy();

	if(m_LastClDummy != g_Config.m_ClDummy)
	{
		m_LastClDummy = g_Config.m_ClDummy;
		m_SuppressFireOnNextPredictTick = true;
		m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
		ReleaseBufferedInputState();
	}

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return;
	}

	if(IsRealSpectatorTeam(LocalClientId))
	{
		Disable();
		return;
	}

	if(m_NeedsRebuild || !m_Ready)
	{
		if(!Rebuild())
		{
			Disable();
			return;
		}
	}

	auto &Pending = m_aPracticeCommandState[LocalClientId];
	const bool HasPendingTeleport = Pending.m_HasPendingTeleport;
	const vec2 PendingTeleportPos = Pending.m_PendingTeleportPos;
	auto ApplyPendingTeleport = [&]() {
		if(!HasPendingTeleport)
			return;
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(LocalClientId))
		{
			ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(PendingTeleportPos));
			FinishMutation(LocalClientId, DummyClientId, pChar, false);
		}
	};

	ApplyPendingTeleport();

	TickPracticeWorld();
	if(m_NeedsRebuild && m_Enabled)
	{
		if(!Rebuild())
		{
			Disable();
			return;
		}
		ApplyPendingTeleport();
		TickPracticeWorld();
	}
	if(!m_Enabled)
		return;

	Pending.m_HasPendingTeleport = false;
	RepublishCachedCores();
	UpdateFreezeSkinDebounce();
	UpdateGhostData();

	if(IsOriginalSpecPaused(LocalClientId))
		ApplySpecPracticeView(LocalClientId);
	else
		RestoreSpecPracticeView();
}

void CFastPractice::UpdateGhostForClientId(int ClientId, SGhostData &Ghost, const SAnchorData &FallbackAnchor)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		Ghost = SGhostData{};
		return;
	}

	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		const CNetObj_Character &Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		Ghost.m_Valid = true;
		Ghost.m_ClientId = ClientId;
		Ghost.m_Pos = vec2((float)Char.m_X, (float)Char.m_Y);
		Ghost.m_Direction = vec2(Char.m_Direction == 0 ? 1.0f : (float)Char.m_Direction, 0.0f);
		Ghost.m_Angle = Char.m_Angle;
		Ghost.m_Weapon = Char.m_Weapon;
		Ghost.m_HookState = Char.m_HookState;
		Ghost.m_HookPos = vec2((float)Char.m_HookX, (float)Char.m_HookY);
		return;
	}

	// Keep showing the anchor ghost when the original tee is in /spec or /pause
	if(m_Enabled && IsOriginalSpecPaused(ClientId) && FallbackAnchor.m_Valid && FallbackAnchor.m_ClientId == ClientId)
	{
		const CNetObj_Character &Char = FallbackAnchor.m_Char;
		Ghost.m_Valid = true;
		Ghost.m_ClientId = ClientId;
		Ghost.m_Pos = vec2((float)Char.m_X, (float)Char.m_Y);
		Ghost.m_Direction = vec2(Char.m_Direction == 0 ? 1.0f : (float)Char.m_Direction, 0.0f);
		Ghost.m_Angle = Char.m_Angle;
		Ghost.m_Weapon = Char.m_Weapon;
		Ghost.m_HookState = Char.m_HookState;
		Ghost.m_HookPos = vec2((float)Char.m_HookX, (float)Char.m_HookY);
		return;
	}

	Ghost = SGhostData{};
}

void CFastPractice::UpdateGhostData()
{
	if(m_Enabled)
	{
		const SAnchorData &LocalGhostAnchor = (m_MainAnchor.m_ClientId == m_EnableLocalClientId) ? m_MainAnchor : m_DummyAnchor;
		const SAnchorData &DummyGhostAnchor = (m_DummyAnchor.m_ClientId == m_EnableDummyClientId) ? m_DummyAnchor : m_MainAnchor;
		UpdateGhostForClientId(m_EnableLocalClientId, m_MainGhost, LocalGhostAnchor);
		UpdateGhostForClientId(m_EnableDummyClientId, m_DummyGhost, DummyGhostAnchor);
	}
	else
	{
		UpdateGhostForClientId(GameClient()->m_aLocalIds[0], m_MainGhost, SAnchorData{});
		UpdateGhostForClientId(GameClient()->m_aLocalIds[1], m_DummyGhost, SAnchorData{});
	}
}

void CFastPractice::OnNewSnapshot()
{
	UpdateGhostData();

	if(!m_Enabled)
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return;
	}

	if(GameClient()->m_Snap.m_pGameInfoObj)
	{
		if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
		{
			Disable();
			return;
		}
		const int RoundStartTick = GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick;
		if(m_LastRoundStartTick >= 0 && RoundStartTick != m_LastRoundStartTick)
			m_NeedsRebuild = true;
		m_LastRoundStartTick = RoundStartTick;
	}

	if(m_RequireDummy && (!Client()->DummyConnected() || m_EnableDummyClientId < 0 || !GameClient()->m_aClients[m_EnableDummyClientId].m_Active))
		DropPracticeDummy();

	const int ControlledId = ControlledPracticeId();
	if(ControlledId < 0 || IsRealSpectatorTeam(ControlledId))
	{
		Disable();
		return;
	}

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	RepublishCachedCores();

	if(IsOriginalSpecPaused(ControlledId))
		ApplySpecPracticeView(ControlledId);
	else
		RestoreSpecPracticeView();
}

void CFastPractice::RenderGhost(const SGhostData &Ghost, const vec2 &RenderPos, float Alpha) const
{
	if(!Ghost.m_Valid || Ghost.m_ClientId < 0 || Ghost.m_ClientId >= MAX_CLIENTS)
		return;
	if(!GameClient()->m_aClients[Ghost.m_ClientId].m_Active)
		return;

	CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Ghost.m_ClientId].m_RenderInfo;
	TeeInfo.m_Size = 64.0f;
	TeeInfo.m_TeeRenderFlags = 0;

	const float Angle = Ghost.m_Angle / 256.0f;
	vec2 Dir = direction(Angle);
	if(length(Dir) < 0.001f)
		Dir = vec2(1.0f, 0.0f);

	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, Dir, RenderPos, Alpha);
}

void CFastPractice::OnRender()
{
	if(!Active() || Client()->State() != IClient::STATE_ONLINE)
		return;
	if(g_Config.m_AcFastPracticeGhostAlpha <= 0)
		return;

	const float GhostAlpha = std::clamp(g_Config.m_AcFastPracticeGhostAlpha / 100.0f, 0.0f, 1.0f);
	if(GhostAlpha <= 0.0f)
		return;

	const CScreenRect PrevScreen = Graphics()->GetScreen();
	const vec2 Center = GameClient()->m_Camera.m_Center;
	const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
		Center.x, Center.y,
		100.0f, 100.0f, 100.0f,
		0.0f, 0.0f,
		Graphics()->ScreenAspect(),
		GameClient()->m_Camera.m_Zoom);
	CScreenRect CullRect = WorldScreen;
	CullRect.Expand(100.0f);
	Graphics()->MapScreen(WorldScreen);

	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	const auto RenderRealMarker = [&](const SGhostData &Ghost, float Alpha) {
		if(!Ghost.m_Valid || Ghost.m_ClientId < 0 || Ghost.m_ClientId >= MAX_CLIENTS)
			return;
		vec2 RenderPos = Ghost.m_Pos;
		if(GameClient()->m_Snap.m_aCharacters[Ghost.m_ClientId].m_Active)
		{
			const CNetObj_Character &Prev = GameClient()->m_Snap.m_aCharacters[Ghost.m_ClientId].m_Prev;
			const CNetObj_Character &Cur = GameClient()->m_Snap.m_aCharacters[Ghost.m_ClientId].m_Cur;
			RenderPos = mix(vec2((float)Prev.m_X, (float)Prev.m_Y), vec2((float)Cur.m_X, (float)Cur.m_Y), IntraTick);
		}
		if(CCharacter *pPracticeChar = m_PracticeWorld.GetCharacterById(Ghost.m_ClientId))
		{
			if(distance(pPracticeChar->Core()->m_Pos, RenderPos) < 10.0f)
				return;
		}
		if(!CullRect.Inside(RenderPos))
			return;
		RenderGhost(Ghost, RenderPos, Alpha);
	};

	RenderRealMarker(m_MainGhost, GhostAlpha);
	RenderRealMarker(m_DummyGhost, GhostAlpha * 0.8f);
	Graphics()->MapScreen(PrevScreen);
}

void CFastPractice::EchoPractice(const char *pFormat, ...) const
{
	char aBody[512];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aBody, sizeof(aBody), Localize(pFormat), Args);
	va_end(Args);

	char aMsg[576];
	str_format(aMsg, sizeof(aMsg), "[AClient] practice: %s", aBody);
	GameClient()->Echo(aMsg);
}

bool CFastPractice::ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs)
{
	vArgs.clear();
	if(!pLine)
		return false;

	const char *p = pLine;
	while(*p)
	{
		while(*p == ' ' || *p == '\t')
			++p;
		if(*p == '\0')
			break;

		std::string Token;
		bool InQuotes = false;
		if(*p == '"')
		{
			InQuotes = true;
			++p;
		}

		while(*p)
		{
			if(InQuotes)
			{
				if(*p == '"')
				{
					++p;
					break;
				}
				if(*p == '\\' && p[1] == '"')
				{
					Token.push_back('"');
					p += 2;
					continue;
				}
				Token.push_back(*p++);
				continue;
			}

			if(*p == ' ' || *p == '\t')
				break;
			Token.push_back(*p++);
		}

		if(!Token.empty())
			vArgs.push_back(std::move(Token));

		while(*p == ' ' || *p == '\t')
			++p;
	}

	return !vArgs.empty();
}

std::string CFastPractice::JoinCommandArgs(const std::vector<std::string> &vArgs, size_t StartIndex)
{
	std::string Out;
	for(size_t i = StartIndex; i < vArgs.size(); i++)
	{
		if(i > StartIndex)
			Out.push_back(' ');
		Out.append(vArgs[i]);
	}
	return Out;
}

bool CFastPractice::ParseCoordinateToken(const char *pToken, float Base, float &Out)
{
	if(!pToken || pToken[0] == '\0')
		return false;

	const bool Relative = pToken[0] == '~';
	const char *pValue = Relative ? pToken + 1 : pToken;
	float Parsed = 0.0f;
	if(pValue[0] != '\0' && !str_tofloat(pValue, &Parsed))
		return false;

	if(std::isnan(Parsed) || std::isinf(Parsed))
		return false;

	const float Computed = (Relative ? Base : 0.0f) + Parsed * 32.0f;
	if(!std::isfinite(Computed))
		return false;

	Out = Computed;
	return true;
}

int CFastPractice::FindClientByName(const char *pName) const
{
	if(!pName || pName[0] == '\0')
		return -1;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active)
			continue;
		if(str_comp(GameClient()->m_aClients[ClientId].m_aName, pName) == 0)
			return ClientId;
	}

	return -1;
}

void CFastPractice::NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const
{
	if(!pChar)
		return;

	const int ClientId = pChar->GetCid();
	if(pChar->GameWorld())
		pChar->GameWorld()->ReleaseHooked(ClientId);

	CCharacterCore Core = pChar->GetCore();
	Core.m_Vel = vec2(0.0f, 0.0f);
	Core.m_Jumped = 0;
	Core.m_JumpedTotal = 0;
	Core.m_HookState = HOOK_RETRACTED;
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookDir = vec2(0.0f, 0.0f);
	Core.m_HookTick = 0;
	Core.m_NewHook = false;
	Core.SetHookedPlayer(-1);
	Core.m_AttachedPlayers.clear();
	if(!KeepFreezeFlags)
	{
		Core.m_DeepFrozen = false;
		Core.m_LiveFrozen = false;
		Core.m_IsInFreeze = false;
		Core.m_FreezeEnd = 0;
	}
	pChar->SetCore(Core);
	pChar->m_Pos = Core.m_Pos;
	pChar->m_PrevPos = Core.m_Pos;
	pChar->m_PrevPrevPos = Core.m_Pos;
	pChar->ResetHook();
	pChar->ResetVelocity();
	if(!KeepFreezeFlags)
	{
		pChar->Unfreeze();
		pChar->m_FreezeTime = 0;
	}
	pChar->m_CanMoveInFreeze = false;
	pChar->m_FrozenLastTick = false;

	CNetObj_PlayerInput NeutralInput = {};
	BuildLiveInputByConn(NeutralInput, ConnectionForClientId(ClientId));
	NeutralizeInput(NeutralInput);
	NeutralInput.m_Fire = ReleasedFireState(NeutralInput.m_Fire);
	pChar->SetInput(&NeutralInput);
	pChar->ResetInput();
}

void CFastPractice::NormalizeWeaponSelectionInput(CCharacter *pChar) const
{
	if(!pChar)
		return;

	CNetObj_PlayerInput Input = *pChar->LatestInput();
	Input.m_WantedWeapon = 0;
	Input.m_NextWeapon = 0;
	Input.m_PrevWeapon = 0;
	pChar->SetInput(&Input);
}

void CFastPractice::TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	Core.m_Pos = Pos;
	pChar->SetCore(Core);
	pChar->m_Pos = Pos;
	pChar->m_PrevPos = Pos;
	pChar->m_PrevPrevPos = Pos;
	NormalizeCharacterAfterReset(pChar, false);
}

void CFastPractice::SaveTeleportHistory(int ClientId, const vec2 &CurrentPos, const vec2 &TargetPos)
{
	StoreLastTeleport(ClientId, TargetPos);
	StoreLastDeathPosition(ClientId, CurrentPos);
}

void CFastPractice::ApplyPracticeTeleport(int ClientId, CCharacter *pChar, const vec2 &TargetPos)
{
	if(!pChar)
		return;

	SaveTeleportHistory(ClientId, pChar->Core()->m_Pos, TargetPos);
	TeleportCharacter(pChar, TargetPos);
}

void CFastPractice::GivePracticeWeapon(CCharacter *pChar, int Weapon, bool Remove) const
{
	if(!pChar)
		return;

	if(Weapon == -1)
	{
		pChar->GiveWeapon(WEAPON_SHOTGUN, Remove);
		pChar->GiveWeapon(WEAPON_GRENADE, Remove);
		pChar->GiveWeapon(WEAPON_LASER, Remove);
	}
	else
	{
		pChar->GiveWeapon(Weapon, Remove);
	}
}

void CFastPractice::EnsureActiveWeaponIsValid(CCharacter *pChar) const
{
	if(!pChar)
		return;

	const CCharacterCore Core = pChar->GetCore();
	if(Core.m_ActiveWeapon >= WEAPON_HAMMER && Core.m_ActiveWeapon < NUM_WEAPONS && Core.m_aWeapons[Core.m_ActiveWeapon].m_Got)
		return;

	if(Core.m_aWeapons[WEAPON_HAMMER].m_Got)
	{
		pChar->SetActiveWeapon(WEAPON_HAMMER);
		return;
	}
	if(Core.m_aWeapons[WEAPON_GUN].m_Got)
	{
		pChar->SetActiveWeapon(WEAPON_GUN);
		return;
	}
	for(int Weapon = WEAPON_SHOTGUN; Weapon < NUM_WEAPONS; Weapon++)
	{
		if(Core.m_aWeapons[Weapon].m_Got)
		{
			pChar->SetActiveWeapon(Weapon);
			return;
		}
	}
	pChar->SetActiveWeapon(-1);
}

void CFastPractice::TogglePracticeHit(CCharacter *pChar, int Weapon) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	switch(Weapon)
	{
	case WEAPON_HAMMER: Core.m_HammerHitDisabled = !Core.m_HammerHitDisabled; break;
	case WEAPON_SHOTGUN: Core.m_ShotgunHitDisabled = !Core.m_ShotgunHitDisabled; break;
	case WEAPON_GRENADE: Core.m_GrenadeHitDisabled = !Core.m_GrenadeHitDisabled; break;
	case WEAPON_LASER: Core.m_LaserHitDisabled = !Core.m_LaserHitDisabled; break;
	default: return;
	}
	pChar->SetCore(Core);
}

vec2 CFastPractice::ClampToPracticePlayableBounds(const vec2 &Pos) const
{
	float MinX = -std::numeric_limits<float>::max();
	float MinY = -std::numeric_limits<float>::max();
	float MaxX = std::numeric_limits<float>::max();
	float MaxY = std::numeric_limits<float>::max();
	if(CMapItemLayerTilemap *pGameLayer = Layers()->GameLayer())
	{
		constexpr float OuterKillTileBoundaryDistance = 201.0f * 32.0f;
		MinX = (-OuterKillTileBoundaryDistance) + 1.0f;
		MinY = (-OuterKillTileBoundaryDistance) + 1.0f;
		MaxX = (-OuterKillTileBoundaryDistance) + (pGameLayer->m_Width * 32.0f) + (OuterKillTileBoundaryDistance * 2.0f) - 1.0f;
		MaxY = (-OuterKillTileBoundaryDistance) + (pGameLayer->m_Height * 32.0f) + (OuterKillTileBoundaryDistance * 2.0f) - 1.0f;
	}
	return vec2(std::clamp(Pos.x, MinX, MaxX), std::clamp(Pos.y, MinY, MaxY));
}

vec2 CFastPractice::ComputeCursorWorldPos(const CCharacter *pChar) const
{
	const vec2 MouseOffset = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	vec2 TargetCameraOffset(0.0f, 0.0f);
	const float CursorLength = length(MouseOffset);
	if(CursorLength > 0.0001f)
	{
		const float OffsetAmount = std::max(CursorLength - (float)GameClient()->m_Camera.Deadzone(), 0.0f) * ((float)GameClient()->m_Camera.FollowFactor() / 100.0f);
		TargetCameraOffset = normalize_pre_length(MouseOffset, CursorLength) * OffsetAmount;
	}

	return pChar->Core()->m_Pos + (MouseOffset - TargetCameraOffset) * GameClient()->m_Camera.m_Zoom + TargetCameraOffset;
}

void CFastPractice::StoreLastTeleport(int ClientId, const vec2 &Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasLastTeleport = true;
	State.m_LastTeleportPos = Pos;
}

void CFastPractice::StoreLastDeathPosition(int ClientId, const vec2 &Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasLastDeath = true;
	State.m_LastDeathPos = Pos;
}

bool CFastPractice::IsSafeRescueTile(int Tile) const
{
	return Tile != TILE_DEATH &&
	       Tile != TILE_FREEZE &&
	       Tile != TILE_DFREEZE &&
	       Tile != TILE_LFREEZE;
}

bool CFastPractice::IsSafeRescuePosition(const vec2 &Pos, float ProximityRadius) const
{
	if(!Collision() || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0)
		return false;

	const float HalfSize = std::max(6.0f, ProximityRadius * 0.5f);
	if(Collision()->TestBox(Pos, vec2(HalfSize * 2.0f, HalfSize * 2.0f)))
		return false;

	const vec2 aSamplePoints[] = {
		Pos,
		Pos + vec2(-HalfSize, -HalfSize),
		Pos + vec2(HalfSize, -HalfSize),
		Pos + vec2(-HalfSize, HalfSize),
		Pos + vec2(HalfSize, HalfSize),
	};
	for(const vec2 &SamplePos : aSamplePoints)
	{
		const int MapIndex = Collision()->GetPureMapIndex(SamplePos);
		if(MapIndex < 0 ||
			!IsSafeRescueTile(Collision()->GetTileIndex(MapIndex)) ||
			!IsSafeRescueTile(Collision()->GetFrontTileIndex(MapIndex)) ||
			!IsSafeRescueTile(Collision()->GetSwitchType(MapIndex)))
		{
			return false;
		}
	}

	if(HasFreezePickupNear(Pos, ProximityRadius + CPickup::ms_CollisionExtraSize))
		return false;

	const bool Grounded =
		Collision()->CheckPoint(Pos.x + HalfSize, Pos.y + HalfSize + 5.0f) ||
		Collision()->CheckPoint(Pos.x - HalfSize, Pos.y + HalfSize + 5.0f);
	return Grounded;
}

void CFastPractice::TrackSafeRescuePosition(int ClientId, CCharacter *pChar)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar)
		return;
	if(IsFrozenState(pChar) || !pChar->IsGrounded())
		return;

	const vec2 Pos = pChar->Core()->m_Pos;
	if(!IsSafeRescuePosition(Pos, pChar->GetProximityRadius()))
		return;

	auto &State = m_aPracticeCommandState[ClientId];
	if(State.m_HasRescueAuto && distance(State.m_RescueAutoPos, Pos) < 48.0f)
		return;

	State.m_HasRescueAuto = true;
	State.m_RescueAutoPos = Pos;
}

void CFastPractice::CaptureSavedChar(int ClientId, int SaveTick, SPracticeSavedChar &Out) const
{
	Out = {};
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	CCharacter *pChar = const_cast<CFastPractice *>(this)->m_PracticeWorld.GetCharacterById(ClientId);
	if(!pChar)
		return;

	Out.m_Valid = true;
	Out.m_ClientId = ClientId;
	Out.m_Core = pChar->GetCore();
	Out.m_HookedPlayerId = pChar->Core()->HookedPlayer();
	Out.m_Core.SetCoreWorld(nullptr, nullptr, nullptr);
	Out.m_Core.SetHookedPlayer(-1);
	Out.m_Core.m_AttachedPlayers.clear();
	Out.m_Pos = pChar->m_Pos;
	Out.m_PrevPos = pChar->m_PrevPos;
	Out.m_PrevPrevPos = pChar->m_PrevPrevPos;
	Out.m_FreezeTime = pChar->m_FreezeTime;
	Out.m_FreezeStartDelta = Out.m_Core.m_FreezeStart > 0 ? (SaveTick - Out.m_Core.m_FreezeStart) : 0;
	Out.m_EndlessFreeze = Out.m_Core.m_FreezeEnd == -1;
	Out.m_FreezeEndDelta = Out.m_Core.m_FreezeEnd > SaveTick ? (Out.m_Core.m_FreezeEnd - SaveTick) : 0;
	Out.m_NinjaActivationDelta = (Out.m_Core.m_aWeapons[WEAPON_NINJA].m_Got && Out.m_Core.m_Ninja.m_ActivationTick > 0) ? (SaveTick - Out.m_Core.m_Ninja.m_ActivationTick) : 0;
	Out.m_LastWeapon = pChar->GetLastWeapon();
	Out.m_TeleCheckpoint = pChar->m_TeleCheckpoint;
	Out.m_NinjaJetpack = pChar->m_NinjaJetpack;
	Out.m_LastRefillJumps = pChar->m_LastRefillJumps;
	Out.m_TuneZone = pChar->GetPureTuneZone();
	Out.m_InvincibleAddedEndlessJump = m_aPracticeCommandState[ClientId].m_InvincibleAddedEndlessJump;
	Out.m_RaceStarted = m_aPracticeRaceState[ClientId].m_StartTick >= 0;
	Out.m_RaceStartDelta = Out.m_RaceStarted ? (SaveTick - m_aPracticeRaceState[ClientId].m_StartTick) : 0;
	Out.m_RaceFinished = m_aPracticeRaceState[ClientId].m_Finished;
}

bool CFastPractice::RestoreSavedChar(const SPracticeSavedChar &Saved, int LoadTick)
{
	if(!Saved.m_Valid || Saved.m_ClientId < 0 || Saved.m_ClientId >= MAX_CLIENTS)
		return false;

	CCharacter *pChar = m_PracticeWorld.GetCharacterById(Saved.m_ClientId);
	if(!pChar)
		return false;

	CCharacterCore Core = Saved.m_Core;
	Core.SetCoreWorld(nullptr, nullptr, nullptr);
	Core.SetHookedPlayer(-1);
	Core.m_AttachedPlayers.clear();
	Core.m_FreezeStart = (Saved.m_FreezeTime > 0 || Core.m_DeepFrozen) ? std::max(0, LoadTick - Saved.m_FreezeStartDelta) : 0;
	if(Saved.m_EndlessFreeze)
		Core.m_FreezeEnd = -1;
	else if(Saved.m_FreezeEndDelta > 0)
		Core.m_FreezeEnd = LoadTick + Saved.m_FreezeEndDelta;
	else
		Core.m_FreezeEnd = 0;

	if(Core.m_aWeapons[WEAPON_NINJA].m_Got && Saved.m_NinjaActivationDelta > 0)
		Core.m_Ninja.m_ActivationTick = std::max(0, LoadTick - Saved.m_NinjaActivationDelta);

	pChar->SetCore(Core);
	pChar->SetCoreWorld(&m_PracticeWorld);
	pChar->SetSolo(Core.m_Solo);
	pChar->SetSuper(Core.m_Super);
	if(!Core.m_Super)
		m_PracticeWorld.m_Teams.Team(Saved.m_ClientId, GameClient()->m_Teams.Team(Saved.m_ClientId));

	pChar->m_Pos = Saved.m_Pos;
	pChar->m_PrevPos = Saved.m_PrevPos;
	pChar->m_PrevPrevPos = Saved.m_PrevPrevPos;
	pChar->m_FreezeTime = Saved.m_FreezeTime;
	pChar->SetLastWeapon(Saved.m_LastWeapon);
	pChar->m_TeleCheckpoint = Saved.m_TeleCheckpoint;
	pChar->m_NinjaJetpack = Saved.m_NinjaJetpack;
	pChar->m_LastRefillJumps = Saved.m_LastRefillJumps;
	pChar->SetTuneZone(Saved.m_TuneZone);
	pChar->m_CanMoveInFreeze = false;
	pChar->m_FrozenLastTick = false;

	m_aPracticeCommandState[Saved.m_ClientId].m_InvincibleAddedEndlessJump = Saved.m_InvincibleAddedEndlessJump;
	m_aPracticeRaceState[Saved.m_ClientId].m_StartTick = Saved.m_RaceStarted ? std::max(0, LoadTick - Saved.m_RaceStartDelta) : -1;
	m_aPracticeRaceState[Saved.m_ClientId].m_Finished = Saved.m_RaceFinished;
	m_aFreezeSkinTicks[Saved.m_ClientId] = Core.m_FreezeEnd != 0 ? FREEZE_SKIN_DEBOUNCE_TICKS : 0;

	EnsureActiveWeaponIsValid(pChar);

	CNetObj_PlayerInput NeutralInput = {};
	BuildLiveInputByConn(NeutralInput, ConnectionForClientId(Saved.m_ClientId));
	NeutralizeInput(NeutralInput);
	NeutralInput.m_Fire = ReleasedFireState(NeutralInput.m_Fire);
	pChar->SetInput(&NeutralInput);
	pChar->ResetInput();
	return true;
}

bool CFastPractice::ExecutePracticeSaveCommand(int LocalClientId, int DummyClientId, const std::vector<std::string> &vArgs)
{
	(void)LocalClientId;
	(void)DummyClientId;

	const std::string SlotName = vArgs.size() > 1 ? LowercaseCopy(JoinCommandArgs(vArgs, 1)) : std::string();
	const int SaveTick = m_PracticeWorld.GameTick();

	SPracticeSaveSlot Slot;
	Slot.m_Name = SlotName;
	Slot.m_Valid = true;
	Slot.m_SavedTick = SaveTick;
	CaptureSavedChar(m_EnableLocalClientId, SaveTick, Slot.m_Main);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		CaptureSavedChar(m_EnableDummyClientId, SaveTick, Slot.m_Dummy);
	Slot.m_vSwitchers = m_PracticeWorld.Switchers();

	if(!Slot.m_Main.m_Valid)
	{
		EchoPractice("practice character is not available");
		return true;
	}

	if(SlotName.empty())
	{
		m_DefaultSave = std::move(Slot);
		EchoPractice("practice checkpoint saved");
		return true;
	}

	for(auto &Existing : m_vNamedSaves)
	{
		if(Existing.m_Name == SlotName)
		{
			Existing = std::move(Slot);
			EchoPractice("practice checkpoint '%s' updated", SlotName.c_str());
			return true;
		}
	}

	if((int)m_vNamedSaves.size() >= MAX_NAMED_SAVES)
		m_vNamedSaves.erase(m_vNamedSaves.begin());
	m_vNamedSaves.push_back(std::move(Slot));
	EchoPractice("practice checkpoint '%s' saved", SlotName.c_str());
	return true;
}

bool CFastPractice::ExecutePracticeLoadCommand(int LocalClientId, int DummyClientId, CCharacter *pChar, const std::vector<std::string> &vArgs)
{
	(void)DummyClientId;

	const std::string SlotName = vArgs.size() > 1 ? LowercaseCopy(JoinCommandArgs(vArgs, 1)) : std::string();
	const SPracticeSaveSlot *pSlot = nullptr;
	if(SlotName.empty())
	{
		if(m_DefaultSave.m_Valid)
			pSlot = &m_DefaultSave;
	}
	else
	{
		for(const auto &Named : m_vNamedSaves)
		{
			if(Named.m_Valid && Named.m_Name == SlotName)
			{
				pSlot = &Named;
				break;
			}
		}
	}

	if(!pSlot && (SlotName == "list" || (SlotName.empty() && !m_vNamedSaves.empty())))
	{
		if(m_vNamedSaves.empty())
		{
			EchoPractice("no practice saves found (use /save)");
			return true;
		}
		std::string Names;
		for(size_t i = 0; i < m_vNamedSaves.size(); i++)
		{
			if(i > 0)
				Names.append(", ");
			Names.append(m_vNamedSaves[i].m_Name);
		}
		EchoPractice("practice saves: %s", Names.c_str());
		return true;
	}

	if(!pSlot || !pSlot->m_Valid || !pSlot->m_Main.m_Valid)
	{
		if(SlotName.empty())
			EchoPractice("no practice save found (use /save first)");
		else
			EchoPractice("no practice save named '%s'", SlotName.c_str());
		return true;
	}

	const vec2 OldControlledPos = pChar->Core()->m_Pos;
	const int LoadTick = m_PracticeWorld.GameTick();
	const int TickDelta = LoadTick - pSlot->m_SavedTick;

	ClearPracticeProjectilesAndLasers(m_PracticeWorld);
	m_PracticeWorld.ReleaseHooked(m_EnableLocalClientId);
	if(m_EnableDummyClientId >= 0)
		m_PracticeWorld.ReleaseHooked(m_EnableDummyClientId);

	if(!pSlot->m_vSwitchers.empty())
	{
		auto &DstSwitchers = m_PracticeWorld.Switchers();
		const size_t Count = std::min(DstSwitchers.size(), pSlot->m_vSwitchers.size());
		for(size_t i = 0; i < Count; i++)
		{
			DstSwitchers[i] = pSlot->m_vSwitchers[i];
			for(int t = 0; t < NUM_DDRACE_TEAMS; t++)
			{
				if(DstSwitchers[i].m_aEndTick[t] > 0)
					DstSwitchers[i].m_aEndTick[t] += TickDelta;
				if(DstSwitchers[i].m_aLastUpdateTick[t] > 0)
					DstSwitchers[i].m_aLastUpdateTick[t] += TickDelta;
			}
		}
	}

	RestoreSavedChar(pSlot->m_Main, LoadTick);
	if(m_RequireDummy && m_EnableDummyClientId >= 0 && pSlot->m_Dummy.m_Valid)
		RestoreSavedChar(pSlot->m_Dummy, LoadTick);

	auto RelinkHook = [&](const SPracticeSavedChar &Saved) {
		if(!Saved.m_Valid)
			return;
		CCharacter *pRestored = m_PracticeWorld.GetCharacterById(Saved.m_ClientId);
		if(!pRestored)
			return;
		const int TargetId = Saved.m_HookedPlayerId;
		if(TargetId >= 0 && TargetId != Saved.m_ClientId && m_PracticeWorld.GetCharacterById(TargetId))
		{
			CCharacterCore Core = pRestored->GetCore();
			Core.SetHookedPlayer(TargetId);
			pRestored->SetCore(Core);
		}
		else if(pRestored->Core()->HookedPlayer() >= 0)
		{
			pRestored->ReleaseHook();
		}
	};
	RelinkHook(pSlot->m_Main);
	if(m_RequireDummy && pSlot->m_Dummy.m_Valid)
		RelinkHook(pSlot->m_Dummy);

	if(CCharacter *pControlled = m_PracticeWorld.GetCharacterById(LocalClientId))
		SaveTeleportHistory(LocalClientId, OldControlledPos, pControlled->Core()->m_Pos);

	if(SlotName.empty())
		EchoPractice("practice checkpoint loaded");
	else
		EchoPractice("practice checkpoint '%s' loaded", SlotName.c_str());
	return true;
}

void CFastPractice::FinishMutation(int LocalClientId, int DummyClientId, CCharacter *pChar, bool WeaponsMutated)
{
	if(WeaponsMutated)
	{
		NormalizeWeaponSelectionInput(pChar);
		if(m_RequireDummy && DummyClientId >= 0)
			NormalizeWeaponSelectionInput(m_PracticeWorld.GetCharacterById(DummyClientId));
	}

	auto SyncFireAfterMutation = [&](CCharacter *pTarget, int Conn) {
		if(!pTarget)
			return;
		CNetObj_PlayerInput Live{};
		BuildLiveInputByConn(Live, Conn);
		CNetObj_PlayerInput Input = *pTarget->LatestInput();
		Input.m_Fire = ReleasedFireState(Live.m_Fire);
		Input.m_WantedWeapon = 0;
		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
		pTarget->SetInput(&Input);
		pTarget->ResetInput();
	};
	SyncFireAfterMutation(pChar, ConnectionForClientId(LocalClientId));
	if(m_RequireDummy && DummyClientId >= 0)
		SyncFireAfterMutation(m_PracticeWorld.GetCharacterById(DummyClientId), ConnectionForClientId(DummyClientId));

	for(const int ClientId : {LocalClientId, DummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(CCharacter *pParticipant = m_PracticeWorld.GetCharacterById(ClientId))
		{
			if(pParticipant->Core()->m_FreezeEnd == 0)
				m_aFreezeSkinTicks[ClientId] = 0;
		}
	}

	m_aFastRenderValid.fill(false);
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	SyncPracticePrevWorld();
	StorePredictionState(m_PracticeWorld.GameTick(), m_PracticeWorld.GameTick(), m_PracticeWorld.GameTick(), m_PracticeWorld.GameTick());
	PublishParticipantCores(LocalClientId, DummyClientId);
	GameClient()->m_PredictedTick = m_PracticeWorld.GameTick();
}

bool CFastPractice::ExecutePracticeMetaCommand(const std::string &Cmd)
{
	if(Cmd == "unpractice")
	{
		Disable();
		EchoPractice("practice mode disabled");
		return true;
	}
	if(Cmd == "practice")
	{
		EchoPractice("practice mode already enabled");
		return true;
	}
	if(Cmd == "practicecmdlist")
	{
		EchoPractice("commands: /save /load /saves /r /back /rescuemode /tp /teleport /tpxy /lasttp /tc /telecursor /totele /totelecp /solo /unsolo /deep /undeep /livefreeze /unlivefreeze /shotgun /grenade /laser /rifle /unshotgun /ungrenade /unlaser /unrifle /weapons /unweapons /addweapon /removeweapon /jetpack /unjetpack /infjump /uninfjump /setjumps /ninja /unninja /endless /unendless /invincible /collision /hookcollision /hitothers /fastinput /practice /unpractice");
		return true;
	}
	if(Cmd == "kill")
	{
		ResetPracticeToAnchor();
		return true;
	}
	return false;
}

bool CFastPractice::ExecutePracticeRescueCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs)
{
	auto &State = m_aPracticeCommandState[LocalClientId];
	if(Cmd == "rescuemode")
	{
		if(vArgs.size() <= 1)
		{
			EchoPractice("rescue mode: %s", State.m_RescueManual ? Localize("manual") : Localize("auto"));
			return true;
		}

		const std::string Arg = LowercaseCopy(vArgs[1]);
		if(Arg == "auto")
		{
			State.m_RescueManual = false;
			EchoPractice("rescue mode changed to auto");
		}
		else if(Arg == "manual")
		{
			State.m_RescueManual = true;
			EchoPractice("rescue mode changed to manual");
		}
		else if(Arg == "list")
		{
			EchoPractice("available rescue modes: auto, manual");
		}
		else
		{
			EchoPractice("unknown argument. check '/rescuemode list'");
		}
		return true;
	}

	if(Cmd != "r" && Cmd != "rescue")
		return false;

	const vec2 CurrentPos = pChar->Core()->m_Pos;
	const bool Frozen = IsFrozenState(pChar);
	if(State.m_RescueManual)
	{
		if(pChar->IsGrounded() && !Frozen && IsSafeRescuePosition(CurrentPos, pChar->GetProximityRadius()))
		{
			State.m_HasRescueManual = true;
			State.m_RescueManualPos = CurrentPos;
			EchoPractice("manual rescue point saved");
			TrackSafeRescuePosition(LocalClientId, pChar);
		}
		else if(State.m_HasRescueManual && IsSafeRescuePosition(State.m_RescueManualPos, pChar->GetProximityRadius()))
		{
			ApplyPracticeTeleport(LocalClientId, pChar, State.m_RescueManualPos);
		}
		else
		{
			EchoPractice("can't set manual rescue while not grounded");
		}
		return true;
	}

	if(State.m_HasRescueAuto && IsSafeRescuePosition(State.m_RescueAutoPos, pChar->GetProximityRadius()))
	{
		ApplyPracticeTeleport(LocalClientId, pChar, State.m_RescueAutoPos);
		return true;
	}
	if(IsSafeRescuePosition(CurrentPos, pChar->GetProximityRadius()))
	{
		TrackSafeRescuePosition(LocalClientId, pChar);
		EchoPractice("safe position updated");
	}
	else
	{
		EchoPractice("no safe rescue position found");
	}
	return true;
}

bool CFastPractice::ExecutePracticeTeleportCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs)
{
	const auto &State = m_aPracticeCommandState[LocalClientId];
	if(Cmd == "back")
	{
		if(!State.m_HasLastDeath)
		{
			EchoPractice("there is nowhere to go back to");
			return true;
		}
		ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(State.m_LastDeathPos));
		return true;
	}

	if(Cmd == "lasttp")
	{
		if(!State.m_HasLastTeleport)
		{
			EchoPractice("you haven't previously teleported");
			return true;
		}
		ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(State.m_LastTeleportPos));
		return true;
	}

	if(Cmd == "tp" || Cmd == "teleport" || Cmd == "tc" || Cmd == "telecursor")
	{
		vec2 Target = ComputeCursorWorldPos(pChar);
		if(vArgs.size() > 1)
		{
			const std::string TargetName = JoinCommandArgs(vArgs, 1);
			const int TargetId = FindClientByName(TargetName.c_str());
			if(TargetId < 0)
			{
				EchoPractice("no player with this name found");
				return true;
			}
			if(IsPracticeParticipant(TargetId))
			{
				if(const CCharacter *pPracticeTarget = m_PracticeWorld.GetCharacterById(TargetId))
				{
					Target = pPracticeTarget->Core()->m_Pos;
					ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(Target));
					return true;
				}
			}
			if(!GameClient()->m_Snap.m_aCharacters[TargetId].m_Active)
			{
				EchoPractice("no player with this name found");
				return true;
			}
			Target = vec2((float)GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur.m_X, (float)GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur.m_Y);
		}

		ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(Target));
		return true;
	}

	if(Cmd == "tpxy")
	{
		if(vArgs.size() < 3)
		{
			EchoPractice("usage: /tpxy x y");
			return true;
		}

		float X = 0.0f;
		float Y = 0.0f;
		if(!ParseCoordinateToken(vArgs[1].c_str(), pChar->Core()->m_Pos.x, X))
		{
			EchoPractice("invalid X coordinate");
			return true;
		}
		if(!ParseCoordinateToken(vArgs[2].c_str(), pChar->Core()->m_Pos.y, Y))
		{
			EchoPractice("invalid Y coordinate");
			return true;
		}

		ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(vec2(X, Y)));
		return true;
	}

	if(Cmd != "totele" && Cmd != "totelecp")
		return false;

	if(vArgs.size() < 2)
	{
		EchoPractice("usage: /%s <index>", Cmd.c_str());
		return true;
	}

	int TeleIndex = 0;
	if(!str_toint(vArgs[1].c_str(), &TeleIndex) || TeleIndex <= 0 || TeleIndex > 255)
	{
		EchoPractice("invalid teleporter index");
		return true;
	}

	const auto &vTeleOuts = Cmd == "totele" ? Collision()->TeleOuts(TeleIndex - 1) : Collision()->TeleCheckOuts(TeleIndex - 1);
	if(vTeleOuts.empty())
	{
		EchoPractice("there is no teleporter with that index on the map");
		return true;
	}

	ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(vTeleOuts[0]));
	return true;
}

bool CFastPractice::ExecutePracticeStateCommand(CCharacter *pChar, const std::string &Cmd)
{
	if(Cmd == "solo" || Cmd == "unsolo")
	{
		pChar->SetSolo(Cmd == "solo");
		return true;
	}

	if(Cmd == "deep" || Cmd == "undeep")
	{
		CCharacterCore Core = pChar->GetCore();
		if(Cmd == "deep")
		{
			Core.m_DeepFrozen = true;
			pChar->SetCore(Core);
			pChar->Freeze();
		}
		else
		{
			Core.m_DeepFrozen = false;
			pChar->SetCore(Core);
			pChar->Unfreeze();
		}
		NormalizeCharacterAfterReset(pChar, Cmd == "deep");
		return true;
	}

	if(Cmd == "livefreeze" || Cmd == "unlivefreeze")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_LiveFrozen = Cmd == "livefreeze";
		pChar->SetCore(Core);
		if(Cmd == "unlivefreeze")
			pChar->Unfreeze();
		NormalizeCharacterAfterReset(pChar, Cmd == "livefreeze");
		return true;
	}

	if(Cmd == "ninja" || Cmd == "unninja")
	{
		if(Cmd == "ninja")
			pChar->GiveNinja();
		else
		{
			pChar->RemoveNinja();
			EnsureActiveWeaponIsValid(pChar);
		}
		return true;
	}

	return false;
}

bool CFastPractice::ExecutePracticeWeaponCommand(CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs, bool &WeaponsMutated)
{
	if(Cmd == "shotgun" || Cmd == "grenade" || Cmd == "laser" || Cmd == "rifle" || Cmd == "unshotgun" || Cmd == "ungrenade" || Cmd == "unlaser" || Cmd == "unrifle" || Cmd == "weapons" || Cmd == "unweapons")
	{
		if(Cmd == "shotgun")
			GivePracticeWeapon(pChar, WEAPON_SHOTGUN, false);
		else if(Cmd == "grenade")
			GivePracticeWeapon(pChar, WEAPON_GRENADE, false);
		else if(Cmd == "laser" || Cmd == "rifle")
			GivePracticeWeapon(pChar, WEAPON_LASER, false);
		else if(Cmd == "unshotgun")
			GivePracticeWeapon(pChar, WEAPON_SHOTGUN, true);
		else if(Cmd == "ungrenade")
			GivePracticeWeapon(pChar, WEAPON_GRENADE, true);
		else if(Cmd == "unlaser" || Cmd == "unrifle")
			GivePracticeWeapon(pChar, WEAPON_LASER, true);
		else if(Cmd == "weapons")
			GivePracticeWeapon(pChar, -1, false);
		else if(Cmd == "unweapons")
			GivePracticeWeapon(pChar, -1, true);
		EnsureActiveWeaponIsValid(pChar);
		WeaponsMutated = true;
		return true;
	}

	if(Cmd != "addweapon" && Cmd != "removeweapon")
		return false;

	if(vArgs.size() < 2)
	{
		EchoPractice("usage: /%s <weapon-id>", Cmd.c_str());
		return true;
	}

	int WeaponId = 0;
	if(!str_toint(vArgs[1].c_str(), &WeaponId) || WeaponId != ClampWeaponId(WeaponId))
	{
		EchoPractice("invalid weapon id");
		return true;
	}

	GivePracticeWeapon(pChar, WeaponId, Cmd == "removeweapon");
	EnsureActiveWeaponIsValid(pChar);
	WeaponsMutated = true;
	return true;
}

bool CFastPractice::ExecutePracticeMovementCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs)
{
	if(Cmd != "jetpack" && Cmd != "unjetpack" && Cmd != "infjump" && Cmd != "uninfjump" && Cmd != "endless" && Cmd != "unendless" && Cmd != "setjumps")
		return false;

	auto &State = m_aPracticeCommandState[LocalClientId];
	CCharacterCore Core = pChar->GetCore();
	if(Cmd == "jetpack")
		Core.m_Jetpack = true;
	else if(Cmd == "unjetpack")
		Core.m_Jetpack = false;
	else if(Cmd == "infjump")
	{
		Core.m_EndlessJump = true;
		State.m_InvincibleAddedEndlessJump = false;
	}
	else if(Cmd == "uninfjump")
	{
		Core.m_EndlessJump = false;
		State.m_InvincibleAddedEndlessJump = false;
	}
	else if(Cmd == "endless")
		Core.m_EndlessHook = true;
	else if(Cmd == "unendless")
		Core.m_EndlessHook = false;
	else if(Cmd == "setjumps")
	{
		if(vArgs.size() < 2)
		{
			EchoPractice("usage: /setjumps <count>");
			return true;
		}
		int Jumps = 0;
		if(!str_toint(vArgs[1].c_str(), &Jumps))
		{
			EchoPractice("invalid jumps value");
			return true;
		}
		Core.m_Jumps = std::clamp(Jumps, -1, 255);
		Core.m_Jumped = 0;
		Core.m_JumpedTotal = 0;
	}

	pChar->SetCore(Core);
	return true;
}

bool CFastPractice::ExecutePracticeInvincibleCommand(int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs)
{
	auto &State = m_aPracticeCommandState[LocalClientId];
	bool Invincible = false;
	CCharacterCore Core = pChar->GetCore();
	if(vArgs.size() > 1)
	{
		int Value = 0;
		if(!str_toint(vArgs[1].c_str(), &Value))
		{
			EchoPractice("invalid value, use 0 or 1");
			return true;
		}
		Invincible = Value != 0;
	}
	else
	{
		Invincible = !Core.m_Invincible;
	}

	if(Invincible)
	{
		pChar->SetSuper(false);
		m_PracticeWorld.m_Teams.Team(LocalClientId, GameClient()->m_Teams.Team(LocalClientId));
		Core = pChar->GetCore();
		Core.m_DeepFrozen = false;
		Core.m_LiveFrozen = false;
		Core.m_IsInFreeze = false;
		Core.m_FreezeEnd = 0;
		if(!Core.m_EndlessJump)
		{
			Core.m_EndlessJump = true;
			State.m_InvincibleAddedEndlessJump = true;
		}
		else
		{
			State.m_InvincibleAddedEndlessJump = false;
		}
		pChar->SetCore(Core);
		pChar->Unfreeze();
		pChar->m_FreezeTime = 0;
	}

	Core = pChar->GetCore();
	if(!Invincible && State.m_InvincibleAddedEndlessJump)
	{
		Core.m_EndlessJump = false;
		State.m_InvincibleAddedEndlessJump = false;
	}
	Core.m_Invincible = Invincible;
	pChar->SetCore(Core);
	EchoPractice("invincible %s", Invincible ? "enabled" : "disabled");
	return true;
}

bool CFastPractice::ExecutePracticeCollisionCommand(CCharacter *pChar, const std::string &Cmd)
{
	if(Cmd != "collision" && Cmd != "hookcollision")
		return false;

	CCharacterCore Core = pChar->GetCore();
	if(Cmd == "collision")
		Core.m_CollisionDisabled = !Core.m_CollisionDisabled;
	else
		Core.m_HookHitDisabled = !Core.m_HookHitDisabled;
	pChar->SetCore(Core);
	return true;
}

bool CFastPractice::ExecutePracticeHitOthersCommand(CCharacter *pChar, const std::vector<std::string> &vArgs)
{
	if(vArgs.size() <= 1 || str_comp_nocase(vArgs[1].c_str(), "all") == 0)
	{
		CCharacterCore Core = pChar->GetCore();
		const bool IsDisabled = Core.m_HammerHitDisabled && Core.m_ShotgunHitDisabled && Core.m_GrenadeHitDisabled && Core.m_LaserHitDisabled;
		Core.m_HammerHitDisabled = !IsDisabled;
		Core.m_ShotgunHitDisabled = !IsDisabled;
		Core.m_GrenadeHitDisabled = !IsDisabled;
		Core.m_LaserHitDisabled = !IsDisabled;
		pChar->SetCore(Core);
		return true;
	}

	const std::string Arg = LowercaseCopy(vArgs[1]);
	if(Arg == "hammer")
		TogglePracticeHit(pChar, WEAPON_HAMMER);
	else if(Arg == "shotgun")
		TogglePracticeHit(pChar, WEAPON_SHOTGUN);
	else if(Arg == "grenade")
		TogglePracticeHit(pChar, WEAPON_GRENADE);
	else if(Arg == "laser")
		TogglePracticeHit(pChar, WEAPON_LASER);
	else
		EchoPractice("unknown argument for /hitothers");
	return true;
}

bool CFastPractice::ExecutePracticeCommand(int LocalClientId, int DummyClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated)
{
	WeaponsMutated = false;
	if(vArgs.empty() || vArgs[0].size() < 2 || vArgs[0][0] != '/')
		return false;

	const std::string Cmd = LowercaseCopy(vArgs[0].substr(1));
	if(ExecutePracticeMetaCommand(Cmd))
		return true;
	if(Cmd == "save")
		return ExecutePracticeSaveCommand(LocalClientId, DummyClientId, vArgs);
	if(Cmd == "load")
	{
		WeaponsMutated = true;
		return ExecutePracticeLoadCommand(LocalClientId, DummyClientId, pChar, vArgs);
	}
	if(Cmd == "saves")
	{
		const std::vector<std::string> ListArgs = {"/load", "list"};
		return ExecutePracticeLoadCommand(LocalClientId, DummyClientId, pChar, ListArgs);
	}
	if(ExecutePracticeRescueCommand(LocalClientId, pChar, Cmd, vArgs))
		return true;
	if(ExecutePracticeTeleportCommand(LocalClientId, pChar, Cmd, vArgs))
		return true;
	if(ExecutePracticeStateCommand(pChar, Cmd))
		return true;
	if(ExecutePracticeWeaponCommand(pChar, Cmd, vArgs, WeaponsMutated))
		return true;
	if(ExecutePracticeMovementCommand(LocalClientId, pChar, Cmd, vArgs))
		return true;
	if(Cmd == "invincible" || Cmd == "invisbl" || Cmd == "invinsbl")
		return ExecutePracticeInvincibleCommand(LocalClientId, pChar, vArgs);
	if(ExecutePracticeCollisionCommand(pChar, Cmd))
		return true;
	if(Cmd == "hitothers")
		return ExecutePracticeHitOthersCommand(pChar, vArgs);
	if(Cmd == "fastinput" || Cmd == "fi")
	{
		if(vArgs.size() > 1)
		{
			const std::string Arg = LowercaseCopy(vArgs[1]);
			if(Arg == "off" || Arg == "disable")
			{
				g_Config.m_AcFastPracticeFastInputOverride = 0;
				EchoPractice("Fast Practice input override disabled (using global settings)");
				return true;
			}
			int Val = 0;
			if(!str_toint(vArgs[1].c_str(), &Val))
			{
				EchoPractice("Usage: /fastinput <0-40|off>");
				return true;
			}
			Val = std::clamp(Val, 0, 40);
			g_Config.m_AcFastPracticeFastInputOverride = 1;
			g_Config.m_AcFastPracticeFastInputAmount = Val;
			if(Val == 0)
				EchoPractice("Fast Practice input set to 0 ms (1:1 pure local, no overrun)");
			else
				EchoPractice("Fast Practice input set to %d ms", Val);
			return true;
		}
		if(g_Config.m_AcFastPracticeFastInputOverride)
			EchoPractice("Fast Practice input is %d ms (override ON)", g_Config.m_AcFastPracticeFastInputAmount);
		else
			EchoPractice("Fast Practice input override is OFF (using global settings)");
		return true;
	}
	return false;
}

bool CFastPractice::ConsumePracticeChatCommand(int Team, const char *pLine)
{
	(void)Team;
	if(!m_Enabled || !pLine || pLine[0] != '/')
		return false;

	if(!ParseCommandArgs(pLine, m_vCmdArgsScratch))
		return false;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return true;
	}

	if(IsRealSpectatorTeam(LocalClientId))
	{
		Disable();
		return false;
	}

	CCharacter *pBaseChar = m_PracticeWorld.GetCharacterById(LocalClientId);
	if(!pBaseChar)
	{
		if(!m_vCmdArgsScratch.empty() && m_vCmdArgsScratch[0].size() >= 2 && m_vCmdArgsScratch[0][0] == '/')
		{
			const std::string Cmd = LowercaseCopy(m_vCmdArgsScratch[0].substr(1));
			if(Cmd == "unpractice")
			{
				Disable();
				EchoPractice("practice mode disabled");
				return true;
			}
			if(Cmd == "kill")
			{
				ResetPracticeToAnchor();
				return true;
			}
		}
		EchoPractice("practice character is not available");
		return true;
	}

	bool WeaponsMutated = false;
	const bool Consumed = ExecutePracticeCommand(LocalClientId, DummyClientId, pBaseChar, m_vCmdArgsScratch, WeaponsMutated);
	if(!Consumed)
		return false;
	if(!m_Enabled)
		return true;
	const std::string Cmd = LowercaseCopy(m_vCmdArgsScratch[0].substr(1));
	if(Cmd != "kill")
	{
		if(CCharacter *pCurrentChar = m_PracticeWorld.GetCharacterById(LocalClientId))
			FinishMutation(LocalClientId, DummyClientId, pCurrentChar, WeaponsMutated);
	}

	if(m_RequireDummy && DummyClientId >= 0 && !m_PracticeWorld.GetCharacterById(DummyClientId))
		DropPracticeDummy();

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	return true;
}

void CFastPractice::OnConsoleInit()
{
	Console()->Register("fast_practice_toggle", "", CFGFLAG_CLIENT, ConFastPracticeToggle, this, "Toggle fast practice mode");
	Console()->Register("ac_fast_practice_toggle", "", CFGFLAG_CLIENT, ConFastPracticeToggle, this, "Toggle fast practice mode (alias)");
}

void CFastPractice::SendSpecOriginal()
{
	const int MainClientId = GameClient()->m_aLocalIds[IClient::CONN_MAIN];
	if(MainClientId < 0 || MainClientId >= MAX_CLIENTS)
		return;

	if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
		return;
	if(GameClient()->m_aClients[MainClientId].m_Spec || GameClient()->m_aClients[MainClientId].m_Paused)
		return;
	if(GameClient()->m_aClients[MainClientId].m_Team == TEAM_SPECTATORS)
		return;

	if(Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = "/spec";
		Client()->SendPackMsg(IClient::CONN_MAIN, &Msg7, MSGFLAG_VITAL, true);
	}
	else
	{
		CNetMsg_Cl_Say Msg;
		Msg.m_Team = 0;
		Msg.m_pMessage = "/spec";
		Client()->SendPackMsg(IClient::CONN_MAIN, &Msg, MSGFLAG_VITAL, true);
	}
}
