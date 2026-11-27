/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MUSIC_PLAYER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MUSIC_PLAYER_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <memory>

class ITextRender;

float MusicPlayerLegacyHudAnchorOffsetCanvasX(ITextRender *pTextRender, int LayoutScale, float HudWidth, float TextScale, bool *pOutReady);

class CMusicPlayer : public CComponent
{
public:
	struct SHudReservation
	{
		CUIRect m_Rect{};
		bool m_Visible = false;
		bool m_Active = false;
		float m_PushAmount = 0.0f;
	};

	CMusicPlayer();
	~CMusicPlayer() override;

	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnWindowResize() override;

	void PreviousTrack();
	void PlayPauseTrack();
	void NextTrack();
	bool GetCurrentTrackInfo(char *pOutTitle, int TitleSize, char *pOutArtist, int ArtistSize, bool *pOutPlaying = nullptr, int64_t *pOutPositionMs = nullptr, int64_t *pOutDurationMs = nullptr) const;

	SHudReservation HudReservation() const;
	vec2 GetHudPushOffsetForRect(const CUIRect &Rect, float CanvasWidth, float CanvasHeight, float Padding = 0.0f) const;
	CUIRect GetHudEditorRect(bool ForcePreview = false) const;
	void RenderHudEditor(bool ForcePreview);

private:
	class CImpl;
	std::unique_ptr<CImpl> m_pImpl;

	void RenderMusicPlayer(bool ForcePreview);
	void EnsureImpl();
};

#endif
