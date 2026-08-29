/**
 * @file SequencerPanel.h
 * @brief 시퀀서 타임라인 패널 (SequenceAsset JSON)
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

namespace sw::editor
{
	struct ClipSequence;

	/** @brief 클립/이벤트 트랙 시퀀서. SequenceAsset JSON과 로드/저장합니다. */
	class SequencerPanel : public EditorDocumentPanel
	{
	public:
		/** @brief 기본 클립 시퀀스로 시작합니다. */
		SequencerPanel();
		~SequencerPanel() override;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 시퀀서 UI를 그립니다. */
		void drawContent() override;
		bool saveDocument() override;

	private:
		void loadFromFocusedPath();
		void saveToLoadedPath();

	private:
		bool					 _bExpanded;
		int32					 _currentFrame;
		int32					 _selected;
		int32					 _firstFrame;
		utf8					 _arrCinematicNote[512];
		unique_ptr<ClipSequence> _sequence;
	};
} // namespace sw::editor
