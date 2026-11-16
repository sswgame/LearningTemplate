#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/Widgets/EditorCurveEditorState.h"
#include "Editor/Panels/Animation/Model/SequenceClipTiming.h"
#include "Editor/Panels/Animation/Model/SequenceModel.h"
#include "Editor/Panels/Animation/Model/SequencerViewModel.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
    class SequencePlayer;
} // namespace sw

namespace sw::editor
{
    struct ClipSequence;

    /**
     * @brief 바인딩 · 카메라 컷 차선 · 키 트랙(트랜스폼 · 프로퍼티 · 리그 슬롯 · 렌즈) · 구간 항목 타임라인을 편집하는 시퀀서입니다. SequenceAsset XML 을 읽고 씁니다.
     * @details **바인딩 문맥**: 미리보기와 "선택을 묶기" 는 지금 씬에서 이 시퀀스를 트는 `SequencePlayerComponent` 하나의 바인딩 표를 쓴다(언리얼 Sequencer 가 레벨의 Level Sequence Actor 로 여는 것과 같다).
     *          그런 플레이어가 없으면 바인딩을 묶을 곳이 없다고 알리고, 시퀀스 편집은 그대로 된다. 묶기는 그 플레이어 오브젝트의 수정으로 되돌리기에 남는다.
     *          MVVM View — 씬 쪽(문맥 · 선택 · 바인딩 대상 · 미리보기 · 파일)은 `SequencerViewModel` 이 맡고 값은 `SequencerSceneViewState` 로 읽는다. 시퀀스 문서는 창 문서라 이 창이 든다.
     *          시간 막대를 옮기면(끄는 동안에도) 그 프레임을 씬에 바로 적용합니다(뷰포트 미리보기 — 구간 동작 포함). 키 트랙 채널 값을 고치면 지금 프레임에
     *          그 채널의 키가 생기고(언리얼 Sequencer 의 자동 키), Key 단추는 대상의 지금 상태를 모든 채널의 키로 찍습니다. 키 띠의 마름모는 눌러 고르고(Ctrl 은 더하기 · 빼기) 끌어 옮긴다(정수 프레임 스냅).
     *          이름: 시간 막대 `sequencer.scrub`, 트랙 줄 `sequencer.track.<n>`, 키 띠 `sequencer.keyStrip.<n>`, `sequencer.addKey`, 채널 값 `sequencer.channel.<n>`,
     *          커브 단추 `sequencer.curve.<n>`, 트랙 더하기 `sequencer.addTrack.<종류 열거자 이름>`(종류 표를 돈다), 고른 키 지우기 `sequencer.deleteSelectedKeys`,
     *          트랙 순서 `sequencer.moveTrackUp` · `sequencer.moveTrackDown`, 키 복사 · 붙여넣기 `sequencer.copyKeys` · `sequencer.pasteKeys`, 오류 배지 `sequencer.trackIssue.<n>`,
     *          보기 `sequencer.zoomIn` · `sequencer.zoomOut` · `sequencer.zoomFit` · `sequencer.viewScroll`(Ctrl+휠 확대, Shift+휠 스크롤 — 키 띠 · 컷 차선 위),
     *          바인딩 줄 `sequencer.binding.<n>`, `sequencer.bindSelection`, `sequencer.addBindingFromSelection`, 컷 차선 `sequencer.cutLane`, `sequencer.addCut`,
     *          쇼트 도구 `sequencer.splitCut` · `sequencer.deleteCut` · `sequencer.cutCamera` · 쇼트 목록 줄 `sequencer.shot.<n>`, 항목 복사 · 붙여넣기 `sequencer.copyItem` · `sequencer.pasteItem`,
     *          항목 칸 `sequencer.item.name` · `.binding` · `.prefab` · `.preset` · `.scenario` · `.clip` · `.sound` · `.text`, 초안 단추 `sequencer.item.create` · `.cancel`.
     *          편집 조작은 언리얼 Sequencer(트랙 끌어 순서 바꾸기 · 키/섹션 복사 · Camera Cut 섹션 Split)와 유니티 Timeline(Ctrl+휠 확대)을 따른다 — 단추로 두는 것은 ImGui 표 줄 끌기가 키 띠 끌기와 겹치기 때문이다.
     */
    class SequencerPanel : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "sequencer";

        /** @brief 기본 클립 시퀀스로 시작합니다. */
        SequencerPanel();
        ~SequencerPanel() override;

        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

        /** @brief 고른 키 트랙의 키 프레임 수입니다(채널마다 같은 프레임의 키는 하나로 센다). 고른 트랙이 없으면 false 입니다. */
        [[nodiscard]] bool findSelectedTrackKeyFrameCount( uint32& outCount ) const;
        /** @brief 편집 중인 문서입니다(감시 값 · 테스트가 읽는다). */
        const SequenceModel& getDocument() const { return _document; }
        /** @brief 바인딩 문맥(이 시퀀스를 트는 플레이어)에서 묶이지 않은 바인딩 수입니다. 문맥이 없으면 모든 바인딩입니다. */
        [[nodiscard]] uint32 countUnboundBindings() const;

    private:
        string captureDocumentText() const override;
        void   applyDocumentText( string_view text ) override;
        /** @brief 문서를 읽어 내용을 채웁니다. 읽음 표시는 기반이 결과로 합니다(`EditorDocumentPanel::reloadDocument`). */
        ToolAssetLoadResult loadDocument() override;
        void                saveToLoadedPath();
        void                applyAsset( const SequenceAsset& asset );
        SequenceAsset       captureAsset() const;
        void                syncPreviewPlayer();
        void                updatePreview( float32 deltaSeconds );
        /** @brief 지금 프레임을 씬에 적용합니다(뷰포트 미리보기). */
        void previewCurrentFrame();
        /** @brief @p asset 의 @p frame 을 바인딩 문맥으로 씬에 적용합니다(키 · 클립 · 컷 · 구간 동작). */
        void applyPreviewFrame( const SequenceAsset& asset, int32 frame );
        /** @brief 미리보기가 들어간 구간(소리 · 애니메이션 덮어쓰기)을 나가고 컷을 풉니다(Stop · 문서 교체). */
        void releasePreview();

        // 바인딩
        /** @brief 바인딩 목록 · 문맥 고르기 · 선택 묶기 · 이름 칸을 그립니다. */
        void drawBindings();
        /** @brief 바인딩 @p bindingID 를 에디터에서 고른 오브젝트에 묶습니다(문맥 플레이어의 수정 — 되돌리기). */
        void bindSelectionTo( uint32 bindingID );
        /** @brief 바인딩의 표시 이름입니다(없으면 "?"). */
        const utf8* findBindingName( uint32 bindingID ) const;

        // 카메라 컷 차선
        /** @brief 카메라 컷만 모은 차선(컷 구간 · 카메라 이름 · 지금 프레임)과 컷 더하기 단추를 그립니다. 컷을 누르면 그 항목을 고른다. */
        void drawCutLane();
        /** @brief 쇼트 도구(지금 프레임에서 나누기 · 고른 컷 지우기 · 고른 컷의 카메라를 고른 바인딩으로)와 쇼트 목록(번호 · 카메라 · 구간 · 길이)을 그립니다. */
        void drawShotTools();

        // 보기
        /** @brief 확대 · 축소 · 맞추기 단추와 보이는 범위 스크롤을 그립니다(키 띠 · 컷 차선이 같은 범위를 쓴다). */
        void drawViewControls();
        /** @brief 띠(@p originX · @p width) 위에서 Ctrl+휠은 포인터 자리를 두고 확대 · 축소, Shift+휠(또는 가로 휠)은 스크롤합니다. 직전 항목이 그 띠여야 한다. */
        void handleTimelineWheel( float32 originX, float32 width );

        // 구간 항목
        /** @brief 구간 항목의 칸을 그립니다 — 이름 · 바인딩과 종류가 쓰는 칸(종류 표)입니다. 초안이 열려 있으면 초안을, 아니면 고른 항목을 고칩니다. */
        void drawItemProperties();
        /** @brief 항목의 바인딩 고르기 콤보입니다. 바꿨으면 true 입니다. */
        [[nodiscard]] bool drawItemBindingCombo( SequenceTrackItem& edit, const SequenceItemKindInfo& kindInfo );
        /** @brief 초안의 만들기 · 취소 단추와 안내입니다. 만들기는 필수 칸이 찰 때까지 꺼져 있습니다. */
        void drawItemDraftButtons();

        // 키 트랙
        /** @brief 키 트랙 목록 · 고른 트랙의 채널 값 · 커브 편집기를 그립니다. */
        void drawKeyTracks();
        /** @brief 트랙 줄 오른쪽의 키 띠(키 위치 · 고른 키 · 지금 프레임)를 그립니다. 키를 누르면 고르고 끌면 옮기며, 빈 곳을 누르면 그 트랙을 고르고 그 프레임으로 갑니다. */
        void drawKeyStrip( const SequenceKeyTrack& track, int32 trackIndex );
        /** @brief 고른 트랙의 이름 · 바인딩 · 키 단추 · 채널 값을 그립니다. */
        void drawSelectedTrack( SequenceKeyTrack& track );
        /** @brief 트랙 텍스트 필드 하나입니다. 손을 놓으며 바뀌었으면 편집으로 기록하고, @p bPreviewOnEdit 이면 현재 프레임을 다시 미리 봅니다. */
        void drawTrackTextField( const utf8* pLabel, string& inoutText, const utf8* pTooltip, bool bPreviewOnEdit );
        /** @brief 키 추가 · 삭제 · 이전 · 다음 키 이동 단추와 대상을 못 찾았다는 안내입니다. @p pArrCurrent 는 대상의 지금 채널 값입니다. */
        void drawKeyButtons( SequenceKeyTrack& track, float32 frame, const float32* pArrCurrent, bool bTargetFound );
        /** @brief 채널마다 키 표식 · 값 드래그 필드 · 곡선 열기 단추 한 줄을 그립니다. */
        void drawChannelRows( SequenceKeyTrack& track, float32 frame, const float32* pArrCurrent );
        /** @brief 커브 단추로 고른 채널의 커브 편집기를 그립니다. */
        void drawChannelCurve();
        /** @brief 새 키 트랙을 고른 바인딩에 더하고 고릅니다. */
        void addKeyTrack( SequenceTrackKind kind );

    private:
        string                         _cinematicNote;
        SequenceModel                  _document; ///< 애셋 · 고른 번호(`_sequence` 가 참조하므로 먼저 선언한다)
        unique_ptr<ClipSequence>       _sequence;
        unique_ptr<sw::SequencePlayer> _previewPlayer;
        vector<SequenceClipTiming>     _listTimingBefore; ///< 타임라인 위젯을 부르기 전 배치(멤버라 프레임마다 할당하지 않는다)
        vector<float32>                _listKeyFrame;     ///< 키 띠가 쓰는 키 프레임(멤버라 프레임마다 할당하지 않는다)
        vector<int32>                  _listCutItem;      ///< 컷 차선이 그리는 컷 항목 번호(멤버라 프레임마다 할당하지 않는다)
        SequencerViewModel             _viewModel;        ///< 바인딩 문맥 · 선택 · 미리보기 · 파일(씬 쪽)
        SequencerSceneViewState        _sceneState;       ///< 바인딩 줄이 그리는 씬 쪽 값(`drawBindings` 앞에서 모은다)
        EditorCurveEditorState         _curveState;       ///< 커브 단추로 연 채널의 편집기 상태
        int32                          _currentFrame;
        int32                          _firstFrame;
        int32                          _keyDragTrack; ///< 키를 끄는 중인 트랙(-1 이면 끌지 않는다)
        int32                          _keyDragDelta; ///< 끄는 중인 키의 프레임 이동(놓을 때 적용한다)
        bool                           _bExpanded;    ///< NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };
} // namespace sw::editor
