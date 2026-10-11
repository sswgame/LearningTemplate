#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/SequenceTimingUtil.h"
#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorCurveEditor.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    class SequencePlayer;
} // namespace sw

namespace sw::editor
{
    struct ClipSequence;

    /**
     * @brief 구간 항목(클립 · 이벤트 · 카메라 컷) 타임라인과 키 트랙(트랜스폼 · 프로퍼티)을 편집하는 시퀀서입니다. SequenceAsset JSON 을 읽고 씁니다.
     * @details 시간 막대를 옮기면(끄는 동안에도) 그 프레임을 씬에 바로 적용합니다(뷰포트 미리보기). 키 트랙 채널 값을 고치면 지금 프레임에
     *          그 채널의 키가 생기고(언리얼 Sequencer 의 자동 키), Key 단추는 대상의 지금 상태를 모든 채널의 키로 찍습니다.
     *          이름표: 시간 막대 `sequencer.scrub`, 트랙 줄 `sequencer.track.<n>`, `sequencer.addKey`, 채널 값 `sequencer.channel.<n>`,
     *          커브 단추 `sequencer.curve.<n>`, 트랙 더하기 `sequencer.addTransformTrack` · `sequencer.addPropertyTrack`.
     */
    class SequencerPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 기본 클립 시퀀스로 시작합니다. */
        SequencerPanel();
        ~SequencerPanel() override;

        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;

        /** @brief 고른 키 트랙의 키 프레임 수입니다(채널마다 같은 프레임의 키는 하나로 센다). 고른 트랙이 없으면 false 입니다. */
        [[nodiscard]] bool findSelectedTrackKeyFrameCount( uint32& outCount ) const;

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
        /** @brief 지금 프레임을 씬에 적용합니다(뷰포트 미리보기). */
        void previewCurrentFrame();
        /** @brief 고른 구간 항목의 이름 · 대상 칸을 그립니다. */
        void drawItemFields();
        /** @brief 키 트랙 목록 · 고른 트랙의 채널 값 · 커브 편집기를 그립니다. */
        void drawKeyTracks();
        /** @brief 트랙 줄 오른쪽의 키 띠(키 자리 · 지금 프레임)를 그립니다. 띠를 누르면 그 트랙을 고르고 그 프레임으로 갑니다. */
        void drawKeyStrip( const SequenceKeyTrack& track, int32 trackIndex );
        /** @brief 고른 트랙의 이름 · 대상 · 키 단추 · 채널 값을 그립니다. */
        void drawSelectedTrack( SequenceKeyTrack& track );
        /** @brief 커브 단추로 고른 채널의 커브 편집기를 그립니다. */
        void drawChannelCurve();
        /** @brief 새 키 트랙을 더하고 고릅니다. 대상은 에디터에서 고른 오브젝트입니다(없으면 비워 둔다). */
        void addKeyTrack( SequenceTrackKind kind );

    private:
        fixed_string<constant::kMaxBuffer512> _cinematicNote;
        unique_ptr<ClipSequence>              _sequence;
        unique_ptr<sw::SequencePlayer>        _previewPlayer;
        vector<SequenceClipTiming>            _listTimingBefore; ///< 타임라인 위젯을 부르기 전 배치(멤버라 프레임마다 할당하지 않는다)
        vector<SequenceKeyTrack>              _listTrack;        ///< 키 트랙(트랜스폼 · 프로퍼티)
        vector<float32>                       _listKeyFrame;     ///< 키 띠가 쓰는 키 프레임(멤버라 프레임마다 할당하지 않는다)
        EditorCurveEditorState                _curveState;       ///< 커브 단추로 연 채널의 편집기 상태
        int32                                 _currentFrame;
        int32                                 _selected;
        int32                                 _firstFrame;
        int32                                 _selectedTrack; ///< 고른 키 트랙(-1 이면 없음)
        int32                                 _curveTrack;    ///< 커브 편집기가 보는 트랙(-1 이면 닫힘)
        int32                                 _curveChannel;  ///< 커브 편집기가 보는 채널
        bool                                  _bExpanded;
    };
} // namespace sw::editor
