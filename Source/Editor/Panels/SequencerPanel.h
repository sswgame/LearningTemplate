#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorDocumentPanel.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    class SequencePlayer;
} // namespace sw

namespace sw::editor
{
    struct ClipSequence;

    /** @brief 클립 · 이벤트 트랙 시퀀서입니다. SequenceAsset JSON 을 읽고 씁니다. */
    class SequencerPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 기본 클립 시퀀스로 시작합니다. */
        SequencerPanel();
        ~SequencerPanel() override;

        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;

    private:
        string captureDocumentText() const override;
        void   applyDocumentText( string_view text ) override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        void                saveToLoadedPath();
        void                applyAsset( const SequenceAsset& asset );
        SequenceAsset       captureAsset() const;
        void                syncPreviewPlayer();
        void                tickPreview( float32 deltaSeconds );

    private:
        fixed_string<constant::kMaxBuffer512> _cinematicNote;
        unique_ptr<ClipSequence>              _sequence;
        unique_ptr<sw::SequencePlayer>        _previewPlayer;
        int32                                 _currentFrame;
        int32                                 _selected;
        int32                                 _firstFrame;
        bool                                  _bExpanded;
    };
} // namespace sw::editor
