/**
 * @file AudioMixerModel.h
 * @brief 오디오 믹서 창의 모델 — 게시된 버스 상태 읽기, 버스마다 천천히 내려가는 피크 표시 값(dB), 감시 값 게시(버스 · 음소거 · 솔로 수, 지켜볼 버스 볼륨),
 *        이벤트 이름 목록(엔진 · 라이브러리 세대가 바뀔 때만 다시 모으기) · 고른 이벤트. ImGui 를 모릅니다.
 * @details 패널(`AudioMixerPanel`)은 띠 · 미터 · 단추를 그리고 클립 경로 입력란만 든다 — 계층 · 인스펙터 `Model/` 과 같은 선이다(언리얼 Audio Mixer 의 버스 상태와 Slate 띠,
 *          유니티 Audio Mixer 창의 그룹 목록과 뷰가 나뉜 것과 같다). 볼륨 · 음소거 · 솔로를 고치는 일은 엔진 명령이라 패널이 바로 부른다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioEngineState.h"

namespace sw
{
    class AudioEngine;
} // namespace sw

namespace sw::editor
{
    /** @brief 감시 값이 읽는 지난 프레임의 값입니다. 에디터 범위의 상태 저장소(`getEditorState`)가 소유한다 — 창이 닫혀도 마지막 값이 남는다. */
    struct AudioMixerProbe
    {
        uint32  _busCount{ 0 };
        uint32  _mutedCount{ 0 };
        uint32  _soloCount{ 0 };
        float32 _probedBusVolume{ -1.0f }; ///< 이름이 `gv_editorWatchBus` 인 버스의 사용자 볼륨(없으면 -1)
    };

    /**
     * @class AudioMixerModel
     * @brief 오디오 믹서 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class AudioMixerModel
    {
    public:
        /** @brief 미터가 보이는 아래쪽 끝(dB)입니다. 이보다 작은 소리는 빈 칸으로 그린다. */
        static constexpr float32 kMeterFloorDb = -60.0f;

        AudioMixerModel();

        /**
         * @brief @p engine 이 게시한 버스 상태를 읽고, 피크 표시 값을 한 프레임만큼 내리고(새 피크가 크면 그 값), 감시 값을 게시합니다. 패널이 그리기마다 부릅니다.
         * @return 버스가 하나라도 있으면 true 입니다(믹서 그래프를 아직 게시하지 않았으면 false).
         */
        bool                          refreshBuses( AudioEngine& engine );
        const vector<AudioBusStatus>& getBusStatuses() const { return _listBusStatus; }
        /** @brief 버스 @p busIndex 의 피크 표시 값(dB)입니다. 소리가 없으면 `kMeterFloorDb` 보다 작다. */
        float32 getHeldPeakDecibel( uint32 busIndex ) const;
        /** @brief 피크 표시 값 @p decibel 이 미터를 채우는 비율(0..1)입니다. */
        static float32 computeMeterFill( float32 decibel );

        /** @brief 엔진이나 이벤트 라이브러리 세대가 바뀌었으면 이벤트 이름을 다시 모읍니다(사전순). 고른 이벤트가 범위 밖이면 첫 이벤트로 둡니다. */
        void                         refreshEventNames( AudioEngine& engine );
        const vector<hashed_string>& getEventNames() const { return _listEventName; }
        uint32                       getSelectedEvent() const { return _selectedEvent; }
        void                         selectEvent( uint32 eventIndex ) { _selectedEvent = eventIndex; }
        /** @brief 고른 이벤트를 @p engine 에 한 번 보냅니다(Post Event). 이벤트가 없으면 아무것도 하지 않습니다. */
        void postSelectedEvent( AudioEngine& engine ) const;

        /** @brief 감시 값이 읽는 지난 프레임의 값입니다. */
        static const AudioMixerProbe& getProbe();

    private:
        vector<AudioBusStatus> _listBusStatus;
        vector<float32>        _listHeldPeak;        ///< 버스마다 천천히 내려가는 피크 표시 값(선형)
        vector<hashed_string>  _listEventName;       ///< 이름 순. 엔진이나 라이브러리 세대가 바뀔 때만 다시 모은다
        const AudioEngine*     _pEventNameEngine;    ///< `_listEventName` 을 모은 엔진(엔진이 새로 뜨면 세대가 다시 1 부터라 함께 본다)
        uint32                 _eventNameGeneration; ///< `_listEventName` 을 모은 때의 `AudioEngine::getEventLibraryGeneration`
        uint32                 _selectedEvent;
    };
} // namespace sw::editor
