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

#include "Engine/Audio/AudioEngineState.h"

namespace sw
{
    class IAudioService;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioEventBrowserPanel
     * @brief 언리얼 Sound Cue 에디터의 속성 보기 · 오디오 디버거, FMOD Studio 이벤트 속성 · 프로파일러에 해당합니다.
     * @details 이벤트 속성은 **읽기 전용**이다. 엔진에는 이벤트 라이브러리를 읽는 길(`AudioEventLibrary::loadFromResource`)만 있고 쓰는 길이 없어,
     *          여기서 고치면 저장할 곳이 없다. 대신 "데이터 파일 열기" 로 `*.audioevents.xml` 을 열고, 고쳐 저장하면 에셋 핫 리로드
     *          (`IAudioService` 의 라이브러리 다시 읽기)가 이 창까지 따라오게 한다. 재생 중 인스턴스와 보이스 수는 오디오 스레드가 블록마다 게시한 사본이다.
     */
    class AudioEventBrowserPanel final : public IEditorPanel
    {
    public:
        AudioEventBrowserPanel();

        const utf8* getPanelTitle() const override { return "Audio Events"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 820.0f, 520.0f }; }

    private:
        void drawEventList();
        void drawEventDetail( IAudioService& audio );
        void drawPlayingInstances();
        void drawVoiceProfiler( const AudioEngineStats& stats );

    private:
        vector<hashed_string>                 _listEventName;    ///< 엔진이 준 이벤트 이름(순서 없음)
        vector<hashed_string>                 _listVisibleEvent; ///< 필터를 거친 사전순 이름
        vector<AudioEventFieldRow>            _listFieldRow;
        vector<AudioInstanceStatus>           _listInstanceStatus;
        vector<AudioInstanceStatus>           _listInstanceRow;
        hashed_string                         _selectedEvent;
        hashed_string                         _selectedLibrary; ///< 고른 이벤트가 들어 있는 라이브러리(리소스 경로)
        fixed_string<constant::kMaxBuffer128> _filter;
        bool                                  _bOnlySelectedEvent; ///< 재생 중 목록을 고른 이벤트로 거른다.
    };
} // namespace sw::editor
