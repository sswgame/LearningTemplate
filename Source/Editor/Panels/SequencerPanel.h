/**
 * @file SequencerPanel.h
 * @brief 시퀀서 타임라인 패널 (SequenceAsset JSON)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
	class SequencePlayer;
} // namespace sw

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

		void drawContent() override;
		bool saveDocument() override;

	private:
		string		  captureDocumentText() const override;
		void		  applyDocumentText( string_view text ) override;
		void		  loadFromFocusedPath();
		void		  saveToLoadedPath();
		void		  applyAsset( const SequenceAsset& asset );
		SequenceAsset captureAsset() const;
		void		  syncPreviewPlayer();
		void		  tickPreview( float32 deltaSeconds );

	private:
		bool						   _bExpanded;
		int32						   _currentFrame;
		int32						   _selected;
		int32						   _firstFrame;
		utf8						   _arrCinematicNote[512];
		unique_ptr<ClipSequence>	   _sequence;
		unique_ptr<sw::SequencePlayer> _previewPlayer;
	};
} // namespace sw::editor
