/**
 * @file AudioEventBrowserPanel.h
 * @brief 오디오 이벤트 창입니다(Window > Audio Events). 로드된 라이브러리의 이벤트 목록과 속성, 재생 중 인스턴스, 보이스 가상화 프로파일러를 보입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventBrowserUtil.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventEditModel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Engine/Audio/AudioEngineState.h"
#include "Engine/Audio/AudioEvent.h"

namespace sw
{
    struct PropertyInfo;

    class AudioEngine;
    class IAudioService;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioEventBrowserPanel
     * @brief 언리얼 Sound Cue 에디터의 속성 보기 · 오디오 디버거, FMOD Studio 이벤트 속성 · 프로파일러에 해당합니다.
     * @details 이벤트 속성은 기본이 읽기 보기다. "고치기" 를 누르면 그 이벤트가 있는 라이브러리 파일을 사본(`AudioEventEditModel`)으로 다시 읽어 속성 그리드로 고치고
     *          (이벤트 복제 · 지우기, 라이브러리의 파라미터 · 감쇠 프리셋 포함 — FMOD Studio 의 이벤트 · 파라미터 편집에 해당),
     *          "저장" 은 검사를 통과할 때만 파일에 쓴다(`AudioEventLibrary::saveToAsset`). 엔진에 올라간 라이브러리는 저장한 파일을 에셋 핫 리로드
     *          (`IAudioService` 의 라이브러리 다시 읽기)가 바꾼다 — 창은 엔진 사본을 직접 고치지 않는다(오디오 스레드가 설명자를 참조한다).
     *          재생 중 인스턴스와 보이스 수는 오디오 스레드가 블록마다 게시한 사본이다.
     *          고치는 라이브러리 파일이 이 창의 문서다(`EditorDocumentPanel`, Data 종류 가운데 `*.audioevents.xml`) — "고치기" · 콘텐츠 브라우저 · `asset.openScratch` 가 모두 워크스페이스 포커스로 연다.
     *          예전에는 "고치기" 만 여는 자체 길이라 시나리오가 사본(`scratch/…`)을 가리킬 수 없었다.
     */
    class AudioEventBrowserPanel final : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "audioevents";

        AudioEventBrowserPanel();

        const utf8*      getPanelTitle() const override { return "Audio Events"; }
        void             drawDocumentContent() override;
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 109.0f, 27.0f ); }
        /** @brief 고치는 라이브러리 사본을 파일에 씁니다(닫기 · 종료 확인의 Save). 검사를 통과하지 못하면 쓰지 않고 false 입니다. */
        [[nodiscard]] bool saveDocument() override;
        /** @brief 읽은 라이브러리를 고치는 중일 때만 저장합니다("고치기 그만" 뒤에는 쓸 사본이 없다). */
        bool canSaveDocument() const override { return EditorDocumentPanel::canSaveDocument() && _edit.isEditingAny(); }
        /** @brief 엔진에서 모은 이벤트 이름 수입니다(감시 값 `Editor.AudioEventCount`). */
        uint32 getEventCount() const { return static_cast<uint32>( _listEventName.size() ); }
        /** @brief 목록에서 이벤트를 골랐습니다(감시 값 `Editor.AudioEventSelected`). */
        bool hasSelectedEvent() const { return _selectedEvent.empty() == false; }
        /** @brief 고치는 라이브러리 사본의 이벤트 수입니다. 고치는 중이 아니면 0 입니다(감시 값 `Editor.AudioEventEditCount`). */
        uint32 getEditEventCount() const { return _edit.isEditingAny() ? static_cast<uint32>( _edit.getLibrary()._listEvent.size() ) : 0u; }

    protected:
        /** @brief 이벤트 라이브러리 파일(`*.audioevents.xml`)만 받습니다. */
        bool acceptsDocumentPath( string_view assetPath ) const override;
        /** @brief 받아들인 라이브러리 파일을 사본으로 읽어 고치기를 시작합니다. 같은 라이브러리를 다시 읽으면 고치던 이벤트를 그대로 고릅니다. 읽지 못하면 `Malformed` 입니다. */
        ToolAssetLoadResult loadDocument() override;

    private:
        /** @brief 사본의 미저장(`AudioEventEditModel::isDirty`)을 패널 dirty 로 옮깁니다(닫기 · 종료 확인 · 제목 표시가 이 값을 본다). */
        void syncDocumentDirty() override;
        void drawEventList();
        void drawEventDetail( IAudioService& audio );
        /**
         * @brief 고치는 라이브러리 사본을 그립니다 — 저장 · 그만두기, 고칠 이벤트 고르기 · 복제 · 지우기, 이벤트 속성 그리드, 라이브러리의 파라미터 · 감쇠 프리셋 그리드.
         * @details 고칠 이벤트(`AudioEventEditModel::getEventName`)는 사본에서 고른다. 복제 · 지운 이벤트는 저장 뒤 핫 리로드가 엔진 목록에 올리기 전까지 사본에만 있다.
         */
        void drawLibraryEditor();
        /** @brief 속성 그리드가 사본을 고쳤다는 알림입니다(이벤트 · 라이브러리 그리드 둘 다). 저장 전 상태로 표시합니다. */
        void onEventEdited( const PropertyInfo& prop );
        void drawPlayingInstances();
        void drawVoiceProfiler( const AudioEngineStats& stats );

    private:
        AudioEventEditModel                     _edit;             ///< 고치기 상태
        EditorPropertyGrid                      _grid;             ///< 사본 이벤트의 속성 그리드
        EditorPropertyGrid                      _libraryGrid;      ///< 사본 라이브러리의 파라미터 · 감쇠 프리셋 그리드
        vector<hashed_string>                   _listEventName;    ///< 엔진이 준 이벤트 이름(순서 없음)
        vector<hashed_string>                   _listVisibleEvent; ///< 필터를 거친 사전순 이름
        vector<AudioEventFieldRow>              _listFieldRow;
        vector<AudioInstanceStatus>             _listInstanceStatus;
        vector<AudioInstanceStatus>             _listInstanceRow;
        hashed_string                           _selectedEvent;
        hashed_string                           _selectedLibrary; ///< 고른 이벤트가 들어 있는 라이브러리(리소스 경로)
        fixed_string<constant::buffer::kMax128> _filter;
        bool                                    _bOnlySelectedEvent;     ///< 재생 중 목록을 고른 이벤트로 거른다. NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        uint8                                   _bVisibleEventStale : 1; ///< 이름 목록이 바뀌어 다시 걸러야 한다
        const AudioEngine*                      _pEventNameEngine;       ///< `_listEventName` 을 모은 엔진(엔진이 새로 뜨면 세대가 다시 1 부터라 함께 본다)
        uint32                                  _eventNameGeneration;    ///< `_listEventName` 을 모은 때의 `AudioEngine::getEventLibraryGeneration`
        string                                  _visibleEventFilter;     ///< `_listVisibleEvent` 를 거른 검색 글
    };
} // namespace sw::editor
