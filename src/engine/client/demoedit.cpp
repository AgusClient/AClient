#include "demoedit.h"

#include <base/str.h>

#include <engine/shared/demo.h>
#include <engine/storage.h>

CDemoEdit::CDemoEdit(const char *pNetVersion, CSnapshotDelta *pSnapshotDelta, CSnapshotDelta *pSnapshotDeltaSixup, IStorage *pStorage, const char *pDemo, const char *pDst, int StartTick, int EndTick, const char *pDstFull) :
	m_SnapshotDelta(*pSnapshotDelta),
	m_SnapshotDeltaSixup(*pSnapshotDeltaSixup),
	m_pStorage(pStorage)
{
	str_copy(m_aDemo, pDemo);
	str_copy(m_aDst, pDst);
	if(pDstFull && pDstFull[0] != '\0')
		str_copy(m_aDstFull, pDstFull);
	else
		m_aDstFull[0] = '\0';

	m_StartTick = StartTick;
	m_EndTick = EndTick;

	// Init the demoeditor
	m_DemoEditor.Init(&m_SnapshotDelta, &m_SnapshotDeltaSixup, nullptr, pStorage);
}

void CDemoEdit::Run()
{
	// Slice the current demo
	m_Success = m_DemoEditor.Slice(m_aDemo, m_aDst, m_StartTick, m_EndTick, nullptr, nullptr);
	if(m_Success)
	{
		if(m_aDstFull[0] != '\0')
		{
			// Preserve the full source demo by renaming it to the requested full destination
			m_pStorage->RemoveFile(m_aDstFull, IStorage::TYPE_SAVE);
			m_pStorage->RenameFile(m_aDemo, m_aDstFull, IStorage::TYPE_SAVE);
		}
		else
		{
			// Remove the temporary demo file if slicing is successful and no full destination was requested
			m_pStorage->RemoveFile(m_aDemo, IStorage::TYPE_SAVE);
		}
	}
}
