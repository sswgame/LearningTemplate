/**
 * @file SequencerPanel.h
 * @brief 시퀀서 타임라인 패널 (SequenceAsset JSON)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
	struct ClipSequence;

	/** @brief 클립/이벤트 트랙 시퀀서. SequenceAsset JSON과 로드/저장합니다. */
	class SequencerPanel : public IEditorPanel
	{
	public:
		/** @brief 기본 클립 시퀀스로 시작합니다. */
		SequencerPanel();
		~SequencerPanel() override;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolPanel() const override { return true; }
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Sequencer"; }
		/** @brief 시퀀서 UI를 그립니다. */
		void drawContent() override;

	private:
		void loadFromFocusedPath();
		void saveToLoadedPath();

	private:
		bool					 _bExpanded;
		int32					 _currentFrame;
		int32					 _selected;
		int32					 _firstFrame;
		utf8					 _arrCinematicNote[512];
		string					 _loadedAssetPath;
		unique_ptr<ClipSequence> _sequence;
	};
} // namespace sw::editor
