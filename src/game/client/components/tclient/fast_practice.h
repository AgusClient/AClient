/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_FAST_PRACTICE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_FAST_PRACTICE_H

#include <engine/console.h>
#include <engine/client/enums.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/prediction/gameworld.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class CFastPractice : public CComponent
{
public:
	struct SLocalRaceState
	{
		vec2 m_Position = vec2(0.0f, 0.0f);
		int m_CurrentTick = -1;
		int m_StartTick = -1;
		bool m_Finished = false;
	};

	int Sizeof() const override { return sizeof(*this); }

	bool Enabled() const { return m_Enabled; }
	bool Active() const { return m_Enabled && m_Ready; }
	bool CanEnable() const;
	void Toggle();
	void Enable();
	void Disable();
	void PrepareForSpectating() { if(m_Enabled) Disable(); }
	bool ConsumeKillCommand();
	bool ConsumePracticeChatCommand(int Team, const char *pLine);
	void ResetPracticeToAnchor();

	void PrepareInputForSend(int *pData, int Size, bool Dummy);
	void BuildNeutralInput(CNetObj_PlayerInput &OutInput, bool Dummy, bool UseFrozenTarget = true) const;
	void SyncFromPrediction();
	bool ForcePredictWeapons() const;
	bool ForcePredictGrenade() const;
	bool ForcePredictGunfire() const;
	bool ForcePredictPlayers() const;
	bool HasPracticePickup(vec2 Pos, int Type, int Subtype);
	bool UsingMapDoors() const { return m_MapEntitiesReady; }
	void RenderMapDoors();
	void InvalidateBufferedInputState();
	bool IsPracticeParticipant(int ClientId) const;
	bool ShouldShowPracticeFrozenSkin(int ClientId) const;
	int ControlledPracticeId() const;
	int PartnerPracticeId() const;
	int CurrentPracticeDummyId() const;
	bool GetLocalRaceState(SLocalRaceState &State) const;

	CGameWorld &PracticeWorld() { return m_PracticeWorld; }
	CGameWorld &PracticePrevWorld() { return m_PracticePrevWorld; }
	const CGameWorld &PracticeWorld() const { return m_PracticeWorld; }
	const CGameWorld &PracticePrevWorld() const { return m_PracticePrevWorld; }

	void SendSpecOriginal();
	int PracticeDummyId() const { return CurrentPracticeDummyId(); }
	bool IsPracticeDummy(int ClientId) const { return ClientId >= 0 && ClientId == CurrentPracticeDummyId(); }
	bool OverridePredict() { return false; }
	bool GetFastInputRenderCharacter(int ClientId, CNetObj_Character &Prev, CNetObj_Character &Cur) const;

	void OnReset() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnRender() override;
	void OnConsoleInit() override;

private:
	enum
	{
		INPUT_HISTORY_SIZE = 200,
		MAX_NAMED_SAVES = 32,
		STATIC_PICKUP_ID_BASE = -1000000,
	};

	struct SStoredInput
	{
		CNetObj_PlayerInput m_Input{};
		int m_Tick = -1;
	};

	struct STrackedProjectile
	{
		int m_Owner = -1;
		int m_StartTick = 0;
		int m_Type = WEAPON_GUN;
		int m_TuneZone = 0;
		vec2 m_StartPos = vec2(0.0f, 0.0f);
		vec2 m_StartVel = vec2(0.0f, 0.0f);
	};

	struct SGhostData
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Direction = vec2(1.0f, 0.0f);
		int m_Angle = 0;
		int m_Weapon = WEAPON_HAMMER;
		int m_HookState = HOOK_RETRACTED;
		vec2 m_HookPos = vec2(0.0f, 0.0f);
	};

	struct SAnchorData
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		CNetObj_Character m_Char = {};
		CNetObj_DDNetCharacter m_DDNet = {};
		bool m_HasDDNet = false;
	};

	struct SPracticeCommandState
	{
		bool m_RescueManual = false;
		bool m_HasRescueAuto = false;
		bool m_HasRescueManual = false;
		bool m_HasLastTeleport = false;
		bool m_HasLastDeath = false;
		bool m_HasPendingTeleport = false;
		vec2 m_PendingTeleportPos = vec2(0.0f, 0.0f);
		bool m_InvincibleAddedEndlessJump = false;
		vec2 m_RescueAutoPos = vec2(0.0f, 0.0f);
		vec2 m_RescueManualPos = vec2(0.0f, 0.0f);
		vec2 m_LastTeleportPos = vec2(0.0f, 0.0f);
		vec2 m_LastDeathPos = vec2(0.0f, 0.0f);
	};

	struct SPracticeRaceState
	{
		int m_StartTick = -1;
		bool m_Finished = false;
	};

	struct SPracticeSavedChar
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		CCharacterCore m_Core{};
		int m_HookedPlayerId = -1;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_PrevPos = vec2(0.0f, 0.0f);
		vec2 m_PrevPrevPos = vec2(0.0f, 0.0f);
		int m_FreezeTime = 0;
		int m_FreezeStartDelta = 0;
		int m_FreezeEndDelta = 0;
		bool m_EndlessFreeze = false;
		int m_NinjaActivationDelta = 0;
		int m_LastWeapon = WEAPON_HAMMER;
		int m_TeleCheckpoint = 0;
		bool m_NinjaJetpack = false;
		bool m_LastRefillJumps = false;
		int m_TuneZone = 0;
		bool m_InvincibleAddedEndlessJump = false;
		bool m_RaceStarted = false;
		int m_RaceStartDelta = 0;
		bool m_RaceFinished = false;
	};

	struct SPracticeSaveSlot
	{
		std::string m_Name;
		bool m_Valid = false;
		int m_SavedTick = 0;
		SPracticeSavedChar m_Main;
		SPracticeSavedChar m_Dummy;
		std::vector<SSwitchers> m_vSwitchers;
	};

	struct SMapPickup
	{
		vec2 m_Pos = vec2(0.0f, 0.0f);
		int m_Type = 0;
		int m_Subtype = 0;
		int m_Number = 0;
		int m_Flags = 0;
		bool m_IsMover = false;
	};

	struct STilePickupSlot
	{
		uint32_t m_Start = 0;
		uint8_t m_Count = 0;
	};

	struct SMapDoor
	{
		vec2 m_From = vec2(0.0f, 0.0f);
		vec2 m_To = vec2(0.0f, 0.0f);
		int m_Number = 0;
	};

	bool m_Enabled = false;
	bool m_Ready = false;
	bool m_NeedsRebuild = false;
	bool m_RequireDummy = false;
	int m_EnableLocalClientId = -1;
	int m_EnableDummyClientId = -1;
	bool m_HasDummyAnchor = false;
	bool m_SuppressFireOnNextPredictTick = false;
	int m_InputSuppressTicks = 0;
	int m_LastClDummy = 0;
	int m_LastResolvedLocalClientId = -1;
	int m_LastResolvedDummyClientId = -1;
	int m_LastRoundStartTick = -1;
	bool m_OverridingSpecSnap = false;
	CNetObj_Character m_SpecSyntheticPrev{};
	CNetObj_Character m_SpecSyntheticCur{};
	std::array<ivec2, NUM_DUMMIES> m_aServerLockedTargets{};
	std::array<int, NUM_DUMMIES> m_aServerLockedFire{};
	std::array<int, NUM_DUMMIES> m_aServerLockedNextWeapon{};
	std::array<int, NUM_DUMMIES> m_aServerLockedPrevWeapon{};
	std::array<bool, NUM_DUMMIES> m_aHasServerLockedTargets{};
	std::array<ivec2, MAX_CLIENTS> m_aFrozenTarget{};
	std::array<bool, MAX_CLIENTS> m_aFrozenTargetValid{};
	std::array<vec2, MAX_CLIENTS> m_aSpawnPos{};
	std::array<bool, MAX_CLIENTS> m_aSpawnPosValid{};
	std::array<vec2, MAX_CLIENTS> m_aSafePos{};
	std::array<bool, MAX_CLIENTS> m_aSafePosValid{};
	SStoredInput m_aaStoredInputs[NUM_DUMMIES][INPUT_HISTORY_SIZE]{};
	int m_aNextStoredInput[NUM_DUMMIES]{};
	int m_aStoredInputCount[NUM_DUMMIES]{};
	CNetObj_Character m_aFastRenderPrev[MAX_CLIENTS]{};
	CNetObj_Character m_aFastRenderCur[MAX_CLIENTS]{};
	std::array<bool, MAX_CLIENTS> m_aFastRenderValid{};
	std::array<int, MAX_CLIENTS> m_aLastEventTick{};
	std::array<CCharacterCore, MAX_CLIENTS> m_aPublishedPredicted{};
	std::array<CCharacterCore, MAX_CLIENTS> m_aPublishedPrevPredicted{};
	std::array<CCharacterCore, MAX_CLIENTS> m_aPublishedRegularPredicted{};
	std::array<bool, MAX_CLIENTS> m_aPublishedValid{};

	SGhostData m_MainGhost;
	SGhostData m_DummyGhost;
	SAnchorData m_MainAnchor;
	SAnchorData m_DummyAnchor;
	std::array<int, MAX_CLIENTS> m_aLastAttackTick{};
	std::array<int, MAX_CLIENTS> m_aFreezeSkinTicks{};
	int m_LastFreezeSkinDebounceTick = -1;
	std::array<SPracticeCommandState, MAX_CLIENTS> m_aPracticeCommandState{};
	std::array<SPracticeRaceState, MAX_CLIENTS> m_aPracticeRaceState{};
	SPracticeSaveSlot m_DefaultSave;
	std::vector<SPracticeSaveSlot> m_vNamedSaves;

	CGameWorld m_PracticeWorld;
	CGameWorld m_PracticePrevWorld;
	CGameWorld m_FastOverrunWorld;
	std::vector<STrackedProjectile> m_vTrackedExplosiveBefore;
	std::vector<STrackedProjectile> m_vTrackedExplosiveAfter;
	std::vector<std::string> m_vCmdArgsScratch;
	bool m_MapEntitiesReady = false;
	int m_MapWidth = 0;
	int m_MapHeight = 0;
	std::vector<SMapPickup> m_vMapPickups;
	std::vector<STilePickupSlot> m_vTilePickupSlots;
	std::vector<SMapDoor> m_vMapDoors;
	std::vector<CDoorTile> m_vExtraDoorTiles;

	static constexpr int FREEZE_SKIN_DEBOUNCE_TICKS = 3;

	static void ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData);

	void ResetPracticeState();
	void ClearMapEntities();
	void BuildMapEntities();
	void SpawnStaticPickupsInWorld(CGameWorld &World) const;
	void SpawnMoverPickupsInWorld(CGameWorld &World) const;
	void ApplyStaticPickups(CGameWorld &World, CCharacter *pChar) const;
	bool HasPracticePickupAt(vec2 Pos, int Type, int Subtype) const;
	bool HasFreezePickupNear(const vec2 &Pos, float Radius) const;
	void CopyDynamicWorld(CGameWorld &Dst, CGameWorld &Src, bool PreserveStaticPickups) const;
	void SyncPracticePrevWorld();
	void CollectTrackedProjectiles(CGameWorld &World, int LocalClientId, int DummyClientId, std::vector<STrackedProjectile> &vOut) const;
	void ShiftPracticeTickBase(int Delta);
	void ResetCommandState();
	void ResetPracticeRaceStates();
	void ResetPracticeSaves();
	void UpdatePracticeRaceState(int ClientId, const CCharacter *pChar, int Tick);
	void SyncPracticeWorldConfig(CGameWorld &World);
	bool IsRealSpectatorTeam(int LocalClientId) const;
	bool IsOriginalSpecPaused(int LocalClientId) const;
	void ApplySpecPracticeView(int ControlledId);
	void RestoreSpecPracticeView();
	void DropPracticeDummy();
	bool ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const;
	int CurrentLocalPracticeId() const;
	int ConnectionForClientId(int ClientId) const;
	void UpdateGhostData();
	void UpdateGhostForClientId(int ClientId, SGhostData &Ghost, const SAnchorData &FallbackAnchor);
	void CaptureAnchorsFromSnapshot();
	void SpawnParticipantFromAnchor(CGameWorld &World, const SAnchorData &Anchor) const;
	bool ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const;
	bool Rebuild();
	void PrunePracticeWorld(CGameWorld &World) const;
	void ClearPracticeProjectilesAndLasers(CGameWorld &World) const;
	void ResetAttackTickHistory();
	void TrackFireSound(int ClientId, CCharacter *pChar);
	static int WeaponFireSound(int Weapon);
	void MaybePlayHammerHitEffect(CCharacter *pChar);
	void UpdateFreezeSkinDebounce();
	void RenderGhost(const SGhostData &Ghost, const vec2 &RenderPos, float Alpha) const;
	void ReleaseBufferedInputState();
	void CaptureServerLockedTargets();
	void RestoreServerLockedInputs();
	void CaptureFrozenTargets();
	void ResetStoredInputs();
	void StoreNeutralInput(bool Dummy, int Tick);
	void StoreInputByConn(const CNetObj_PlayerInput &Input, int Conn);
	const CNetObj_PlayerInput *GetStoredInputByConn(int Tick, int Conn) const;
	void BuildLiveInput(CNetObj_PlayerInput &OutInput, bool Dummy) const;
	void BuildLiveInputByConn(CNetObj_PlayerInput &OutInput, int Conn) const;
	void TickPracticeWorld();
	void StorePredictionState(int Tick, int FinalTickRegular, int FinalTickOthers, int FinalTickSelf);
	void SeedPredictionHistory();
	void CachePredictedCore(int ClientId, const CCharacterCore &Core);
	void CachePrevPredictedCore(int ClientId, const CCharacterCore &Core);
	void CacheRegularPredictedCore(int ClientId, const CCharacterCore &Core);
	void RepublishCachedCores() const;
	void FinishMutation(int LocalClientId, int DummyClientId, CCharacter *pChar, bool WeaponsMutated);
	void FillRenderCharacter(CCharacter *pChar, CNetObj_Character &Out) const;
	bool IsDeathTile(const CCharacter *pChar) const;
	void ResetCharacterToSaved(int ClientId, CCharacter *pChar, int Tick);
	void PlayCoreEvents(CCharacter *pChar, int Tick);
	void PublishParticipantCores(int LocalClientId, int DummyClientId);

	[[gnu::format(printf, 2, 3)]]
	void EchoPractice(const char *pFormat, ...) const;
	static bool ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs);
	static bool ParseCoordinateToken(const char *pToken, float Base, float &Out);
	static std::string JoinCommandArgs(const std::vector<std::string> &vArgs, size_t StartIndex);
	int FindClientByName(const char *pName) const;
	void NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const;
	void NormalizeWeaponSelectionInput(CCharacter *pChar) const;
	void TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const;
	void SaveTeleportHistory(int ClientId, const vec2 &CurrentPos, const vec2 &TargetPos);
	void ApplyPracticeTeleport(int ClientId, CCharacter *pChar, const vec2 &TargetPos);
	void GivePracticeWeapon(CCharacter *pChar, int Weapon, bool Remove) const;
	void EnsureActiveWeaponIsValid(CCharacter *pChar) const;
	void TogglePracticeHit(CCharacter *pChar, int Weapon) const;
	vec2 ClampToPracticePlayableBounds(const vec2 &Pos) const;
	vec2 ComputeCursorWorldPos(const CCharacter *pChar) const;
	void StoreLastTeleport(int ClientId, const vec2 &Pos);
	void StoreLastDeathPosition(int ClientId, const vec2 &Pos);
	bool IsSafeRescueTile(int Tile) const;
	bool IsSafeRescuePosition(const vec2 &Pos, float ProximityRadius) const;
	void TrackSafeRescuePosition(int ClientId, CCharacter *pChar);
	void CaptureSavedChar(int ClientId, int SaveTick, SPracticeSavedChar &Out) const;
	bool RestoreSavedChar(const SPracticeSavedChar &Saved, int LoadTick);
	bool ExecutePracticeSaveCommand(int LocalClientId, int DummyClientId, const std::vector<std::string> &vArgs);
	bool ExecutePracticeLoadCommand(int LocalClientId, int DummyClientId, CCharacter *pChar, const std::vector<std::string> &vArgs);
	bool ExecutePracticeMetaCommand(const std::string &Cmd);
	bool ExecutePracticeRescueCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs);
	bool ExecutePracticeTeleportCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs);
	bool ExecutePracticeStateCommand(CCharacter *pChar, const std::string &Cmd);
	bool ExecutePracticeWeaponCommand(CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs, bool &WeaponsMutated);
	bool ExecutePracticeMovementCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs);
	bool ExecutePracticeInvincibleCommand(int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs);
	bool ExecutePracticeCollisionCommand(CCharacter *pChar, const std::string &Cmd);
	bool ExecutePracticeHitOthersCommand(CCharacter *pChar, const std::vector<std::string> &vArgs);
	bool ExecutePracticeCommand(int LocalClientId, int DummyClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated);
};

#endif
