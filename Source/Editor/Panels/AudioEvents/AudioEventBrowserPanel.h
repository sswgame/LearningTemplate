/**
 * @file AudioEventBrowserPanel.h
 * @brief 오디오 이벤트 창입니다(Window > Audio Events). 로드된 라이브러리의 이벤트 목록과 속성, 재생 중 인스턴스, 보이스 가상화 프로파일러를 보입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/AudioEvents/AudioEventBrowserUtil.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

#include "Engine/Audio/AudioEngineState.h"
#include "Engine/Audio/AudioEvent.h"

namespace sw
{
    struct PropertyInfo;

    class IAudioService;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioEventBrowserPanel
     * @brief 언리얼 Sound Cue 에디터의 속성 보기 · 오디오 디버거, FMOD Studio 이벤트 속성 · 프로파일러에 해당합니다.
     * @details 이벤트 속성은 기본이 읽기 보기다. "고치기" 를 누르면 그 이벤트가 든 라이브러리 파일을 사본(`_editLibrary`)으로 다시 읽어 속성 그리드로 고치고,
     *          "저장" 은 검사를 통과할 때만 파일에 쓴다(`AudioEventLibrary::saveToResource`). 엔진에 올라간 라이브러리는 저장한 파일을 에셋 핫 리로드
     *          (`IAudioService` 의 라이브러리 다시 읽기)가 바꾼다 — 창은 엔진 사본을 직접 고치지 않는다(오디오 스레드가 설명자를 참조한다).
     *          재생 중 인스턴스와 보이스 수는 오디오 스레드가 블록마다 게시한 사본이다.
     */
    class AudioEventBrowserPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "audioevents";

        AudioEventBrowserPanel();

        const utf8* getPanelTitle() const override { return "Audio Events"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 820.0f, 520.0f }; }

    private:
        void drawEventList();
        void drawEventDetail( IAudioService& audio );
        /** @brief 고른 이벤트의 라이브러리 파일을 사본으로 읽어 고치기를 시작합니다. 읽지 못하면 시작하지 않습니다(읽기가 오류를 남긴다). */
        void beginEditing();
        /** @brief 고치기를 끝내고 사본을 버립니다. */
        void endEditing();
        /** @brief 사본 라이브러리의 이벤트 @p event 를 속성 그리드로 그리고, 저장 · 되돌리기 단추를 둡니다. 같은 파일의 다른 이벤트도 같은 사본에서 고칩니다. */
        void drawEventEditor( AudioEventDesc& event );
        /** @brief 속성 그리드가 사본을 고쳤다는 알림입니다. 저장 전 상태로 표시합니다. */
        void onEventEdited( const PropertyInfo& prop );
        void drawPlayingInstances();
        void drawVoiceProfiler( const AudioEngineStats& stats );

    private:
        AudioEventLibrary                     _editLibrary;      ///< 고치는 중인 라이브러리 사본(파일에서 다시 읽음)
        EditorPropertyGrid                    _grid;             ///< 사본 이벤트의 속성 그리드
        vector<hashed_string>                 _listEventName;    ///< 엔진이 준 이벤트 이름(순서 없음)
        vector<hashed_string>                 _listVisibleEvent; ///< 필터를 거친 사전순 이름
        vector<AudioEventFieldRow>            _listFieldRow;
        vector<AudioInstanceStatus>           _listInstanceStatus;
        vector<AudioInstanceStatus>           _listInstanceRow;
        hashed_string                         _selectedEvent;
        hashed_string                         _selectedLibrary; ///< 고른 이벤트가 들어 있는 라이브러리(리소스 경로)
        hashed_string                         _editLibraryName; ///< `_editLibrary` 를 읽은 리소스 경로(저장할 곳)입니다. 비면 고치기 중이 아닙니다.
        fixed_string<constant::kMaxBuffer128> _filter;
        bool                                  _bOnlySelectedEvent; ///< 재생 중 목록을 고른 이벤트로 거른다.
        bool                                  _bEditDirty;         ///< 사본을 고친 뒤 아직 저장하지 않았다.
    };
} // namespace sw::editor
